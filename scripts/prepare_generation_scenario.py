#!/usr/bin/env python3
"""Build isolated solver inputs for the two 02–05 September scenarios."""

from __future__ import annotations

import argparse
import copy
import json
import math
import sys
from collections import Counter, defaultdict
from datetime import date, timedelta
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import prepare_one_week_generation as quota_engine  # noqa: E402


DATA = ROOT / "data" / "timetable_data.json"
BASELINE = ROOT / "output" / "scenarios" / "baseline_data.json"
MANUAL_DIR = ROOT / "output" / "scenarios" / "manual-sep2"
GENERATED_DIR = ROOT / "output" / "scenarios" / "generated-sep2"
SEMESTER_END = date(2026, 12, 26)
PRIORITY_TEACHERS = {
    "Гарбузов Андрей Евгеньевич",
    "Меренчуков Иван Александрович",
}


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def ensure_baseline() -> dict:
    if not BASELINE.exists():
        write_json(BASELINE, read_json(DATA))
    baseline = read_json(BASELINE)
    changed = apply_operational_policies(baseline)
    if changed:
        write_json(BASELINE, baseline)
    return baseline


def apply_operational_policies(data: dict) -> bool:
    """Apply confirmed operational rules before every scenario build."""
    changed = False

    # These rooms are present in the inventory for historical/reference
    # purposes, but dispatch confirmed that they must never be assigned.
    for room in data.get("rooms", []):
        if str(room.get("name", "")).strip() in {"18/1", "18/2"} and room.get("active") is not False:
            room["active"] = False
            changed = True

    # Campus 0 is Лесная. This is a soft preference: if no suitable room is
    # available, the solver may still use another allowed campus.
    for teacher in data.get("teachers", []):
        if teacher.get("name") == "Вагайская Татьяна Александровна":
            if teacher.get("campus_priority") != [0]:
                teacher["campus_priority"] = [0]
                changed = True
            break

    return changed


def regular_semester_pairs(lesson: dict) -> int:
    hours = max(0, int(lesson.get("total_hours", 0)))
    return math.ceil(hours / 2) if hours else 0


def occurrence_counts(schedule_path: Path) -> Counter[int]:
    schedule = read_json(schedule_path)
    result: Counter[int] = Counter()
    for group in schedule.get("groups", []):
        for day in group.get("days", []):
            for slot in day.get("slots", []):
                for lesson in slot.get("lessons", []):
                    result[int(lesson["id"])] += 1
    return result


def theory_history(data: dict, completed: Counter[int]) -> dict[tuple[int, int], int]:
    lessons = {int(item["id"]): item for item in data.get("lessons", [])}
    result: dict[tuple[int, int], int] = defaultdict(int)
    for lesson_id, count in completed.items():
        lesson = lessons.get(lesson_id)
        if not lesson or lesson.get("is_lab") or lesson.get("is_block") or lesson.get("is_pp"):
            continue
        result[(int(lesson.get("group", -1)), int(lesson.get("subject_id", -1)))] += count
    return result


def dates_between(first: date, last: date) -> list[date]:
    result = []
    current = first
    while current <= last:
        if current.isoweekday() <= 6:
            result.append(current)
        current += timedelta(days=1)
    return result


def build_allocation(
    data: dict,
    first: date,
    last: date,
    target_pairs: int,
    completed: Counter[int],
    minimum_occurrences: dict[int, int] | None = None,
    fixed: list[dict] | None = None,
    preferred: list[dict] | None = None,
    present_teachers_only: set[int] | None = None,
    part_target_overrides: dict[tuple[int, int], int] | None = None,
    prior_theory: dict[tuple[int, int], int] | None = None,
    max_student_pairs: int = 4,
    flexible_parts: bool = False,
    exclude_labs: bool = False,
    hard_no_student_windows: bool = True,
) -> tuple[dict, dict]:
    minimum_occurrences = minimum_occurrences or {}
    fixed = fixed or []
    preferred = preferred or []
    part_target_overrides = part_target_overrides or {}
    prior_theory = prior_theory or {}
    teachers = {int(item["id"]): item for item in data.get("teachers", [])}
    groups = {int(item["id"]): item for item in data.get("groups", [])}
    unavailable = quota_engine.unavailable_dates(data)
    period_dates = dates_between(first, last)

    lessons = []
    remaining: dict[int, int] = {}
    for lesson in data.get("lessons", []):
        lesson_id = int(lesson["id"])
        teacher_id = int(lesson.get("teacher", -1))
        value = max(0, regular_semester_pairs(lesson) - completed.get(lesson_id, 0))
        remaining[lesson_id] = value
        if value <= 0 or teacher_id < 0 or lesson.get("is_block") or lesson.get("is_pp"):
            continue
        if not lesson.get("generation_active", True):
            continue
        if exclude_labs and lesson.get("is_lab", False):
            continue
        if present_teachers_only is not None and teacher_id not in present_teachers_only:
            continue
        lessons.append(lesson)

    teacher_capacity = {
        teacher_id: quota_engine.teacher_capacity_between(
            teacher, unavailable[teacher_id], first, last
        )
        for teacher_id, teacher in teachers.items()
    }
    teacher_future_capacity = {
        teacher_id: quota_engine.teacher_capacity_between(
            teacher, unavailable[teacher_id], last + timedelta(days=1), SEMESTER_END
        )
        for teacher_id, teacher in teachers.items()
    }

    teacher_remaining: dict[int, int] = defaultdict(int)
    for lesson in lessons:
        teacher_remaining[int(lesson["teacher"])] += remaining[int(lesson["id"])]
    teacher_target: dict[int, int] = {}
    teacher_floor: dict[int, int] = {}
    for teacher_id, load in teacher_remaining.items():
        current_capacity = teacher_capacity.get(teacher_id, 0)
        future_capacity = teacher_future_capacity.get(teacher_id, 0)
        total_capacity = current_capacity + future_capacity
        weighted = math.ceil(load * current_capacity / total_capacity) if total_capacity else 0
        teacher_target[teacher_id] = min(current_capacity, weighted)
        teacher_floor[teacher_id] = min(current_capacity, max(0, load - future_capacity))

    priority_ids = {
        teacher_id for teacher_id, teacher in teachers.items()
        if teacher.get("name") in PRIORITY_TEACHERS and teacher_id in teacher_remaining
    }
    for teacher_id in priority_ids:
        teacher_target[teacher_id] = teacher_capacity.get(teacher_id, 0)

    maximum: dict[int, int] = {}
    for lesson in lessons:
        lesson_id = int(lesson["id"])
        teacher_id = int(lesson.get("teacher", -1))
        group_id = int(lesson.get("group", -1))
        placements = sum(
            1 for current in period_dates for pair in range(1, 8)
            if quota_engine.rule_allows(groups.get(group_id), current, pair)
            and current not in unavailable[teacher_id]
            and quota_engine.rule_allows(teachers.get(teacher_id), current, pair)
        )
        maximum[lesson_id] = min(remaining[lesson_id], placements, len(period_dates) * 3)
        if minimum_occurrences.get(lesson_id, 0) > maximum[lesson_id]:
            raise RuntimeError(
                f"Ручная нагрузка занятия {lesson_id} превышает доступный остаток/окна: "
                f"{minimum_occurrences[lesson_id]} > {maximum[lesson_id]}"
            )

    part_lessons: dict[tuple[int, int], list[int]] = defaultdict(list)
    for lesson in lessons:
        for part in quota_engine.lesson_parts(lesson, groups):
            part_lessons[part].append(int(lesson["id"]))
    targets = {
        part: part_target_overrides.get(part, target_pairs)
        for part, lesson_ids in part_lessons.items()
        if any(remaining[lesson_id] > 0 for lesson_id in lesson_ids)
    }
    part_target_ranges: dict[tuple[int, int], tuple[int, int]] = {}
    if flexible_parts:
        fixed_by_lesson_time = {
            (int(item["lesson_id"]), int(item.get("slot", item.get("time", 0))))
            for item in fixed
        }
        fixed_part_slots: dict[tuple[int, int], set[int]] = defaultdict(set)
        lessons_by_id = {int(item["id"]): item for item in lessons}
        for lesson_id, time in fixed_by_lesson_time:
            lesson = lessons_by_id.get(lesson_id)
            if lesson is None:
                continue
            for part in quota_engine.lesson_parts(lesson, groups):
                fixed_part_slots[part].add(time)
        for part in targets:
            minimum = max(2, len(fixed_part_slots.get(part, set())))
            part_target_ranges[part] = (minimum, max_student_pairs * len(period_dates))

    theory_for: dict[tuple[int, int], list[int]] = defaultdict(list)
    for lesson in lessons:
        if not lesson.get("is_lab", False):
            theory_for[(int(lesson["group"]), int(lesson.get("subject_id", -1)))].append(int(lesson["id"]))

    original = {int(lesson["id"]): remaining[int(lesson["id"])] for lesson in lessons}
    quota, optimizer_report, enforced_targets, relaxations = quota_engine.solve_strict_quotas(
        lessons=lessons,
        groups=groups,
        teachers=teachers,
        teacher_unavailable=unavailable,
        maximum=maximum,
        original=original,
        targets=targets,
        teacher_capacity=teacher_capacity,
        teacher_period_target=teacher_target,
        teacher_period_floor=teacher_floor,
        non_relaxable_teachers=priority_ids,
        theory_for=theory_for,
        minimum_occurrences=minimum_occurrences,
        fixed_assignments=fixed,
        preferred_assignments=preferred,
        prior_theory=prior_theory,
        max_student_pairs_per_day=max_student_pairs,
        part_target_ranges=part_target_ranges,
        hard_no_student_windows=hard_no_student_windows,
        rooms=data.get("rooms", []),
    )
    teacher_selected: dict[int, int] = defaultdict(int)
    for lesson in lessons:
        teacher_selected[int(lesson["teacher"])] += quota.get(int(lesson["id"]), 0)
    selected_by_part: dict[tuple[int, int], int] = defaultdict(int)
    for lesson in lessons:
        value = quota.get(int(lesson["id"]), 0)
        for part in quota_engine.lesson_parts(lesson, groups):
            selected_by_part[part] += value

    report = {
        "date_from": first.isoformat(), "date_to": last.isoformat(),
        "target_pairs": target_pairs, "part_target_overrides": {
            f"{group}:{part}": value for (group, part), value in sorted(part_target_overrides.items())
        },
        "completed_before_period": sum(completed.values()),
        "selected_occurrences": sum(quota.values()),
        "selected_pairs_by_part": {
            f"{group}:{part}": value for (group, part), value in sorted(selected_by_part.items())
        },
        "teacher_targets": [
            {
                "teacher": teacher_id, "name": teachers[teacher_id]["name"],
                "minimum_pairs": target, "selected_pairs": teacher_selected.get(teacher_id, 0),
                "minimum_safe_for_deadline": teacher_floor.get(teacher_id, 0),
                "remaining_semester_pairs": teacher_remaining.get(teacher_id, 0),
                "current_capacity": teacher_capacity.get(teacher_id, 0),
                "future_capacity": teacher_future_capacity.get(teacher_id, 0),
            }
            for teacher_id, target in sorted(enforced_targets.items()) if target > 0
        ],
        "target_relaxations": relaxations,
        "quota_optimizer": optimizer_report,
    }
    return quota, report


def apply_snapshot(
    baseline: dict,
    quota: dict[int, int],
    report: dict,
    first: date,
    last: date,
    label: str,
    prior_theory: dict[tuple[int, int], int],
    max_student_pairs: int,
    hard_no_student_windows: bool = True,
) -> dict:
    snapshot = copy.deepcopy(baseline)
    active = 0
    for lesson in snapshot.get("lessons", []):
        value = quota.get(int(lesson["id"]), 0)
        lesson["plan_active"] = value > 0
        lesson["total_slots"] = value
        lesson["week_parity"] = "all"
        if value > 0:
            active += 1
    settings = snapshot.setdefault("settings", {})
    settings["start_date"] = first.isoformat()
    settings["end_date"] = last.isoformat()
    settings["prior_theory_pairs"] = [
        {"group": group, "subject": subject, "pairs": pairs}
        for (group, subject), pairs in sorted(prior_theory.items()) if pairs > 0
    ]
    settings["teacher_period_targets"] = report["teacher_targets"]
    settings.setdefault("solver_config", {}).update({
        "solver_time_limit_seconds": 180,
        "week_time_limit_seconds": 150,
        "solver_workers": 4,
        "quality_improvement_seconds": 120,
        "hard_no_student_windows": hard_no_student_windows,
        "hard_no_teacher_windows": False,
        "hard_min_study_days_per_week": True,
        "hard_max_one_two_pair_student_day": False,
        "hard_max_two_same_subject_per_day": True,
        "max_same_subject_pairs_per_day": 3,
        "max_whole_group_same_subject_pairs_per_day": 2,
        "max_student_pairs_per_day": max_student_pairs,
        "min_student_pairs_per_study_day": 2,
        "min_student_study_days_per_week": len(dates_between(first, last)),
        "min_initial_theory_slots_before_labs": 2,
        "use_quality_objective": True,
        "optimize_teacher_windows": True,
        "optimize_student_windows": not hard_no_student_windows,
        "teacher_window_weight": 12000,
        "teacher_campus_preference_weight": 1500,
        "student_window_weight": 20000 if not hard_no_student_windows else 1000,
        "student_two_pair_day_weight": 4000,
        "student_late_slot_weight": 20,
        "student_five_pair_day_weight": 0,
        "random_seed": 37,
    })
    snapshot.setdefault("meta", {})["generation_scenario"] = {
        "label": label, "date_from": first.isoformat(), "date_to": last.isoformat(),
        "active_lessons": active, "selected_occurrences": report["selected_occurrences"],
        "completed_before_period": report["completed_before_period"],
    }
    return snapshot


def prepare_sep2(mode: str) -> Path:
    baseline = ensure_baseline()
    first = last = date(2026, 9, 2)
    completed: Counter[int] = Counter()
    minimum: dict[int, int] = {}
    fixed: list[dict] = []
    preferred: list[dict] = []
    present: set[int] | None = None
    overrides: dict[tuple[int, int], int] = {}
    max_student = 4
    label = f"{mode}-sep2"
    target_dir = MANUAL_DIR if mode == "manual" else GENERATED_DIR

    if mode == "manual":
        mapping = read_json(MANUAL_DIR / "mapping_report.json")
        locks = read_json(MANUAL_DIR / "locks.json")
        minimum = {int(key): int(value) for key, value in locks["required_occurrences"].items()}
        preferred = [
            {"lesson_id": int(event["lesson_id"]), "slot": int(event["pair"]) - 1}
            for event in mapping["events"]
        ]
        present = {int(event["teacher_id"]) for event in mapping["events"]}
        max_student = 5
        groups = {int(item["id"]): item for item in baseline["groups"]}
        # Only ТОиРА-2701п needs a fifth pair: it closes the manual 1–4 block
        # after the simultaneous Samtsov lesson is moved to pair 5.
        for part in range(max(1, int(groups[30].get("parts", 2)))):
            overrides[(30, part)] = 5

        # Existing teachers and groups are factual for this scenario.  Let a
        # present teacher work throughout the day, but keep the campus from the
        # manual sheet.  Teachers absent from it receive no new lesson.
        teacher_campus: dict[int, set[int]] = defaultdict(set)
        group_campus: dict[int, set[int]] = defaultdict(set)
        for event in mapping["events"]:
            if event.get("campus") in (0, 1):
                teacher_campus[int(event["teacher_id"])].add(int(event["campus"]))
                group_campus[int(event["group_id"])].add(int(event["campus"]))
        mixed = {
            "teachers": {str(key): sorted(value) for key, value in teacher_campus.items() if len(value) > 1},
            "groups": {str(key): sorted(value) for key, value in group_campus.items() if len(value) > 1},
        }
        if mixed["teachers"] or mixed["groups"]:
            raise RuntimeError("Ручная таблица меняет площадку внутри дня: " + json.dumps(mixed, ensure_ascii=False))
        for teacher in baseline["teachers"]:
            teacher_id = int(teacher["id"])
            if teacher_id in present:
                teacher["max_pairs_per_day"] = max(7, int(teacher.get("max_pairs_per_day", 0)))
                teacher.setdefault("date_slot_overrides", [])
                teacher["date_slot_overrides"] = [
                    item for item in teacher["date_slot_overrides"] if item.get("date") != first.isoformat()
                ] + [{"date": first.isoformat(), "slots": list(range(1, 8))}]
                if teacher_campus.get(teacher_id):
                    teacher["allowed_campuses"] = sorted(teacher_campus[teacher_id])
        for group in baseline["groups"]:
            group_id = int(group["id"])
            group.setdefault("date_slot_overrides", [])
            group["date_slot_overrides"] = [
                item for item in group["date_slot_overrides"] if item.get("date") != first.isoformat()
            ] + [{"date": first.isoformat(), "slots": list(range(1, 8))}]
        for lesson in baseline["lessons"]:
            campuses = group_campus.get(int(lesson.get("group", -1)))
            if campuses:
                lesson["allowed_campuses"] = sorted(campuses)
    else:
        # A fully generated first day must not be artificially capped at an
        # exact four pairs.  Let the quota model fill every feasible student
        # part up to four pairs and maximize the total taught load.  The
        # unrestricted five-pair optimum overloads the actual room/campus
        # inventory once concrete rooms are assigned.  Labs are
        # intentionally excluded on the first day because there is no prior
        # theory history yet.
        max_student = 4

    quota_engine.START = first
    quota_engine.END = last
    quota_engine.MIN_PAIRS_PER_STUDY_DAY = 2
    quota_engine.TARGET_PAIRS_PER_WEEK = 4
    quota, report = build_allocation(
        baseline, first, last, 4, completed,
        minimum_occurrences=minimum, fixed=fixed, preferred=preferred,
        present_teachers_only=present, part_target_overrides=overrides,
        max_student_pairs=max_student, flexible_parts=True,
        exclude_labs=True,
        hard_no_student_windows=True,
    )
    snapshot = apply_snapshot(baseline, quota, report, first, last, label, {}, max_student, True)
    input_path = target_dir / "input_sep2.json"
    write_json(input_path, snapshot)
    write_json(target_dir / "allocation_sep2.json", report)
    witness = report.get("quota_optimizer", {}).get("placement_witness", {})
    optimized_assignments = [
        {"lesson_id": int(lesson_id), "date": first.isoformat(), "slot": int(time)}
        for lesson_id, times in witness.items() for time in times
    ]
    if len(optimized_assignments) != report["selected_occurrences"]:
        raise RuntimeError("Оптимизатор не вернул полный свидетель расстановки 2 сентября")
    preferred_keys = {(int(item["lesson_id"]), int(item["slot"])) for item in preferred}
    optimized_keys = {(item["lesson_id"], item["slot"]) for item in optimized_assignments}
    write_json(target_dir / "optimized_locks_sep2.json", {
        "source": "optimized_manual",
        "assignments": optimized_assignments,
        "manual_positions_total": len(preferred_keys),
        "manual_positions_preserved": len(preferred_keys & optimized_keys),
        "manual_positions_changed": len(preferred_keys - optimized_keys),
    })
    write_json(DATA, snapshot)
    return input_path


def prepare_after_sep2(mode: str, schedule: Path) -> Path:
    baseline = ensure_baseline()
    completed = occurrence_counts(schedule)
    history = theory_history(baseline, completed)
    first, last = date(2026, 9, 3), date(2026, 9, 5)
    quota_engine.START = first
    quota_engine.END = last
    quota_engine.MIN_PAIRS_PER_STUDY_DAY = 2
    quota_engine.TARGET_PAIRS_PER_WEEK = 9
    quota, report = build_allocation(
        baseline, first, last, 9, completed,
        prior_theory=history, max_student_pairs=4,
    )
    label = f"{mode}-after-sep2"
    snapshot = apply_snapshot(baseline, quota, report, first, last, label, history, 4)
    target_dir = MANUAL_DIR if mode == "manual" else GENERATED_DIR
    input_path = target_dir / "input_sep3_5.json"
    write_json(input_path, snapshot)
    write_json(target_dir / "allocation_sep3_5.json", report)
    write_json(target_dir / "completed_sep2.json", {
        "schedule": str(schedule), "occurrences": {str(key): value for key, value in sorted(completed.items())},
        "total": sum(completed.values()),
    })
    write_json(DATA, snapshot)
    return input_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("sep2", "after-sep2", "restore-baseline"))
    parser.add_argument("--mode", choices=("manual", "generated"), default="manual")
    parser.add_argument("--schedule", type=Path)
    args = parser.parse_args()
    if args.action == "restore-baseline":
        write_json(DATA, ensure_baseline())
        print(BASELINE)
        return 0
    if args.action == "sep2":
        result = prepare_sep2(args.mode)
    else:
        if not args.schedule:
            raise SystemExit("--schedule is required for after-sep2")
        result = prepare_after_sep2(args.mode, args.schedule)
    print(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
