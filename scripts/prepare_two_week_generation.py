#!/usr/bin/env python3
"""Prepare and restore a safe two-week solver snapshot.

The source workbook contains semester totals.  The weekly solver normally treats
the selected date range as the whole planning horizon, so a two-week run would
otherwise try to place half of every semester subject.  This helper temporarily
converts every active lesson to its rounded 2/16 share, keeps an exact backup,
and restores the semester source after generation.
"""

from __future__ import annotations

import argparse
import json
import math
import shutil
from collections import defaultdict
from datetime import date, datetime, timedelta
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "timetable_data.json"
STATE = ROOT / "data" / ".two_week_generation_state.json"
HISTORY = ROOT / "data" / "history"


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, value: dict) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    temporary.replace(path)


def rounded(value: float) -> int:
    return math.floor(value + 0.5)


def teacher_capacities(source: dict) -> dict[int, int]:
    dates: list[date] = []
    current = date(2026, 9, 1)
    finish = date(2026, 9, 12)
    while current <= finish:
        if current.isoweekday() <= 6:
            dates.append(current)
        current += timedelta(days=1)

    unavailable: dict[int, set[date]] = defaultdict(set)
    for item in source.get("teacher_unavailable", []):
        teacher = int(item.get("teacher", -1))
        for raw in item.get("dates", []):
            try:
                unavailable[teacher].add(date.fromisoformat(raw))
            except ValueError:
                pass
        try:
            start = date.fromisoformat(item.get("from", ""))
            end = date.fromisoformat(item.get("to", ""))
        except ValueError:
            continue
        while start <= end:
            unavailable[teacher].add(start)
            start += timedelta(days=1)

    result: dict[int, int] = {}
    for teacher in source.get("teachers", []):
        teacher_id = int(teacher["id"])
        work_days = {int(item["day"]): item for item in teacher.get("work_days", [])}
        capacity = 0
        for current in dates:
            if current in unavailable[teacher_id]:
                continue
            rule = work_days.get(current.isoweekday())
            if rule is None:
                capacity += 7
            elif rule.get("enabled", True):
                first = max(1, int(rule.get("start_slot", 1)))
                last = min(7, int(rule.get("end_slot", 7)))
                capacity += max(0, last - first + 1)
        result[teacher_id] = capacity
    return result


def build_two_week_quotas(source: dict) -> dict:
    """Allocate group 2/16 targets while respecting teacher availability."""
    lessons = [
        lesson
        for lesson in source.get("lessons", [])
        if lesson.get("plan_active", True)
        and not lesson.get("is_block", False)
        and not lesson.get("is_pp", False)
    ]
    capacities = teacher_capacities(source)
    # A few imported workloads consume literally every available position and
    # leave the exact day/campus model no room to permute lessons.  Keep a tiny
    # operational reserve for those teachers.  Garbuzov (67) intentionally
    # remains at the full seven Saturday pairs requested by the user.
    for teacher_id, safe_capacity in {15: 12, 64: 70, 65: 34}.items():
        if teacher_id in capacities:
            capacities[teacher_id] = min(capacities[teacher_id], safe_capacity)
    teachers = {int(item["id"]): item for item in source.get("teachers", [])}
    groups = {int(item["id"]): item for item in source.get("groups", [])}
    unavailable: dict[int, set[date]] = defaultdict(set)
    for item in source.get("teacher_unavailable", []):
        teacher = int(item.get("teacher", -1))
        for raw in item.get("dates", []):
            try:
                unavailable[teacher].add(date.fromisoformat(raw))
            except ValueError:
                pass

    dates: list[date] = []
    current = date(2026, 9, 1)
    while current <= date(2026, 9, 12):
        if current.isoweekday() <= 6:
            dates.append(current)
        current += timedelta(days=1)

    def schedule_allows(entity: dict | None, current_date: date, slot: int) -> bool:
        if entity is None:
            return True
        rule = next(
            (item for item in entity.get("work_days", [])
             if int(item.get("day", 0)) == current_date.isoweekday()),
            None,
        )
        if rule is None:
            return True
        pair_number = slot + 1
        return (
            rule.get("enabled", True)
            and int(rule.get("start_slot", 1)) <= pair_number
            and pair_number <= int(rule.get("end_slot", 7))
        )

    def placement_capacity(lesson: dict) -> int:
        teacher = int(lesson.get("teacher", -1))
        group = int(lesson.get("group", -1))
        result = 0
        for current_date in dates:
            if teacher >= 0 and current_date in unavailable[teacher]:
                continue
            starts = (0, 2) if lesson.get("is_block", False) else range(7)
            for slot in starts:
                if not schedule_allows(groups.get(group), current_date, slot):
                    continue
                if teacher >= 0 and not schedule_allows(teachers.get(teacher), current_date, slot):
                    continue
                if lesson.get("is_block", False):
                    if not schedule_allows(groups.get(group), current_date, slot + 1):
                        continue
                    if teacher >= 0 and not schedule_allows(
                        teachers.get(teacher), current_date, slot + 1
                    ):
                        continue
                result += 1
        return result

    quota: dict[int, int] = {}
    original: dict[int, int] = {}
    maximum: dict[int, int] = {}

    for lesson in lessons:
        lesson_id = int(lesson["id"])
        total = int(lesson.get("total_slots", 0))
        original[lesson_id] = total
        maximum[lesson_id] = min(
            max(1, math.ceil(total / 8.0)), placement_capacity(lesson)
        )
        quota[lesson_id] = min(total // 8, maximum[lesson_id])

    def occupied(lesson: dict) -> int:
        return 2 if lesson.get("is_block", False) else 1

    def teacher_loads() -> dict[int, int]:
        result: dict[int, int] = defaultdict(int)
        for lesson in lessons:
            teacher = int(lesson.get("teacher", -1))
            if teacher >= 0:
                result[teacher] += quota[int(lesson["id"])] * occupied(lesson)
        return result

    loads = teacher_loads()
    removed_for_teacher_capacity = 0
    by_teacher: dict[int, list[dict]] = defaultdict(list)
    for lesson in lessons:
        by_teacher[int(lesson.get("teacher", -1))].append(lesson)

    for teacher, teacher_lessons in by_teacher.items():
        if teacher < 0:
            continue
        capacity = capacities.get(teacher, 77)
        # Remove the least urgent fractional shares first.  A block consumes
        # two consecutive pairs, therefore its quota unit costs two.
        candidates = sorted(
            teacher_lessons,
            key=lambda item: (
                original[int(item["id"])] % 8,
                -occupied(item),
                original[int(item["id"])],
            ),
        )
        while loads[teacher] > capacity:
            candidate = next(
                (item for item in candidates if quota[int(item["id"])] > 0), None
            )
            if candidate is None:
                break
            lesson_id = int(candidate["id"])
            quota[lesson_id] -= 1
            delta = occupied(candidate)
            loads[teacher] -= delta
            removed_for_teacher_capacity += delta

    # The target is calculated from the full semester load of each subgroup,
    # not by rounding every row independently (which inflates small subjects).
    group_target: dict[tuple[int, int], int] = defaultdict(int)
    target_fraction: dict[tuple[int, int], float] = defaultdict(float)
    for lesson in lessons:
        delta = original[int(lesson["id"])] * occupied(lesson) / 8.0
        group = int(lesson.get("group", -1))
        subgroup = int(lesson.get("subgroup", -1))
        for part in (0, 1):
            if subgroup in (-1, part):
                target_fraction[(group, part)] += delta
    for key, value in target_fraction.items():
        group_target[key] = min(44, rounded(value))

    def group_loads() -> dict[tuple[int, int], int]:
        result: dict[tuple[int, int], int] = defaultdict(int)
        for lesson in lessons:
            delta = quota[int(lesson["id"])] * occupied(lesson)
            group = int(lesson.get("group", -1))
            subgroup = int(lesson.get("subgroup", -1))
            for part in (0, 1):
                if subgroup in (-1, part):
                    result[(group, part)] += delta
        return result

    current_group = group_loads()
    loads = teacher_loads()
    added_for_group_targets = 0

    # Fill the gaps with the subjects having the largest unallocated semester
    # remainder.  This preserves group pace even when an overloaded teacher's
    # subjects must be deferred to later weeks.
    while True:
        best: tuple[tuple, dict] | None = None
        for lesson in lessons:
            lesson_id = int(lesson["id"])
            if quota[lesson_id] >= maximum[lesson_id]:
                continue
            delta = occupied(lesson)
            teacher = int(lesson.get("teacher", -1))
            if teacher >= 0 and loads.get(teacher, 0) + delta > capacities.get(teacher, 77):
                continue
            group = int(lesson.get("group", -1))
            subgroup = int(lesson.get("subgroup", -1))
            parts = [part for part in (0, 1) if subgroup in (-1, part)]
            deficits = [group_target[(group, part)] - current_group[(group, part)] for part in parts]
            if not deficits or min(deficits) < delta:
                continue
            score = (
                min(deficits),
                original[lesson_id] % 8,
                0 if teacher < 0 else 1,
                -quota[lesson_id],
            )
            if best is None or score > best[0]:
                best = (score, lesson)
        if best is None:
            break
        lesson = best[1]
        lesson_id = int(lesson["id"])
        delta = occupied(lesson)
        quota[lesson_id] += 1
        teacher = int(lesson.get("teacher", -1))
        if teacher >= 0:
            loads[teacher] += delta
        group = int(lesson.get("group", -1))
        subgroup = int(lesson.get("subgroup", -1))
        for part in (0, 1):
            if subgroup in (-1, part):
                current_group[(group, part)] += delta
        added_for_group_targets += delta

    deferred = []
    teacher_names = {int(item["id"]): item.get("name", "") for item in source.get("teachers", [])}
    for teacher, value in sorted(loads.items(), key=lambda item: item[1], reverse=True):
        if value >= capacities.get(teacher, 77):
            deferred.append(
                {
                    "teacher": teacher,
                    "name": teacher_names.get(teacher, ""),
                    "scheduled_pairs": value,
                    "capacity": capacities.get(teacher, 77),
                }
            )

    unfilled = sum(
        max(0, group_target[key] - current_group.get(key, 0)) for key in group_target
    )
    return {
        "quota": quota,
        "group_target": group_target,
        "group_load": current_group,
        "teacher_load": loads,
        "teacher_capacity": capacities,
        "removed_for_teacher_capacity": removed_for_teacher_capacity,
        "added_for_group_targets": added_for_group_targets,
        "unfilled_group_part_pairs": unfilled,
        "capacity_limited_teachers": deferred,
    }


def prepare() -> None:
    if STATE.exists():
        raise SystemExit(
            f"Temporary generation state already exists: {STATE}. Restore it first."
        )

    source = read_json(DATA)
    HISTORY.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup = HISTORY / f"before_two_week_generation_{stamp}.json"
    shutil.copy2(DATA, backup)

    active_lessons = 0
    semester_starts = 0
    snapshot_starts = 0
    allocation = build_two_week_quotas(source)

    for lesson in source.get("lessons", []):
        if not lesson.get("plan_active", True):
            continue
        original = int(lesson.get("total_slots", 0))
        semester_starts += original
        if lesson.get("is_block", False) or lesson.get("is_pp", False):
            lesson["plan_active"] = False
            continue
        reduced = allocation["quota"][int(lesson["id"])]
        if reduced <= 0:
            lesson["plan_active"] = False
            continue
        active_lessons += 1
        lesson["total_slots"] = reduced
        snapshot_starts += reduced

    settings = source.setdefault("settings", {})
    # Anchor on Monday so the backend forms real calendar weeks.  Monday is
    # explicitly unavailable; the first actual lesson remains Tuesday 01.09.
    settings["start_date"] = "2026-08-31"
    settings["end_date"] = "2026-09-12"
    source.setdefault("unavailable", []).append(
        {
            "id": 900000,
            "uid": "temporary-two-week-start-tuesday",
            "all_groups": True,
            "dates": ["2026-08-31"],
            "text": "Период расписания начинается со вторника 01.09.2026",
        }
    )
    config = settings.setdefault("solver_config", {})
    config.update(
        {
            "solver_time_limit_seconds": 1200,
            "week_time_limit_seconds": 300,
            "solver_workers": 4,
            "stop_after_first_solution": False,
            "linearization_level": 0,
            "symmetry_level": 2,
            "random_seed": 7,
            "quality_improvement_seconds": 45,
            "hard_no_student_windows": True,
            "hard_no_teacher_windows": False,
            "hard_min_study_days_per_week": False,
            "hard_min_2_teacher_pairs_per_day": False,
            "use_quality_objective": True,
            "optimize_teacher_windows": True,
            "optimize_student_windows": False,
            "max_student_pairs_per_day": 4,
            "min_student_pairs_per_study_day": 2,
            "min_student_study_days_per_week": 3,
            "group_week_missing_day_weight": 40,
            "student_five_pair_day_weight": 10000,
            "student_late_slot_weight": 1,
            "teacher_late_slot_weight": 1,
            "teacher_window_weight": 450,
            "student_window_weight": 1000,
            "min_subject_spread_total_slots": 9999,
        }
    )
    meta = source.setdefault("meta", {})
    meta["temporary_generation"] = {
        "kind": "two_week_semester_share",
        "semester_weeks": 16,
        "date_from": "2026-09-01",
        "date_to": "2026-09-12",
        "excluded": ["УП", "ПП"],
        "backup": str(backup),
    }

    state = {
        "backup": str(backup),
        "prepared_at": datetime.now().isoformat(timespec="seconds"),
        "semester_starts": semester_starts,
        "snapshot_starts": snapshot_starts,
        "active_lessons": active_lessons,
        "max_group_part_pairs": max(allocation["group_load"].values(), default=0),
        "max_teacher_pairs": max(allocation["teacher_load"].values(), default=0),
        "removed_for_teacher_capacity": allocation["removed_for_teacher_capacity"],
        "added_for_group_targets": allocation["added_for_group_targets"],
        "unfilled_group_part_pairs": allocation["unfilled_group_part_pairs"],
        "capacity_limited_teachers": allocation["capacity_limited_teachers"],
    }
    write_json(DATA, source)
    write_json(STATE, state)
    print(json.dumps(state, ensure_ascii=False, indent=2))


def restore() -> None:
    if not STATE.exists():
        raise SystemExit("No temporary two-week generation state exists.")
    state = read_json(STATE)
    backup = Path(state["backup"])
    if not backup.exists():
        raise SystemExit(f"Backup is missing: {backup}")
    shutil.copy2(backup, DATA)
    STATE.unlink()
    print(json.dumps({"restored": str(backup)}, ensure_ascii=False, indent=2))


def status() -> None:
    if not STATE.exists():
        print(json.dumps({"prepared": False}, indent=2))
        return
    value = read_json(STATE)
    value["prepared"] = True
    print(json.dumps(value, ensure_ascii=False, indent=2))


def mirror_first_week() -> None:
    """Use the last strict first-week solution as equal quotas for both weeks."""
    if not STATE.exists():
        raise SystemExit("Prepare the two-week snapshot first.")
    schedule_path = ROOT / "output" / "latest" / "schedule_all.json"
    if not schedule_path.exists():
        raise SystemExit(f"Partial schedule is missing: {schedule_path}")
    schedule = read_json(schedule_path)
    counts: dict[int, int] = defaultdict(int)
    for group in schedule.get("groups", []):
        for day in group.get("days", []):
            date_iso = day.get("date_iso", "")
            if not ("2026-09-01" <= date_iso <= "2026-09-05"):
                continue
            for slot in day.get("slots", []):
                for lesson in slot.get("lessons", []):
                    counts[int(lesson["id"])] += 1

    data = read_json(DATA)
    active = 0
    starts_per_week = 0
    for lesson in data.get("lessons", []):
        if not lesson.get("plan_active", True):
            continue
        count = counts.get(int(lesson["id"]), 0)
        if count <= 0:
            lesson["plan_active"] = False
            continue
        lesson["total_slots"] = count * 2
        lesson["week_parity"] = "all"
        active += 1
        starts_per_week += count

    # Если на две недели у группы осталось ровно две пары, равномерное
    # распределение 1+1 неизбежно создаёт запрещённые одиночные дни. Сохраняем
    # общий объём, но проводим обе пары на нечётной неделе (2+0).
    cohort_totals: dict[int, list[int]] = defaultdict(lambda: [0, 0])
    for lesson in data.get("lessons", []):
        if not lesson.get("plan_active", True):
            continue
        group_id = int(lesson.get("group", -1))
        subgroup = int(lesson.get("subgroup", -1))
        occurrences = int(lesson.get("total_slots", 0))
        if subgroup < 0:
            cohort_totals[group_id][0] += occurrences
            cohort_totals[group_id][1] += occurrences
        else:
            cohort_totals[group_id][subgroup % 2] += occurrences
    packed_groups = sorted(
        group_id for group_id, totals in cohort_totals.items() if totals == [2, 2]
    )
    for lesson in data.get("lessons", []):
        if lesson.get("plan_active", True) and int(lesson.get("group", -1)) in packed_groups:
            lesson["week_parity"] = "odd"

    for teacher in data.get("teachers", []):
        if teacher.get("name") == "Колтышев Евгений Валерьевич":
            teacher["max_work_days_per_week"] = 1
    config = data.setdefault("settings", {}).setdefault("solver_config", {})
    config.update(
        {
            "max_student_pairs_per_day": 4,
            "min_student_pairs_per_study_day": 2,
            "hard_no_student_windows": True,
            "student_five_pair_day_weight": 100000,
            "quality_improvement_seconds": 45,
        }
    )
    data.setdefault("meta", {}).setdefault("temporary_generation", {}).update(
        {
            "quota_strategy": "mirror_strict_first_week",
            "starts_per_week": starts_per_week,
            "packed_singleton_groups": packed_groups,
            "koltyshev_max_work_days_per_week": 1,
        }
    )
    write_json(DATA, data)
    state = read_json(STATE)
    state.update(
        {
            "mirrored": True,
            "mirrored_active_lessons": active,
            "mirrored_starts_per_week": starts_per_week,
            "packed_singleton_groups": packed_groups,
        }
    )
    write_json(STATE, state)
    print(
        json.dumps(
            {"active_lessons": active, "starts_per_week": starts_per_week},
            ensure_ascii=False,
            indent=2,
        )
    )


def filter_unsequenced_labs() -> None:
    """Defer LPZ rows that have no active theory row in the two-week slice."""
    if not STATE.exists():
        raise SystemExit("Prepare the two-week snapshot first.")
    data = read_json(DATA)
    theory_keys = {
        (int(lesson.get("group", -1)), int(lesson.get("subject_id", -1)))
        for lesson in data.get("lessons", [])
        if lesson.get("plan_active", True) and not lesson.get("is_lab", False)
    }
    deferred = 0
    for lesson in data.get("lessons", []):
        if not lesson.get("plan_active", True) or not lesson.get("is_lab", False):
            continue
        key = (int(lesson.get("group", -1)), int(lesson.get("subject_id", -1)))
        if key in theory_keys:
            continue
        lesson["plan_active"] = False
        deferred += 1
    data.setdefault("meta", {}).setdefault("temporary_generation", {}).update(
        {"lpz_without_active_theory_deferred": deferred}
    )
    write_json(DATA, data)
    state = read_json(STATE)
    state["lpz_without_active_theory_deferred"] = deferred
    write_json(STATE, state)
    print(json.dumps({"deferred_lpz": deferred}, ensure_ascii=False, indent=2))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "action",
        choices=(
            "prepare",
            "restore",
            "status",
            "mirror-first-week",
            "filter-unsequenced-labs",
        ),
    )
    args = parser.parse_args()
    {
        "prepare": prepare,
        "restore": restore,
        "status": status,
        "mirror-first-week": mirror_first_week,
        "filter-unsequenced-labs": filter_unsequenced_labs,
    }[args.action]()


if __name__ == "__main__":
    main()
