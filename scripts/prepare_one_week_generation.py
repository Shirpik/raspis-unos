"""Prepare/restore a short-week snapshot for 03–05 September 2026.

The workbook contains semester totals.  This helper selects approximately one
seventeenth of each cohort's semester load, caps students at four pairs per day,
removes UP/PP, and defers LPZ if no theory is selected for the same subject.
"""

from __future__ import annotations

import argparse
import copy
import json
import math
import shutil
import subprocess
import tempfile
from collections import defaultdict
from datetime import date, datetime, timedelta
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "timetable_data.json"
STATE = ROOT / "data" / ".one_week_generation_state.json"
HISTORY = ROOT / "data" / "history"
START = date(2026, 9, 3)
END = date(2026, 9, 5)
SEMESTER_END = date(2026, 12, 26)
MIN_PAIRS_PER_STUDY_DAY = 2
TARGET_PAIRS_PER_WEEK = 9
SEMESTER_DISTRIBUTION_WEEKS = 17
PRIORITY_TEACHER_NAMES = {
    "Гарбузов Андрей Евгеньевич",
    "Меренчуков Иван Александрович",
}

def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, value: dict) -> None:
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temp.replace(path)


def dates() -> list[date]:
    result = []
    current = START
    while current <= END:
        if current.isoweekday() <= 6:
            result.append(current)
        current += timedelta(days=1)
    return result


def rule_allows(entity: dict | None, current: date, pair: int) -> bool:
    if entity is None:
        return True
    period = entity.get("work_period") or {}
    raw_from = str(period.get("from", ""))
    raw_to = str(period.get("to", ""))
    if raw_from and raw_to:
        try:
            if not date.fromisoformat(raw_from) <= current <= date.fromisoformat(raw_to):
                return False
        except ValueError:
            return False
    for override in entity.get("date_slot_overrides", []):
        if override.get("date") == current.isoformat():
            return pair in {int(slot) for slot in override.get("slots", [])}
    rule = next((item for item in entity.get("work_days", []) if int(item.get("day", 0)) == current.isoweekday()), None)
    if rule is None:
        return True
    if not bool(rule.get("enabled", True)):
        return False
    if isinstance(rule.get("slots"), list):
        return pair in {int(slot) for slot in rule.get("slots", [])}
    return int(rule.get("start_slot", 1)) <= pair <= int(rule.get("end_slot", 7))


def unavailable_dates(source: dict) -> dict[int, set[date]]:
    result: dict[int, set[date]] = defaultdict(set)
    for item in source.get("teacher_unavailable", []):
        teacher = int(item.get("teacher", -1))
        for raw in item.get("dates", []):
            try:
                result[teacher].add(date.fromisoformat(raw))
            except ValueError:
                pass
        try:
            first = date.fromisoformat(item.get("from", ""))
            last = date.fromisoformat(item.get("to", ""))
        except ValueError:
            continue
        while first <= last:
            result[teacher].add(first)
            first += timedelta(days=1)
    return result


def teacher_capacity_between(
    teacher: dict,
    unavailable: set[date],
    first_date: date,
    last_date: date,
) -> int:
    """Return real teaching-slot capacity, respecting weekly day limits."""
    by_week: dict[tuple[int, int], list[int]] = defaultdict(list)
    current = first_date
    while current <= last_date:
        if current.isoweekday() <= 6:
            slots = 0 if current in unavailable else sum(
                1 for pair in range(1, 8) if rule_allows(teacher, current, pair)
            )
            max_daily = int(teacher.get("max_pairs_per_day", 0))
            if max_daily > 0:
                slots = min(slots, max_daily)
            iso = current.isocalendar()
            by_week[(iso.year, iso.week)].append(slots)
        current += timedelta(days=1)

    total = 0
    max_days = int(teacher.get("max_work_days_per_week", 0))
    for daily in by_week.values():
        if max_days > 0:
            daily = sorted(daily, reverse=True)[:max_days]
        total += sum(daily)
    return total


def semester_total_slots(lesson: dict) -> int:
    """Restore the imported semester occurrence count from academic hours.

    The short-week helper overwrites ``total_slots`` with a temporary quota.
    HTTP history may therefore contain a mixture of semester totals and short
    quotas.  ``total_hours`` is the preserved import source: ordinary/PP
    occurrences consume two academic hours and an UP block consumes six.
    """
    hours = int(lesson.get("total_hours", 0))
    if hours <= 0:
        return 0
    return max(1, math.ceil(hours / (6 if lesson.get("is_block", False) else 2)))


def lesson_parts(lesson: dict, groups: dict[int, dict]) -> list[tuple[int, int]]:
    """Return real physical subgroup streams affected by a lesson.

    Persisted subgroup IDs are global (``group_id * 2 + local_part``), not the
    local values 0/1.  Treating them as local silently dropped virtually every
    subgroup lesson outside group 0 from the short-week allocation.
    """
    group_id = int(lesson.get("group", -1))
    group = groups.get(group_id)
    if group is None:
        return []
    part_count = max(1, int(group.get("parts", 2)))
    subgroup = int(lesson.get("subgroup", -1))
    if subgroup == -1:
        return [(group_id, part) for part in range(part_count)]
    local_part = subgroup - group_id * 2
    if 0 <= local_part < part_count:
        return [(group_id, local_part)]
    return []


def quota_optimizer_path() -> Path:
    """Locate the exact CP-SAT quota helper built together with the solver."""
    executable = "quota_optimizer.exe" if __import__("os").name == "nt" else "quota_optimizer"
    candidates = (
        ROOT / "build" / executable,
        ROOT / "build-desktop" / executable,
        ROOT / executable,
    )
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise RuntimeError(
        "Не найден строгий оптимизатор недельных квот. "
        "Соберите цель quota_optimizer через CMake перед подготовкой данных."
    )


def solve_strict_quotas(
    lessons: list[dict],
    groups: dict[int, dict],
    teachers: dict[int, dict],
    teacher_unavailable: dict[int, set[date]],
    maximum: dict[int, int],
    original: dict[int, int],
    targets: dict[tuple[int, int], int],
    teacher_capacity: dict[int, int],
    teacher_period_target: dict[int, int],
    teacher_period_floor: dict[int, int],
    non_relaxable_teachers: set[int],
    theory_for: dict[tuple[int, int], list[int]],
    minimum_occurrences: dict[int, int] | None = None,
    fixed_assignments: list[dict] | None = None,
    preferred_assignments: list[dict] | None = None,
    prior_theory: dict[tuple[int, int], int] | None = None,
    max_student_pairs_per_day: int = 4,
    part_target_ranges: dict[tuple[int, int], tuple[int, int]] | None = None,
    hard_no_student_windows: bool = True,
    rooms: list[dict] | None = None,
) -> tuple[dict[int, int], dict, dict[int, int], list[dict]]:
    """Select all short-period quotas in one exact integer model.

    The timetable solver cannot repair an already under-filled quota snapshot.
    Therefore cohort totals, teacher pace and the LPZ prerequisite are solved
    together here instead of being approximated by a greedy pass plus swaps.
    """
    lesson_by_id = {int(lesson["id"]): lesson for lesson in lessons}
    minimum_occurrences = minimum_occurrences or {}
    fixed_assignments = fixed_assignments or []
    preferred_assignments = preferred_assignments or []
    prior_theory = prior_theory or {}
    part_target_ranges = part_target_ranges or {}
    rooms = rooms or []
    part_lessons: dict[tuple[int, int], list[int]] = defaultdict(list)
    labs_for: dict[tuple[int, int], list[int]] = defaultdict(list)
    for lesson in lessons:
        lesson_id = int(lesson["id"])
        for part in lesson_parts(lesson, groups):
            part_lessons[part].append(lesson_id)
        if lesson.get("is_lab", False):
            labs_for[(int(lesson.get("group", -1)), int(lesson.get("subject_id", -1)))].append(lesson_id)

    teacher_ids = sorted({int(lesson.get("teacher", -1)) for lesson in lessons})
    period_dates = dates()

    def lesson_allowed_slots(lesson: dict) -> list[int]:
        group = groups.get(int(lesson.get("group", -1)))
        teacher_id = int(lesson.get("teacher", -1))
        teacher = teachers.get(teacher_id)
        return [
            day_index * 7 + pair - 1
            for day_index, current in enumerate(period_dates)
            for pair in range(1, 8)
            if current not in teacher_unavailable.get(teacher_id, set())
            and rule_allows(group, current, pair)
            and rule_allows(teacher, current, pair)
        ]

    def lesson_allowed_campuses(lesson: dict) -> list[int]:
        teacher = teachers.get(int(lesson.get("teacher", -1)), {})
        lesson_values = {int(value) for value in lesson.get("allowed_campuses", [])} or {0, 1}
        teacher_values = {int(value) for value in teacher.get("allowed_campuses", [])} or {0, 1}
        return sorted(lesson_values & teacher_values)

    model = {
        "variables": [
            {
                "id": lesson_id,
                "minimum": minimum_occurrences.get(lesson_id, 0),
                "maximum": maximum[lesson_id],
                "semester_total": original[lesson_id],
                "teacher": int(lesson_by_id[lesson_id].get("teacher", -1)),
                "group": int(lesson_by_id[lesson_id].get("group", -1)),
                "parts": [
                    group_id * 2 + part
                    for group_id, part in lesson_parts(lesson_by_id[lesson_id], groups)
                ],
                "subject": str(lesson_by_id[lesson_id].get("subject_id", -1)),
                "whole_group": int(lesson_by_id[lesson_id].get("subgroup", -1)) == -1,
                "allowed_slots": lesson_allowed_slots(lesson_by_id[lesson_id]),
                "allowed_campuses": lesson_allowed_campuses(lesson_by_id[lesson_id]),
                # A whole-group occurrence advances both physical streams and
                # should therefore carry twice the fairness weight.
                "part_weight": max(1, len(lesson_parts(lesson_by_id[lesson_id], groups))),
                "sports_room": lesson_by_id[lesson_id].get("required_room_purpose") == "sports_hall",
            }
            for lesson_id in sorted(lesson_by_id)
        ],
        "parts": [
            ({
                "group": group_id,
                "part": part,
                "key": group_id * 2 + part,
                "lesson_ids": sorted(part_lessons[(group_id, part)]),
            } | ({
                "minimum_target": part_target_ranges[(group_id, part)][0],
                "maximum_target": part_target_ranges[(group_id, part)][1],
            } if (group_id, part) in part_target_ranges else {"target": target}))
            for (group_id, part), target in sorted(targets.items())
        ],
        "teachers": [
            {
                "id": teacher_id,
                "minimum": teacher_period_target.get(teacher_id, 0),
                "hard_minimum": max(
                    teacher_period_floor.get(teacher_id, 0),
                    teacher_period_target.get(teacher_id, 0)
                    if teacher_id in non_relaxable_teachers else 0,
                ),
                "maximum": teacher_capacity.get(teacher_id, 0),
                "max_work_days": int(teachers.get(teacher_id, {}).get("max_work_days_per_week", 0)),
            }
            for teacher_id in teacher_ids
        ],
        "lab_rules": [
            {
                "group": group_id,
                "subject": subject_id,
                "theory_ids": sorted(theory_for.get((group_id, subject_id), [])),
                "lab_ids": sorted(lab_ids),
                "prior_theory": prior_theory.get((group_id, subject_id), 0),
            }
            for (group_id, subject_id), lab_ids in sorted(labs_for.items())
            # Match the timetable model: an LPZ-only curriculum component has
            # no separate theory row and is therefore schedulable as-is.
            if theory_for.get((group_id, subject_id))
        ],
        "workers": 4,
        "time_limit_seconds": 30,
        "random_seed": 37,
        "day_count": len(period_dates),
        "slots_per_day": 7,
        "min_student_pairs_per_day": MIN_PAIRS_PER_STUDY_DAY,
        "max_student_pairs_per_day": max_student_pairs_per_day,
        "hard_no_student_windows": hard_no_student_windows,
        "whole_group_same_subject_limit": 2,
        "physical_part_same_subject_limit": 3,
        "fixed": [
            {
                "lesson_id": int(item["lesson_id"]),
                "time": int(item.get("time", item.get("slot", 0))),
            }
            for item in fixed_assignments
        ],
        "preferred": [
            {
                "lesson_id": int(item["lesson_id"]),
                "time": int(item.get("time", item.get("slot", 0))),
            }
            for item in preferred_assignments
        ],
        "maximize_part_load": bool(part_target_ranges),
        "room_capacity_by_campus": [
            sum(
                1 for room in rooms
                if room.get("active", True)
                and room.get("access_mode", "general") not in ("blocked", "exclusive")
                and int(room.get("campus", -1)) == campus
                and room.get("purpose", "") != "sports_hall"
            )
            for campus in (0, 1)
        ],
        "sports_capacity_by_campus": [
            sum(
                1 for room in rooms
                if room.get("active", True)
                and room.get("access_mode", "general") != "blocked"
                and int(room.get("campus", -1)) == campus
                and room.get("purpose", "") == "sports_hall"
            )
            for campus in (0, 1)
        ],
    }

    def run_model(payload: dict) -> tuple[subprocess.CompletedProcess[str], dict]:
        debug_dir = __import__("os").environ.get("RASPIS_QUOTA_MODEL_DIR")
        if debug_dir:
            debug_path = Path(debug_dir)
            debug_path.mkdir(parents=True, exist_ok=True)
            (debug_path / "quota_model.json").write_text(
                json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8"
            )
        with tempfile.TemporaryDirectory(prefix="raspis_quota_") as temp_dir:
            model_path = Path(temp_dir) / "quota_model.json"
            model_path.write_text(json.dumps(payload, ensure_ascii=False), encoding="utf-8")
            completed = subprocess.run(
                [str(quota_optimizer_path()), str(model_path)],
                cwd=ROOT,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=45,
                check=False,
            )
        try:
            return completed, json.loads(completed.stdout.lstrip("\ufeff"))
        except json.JSONDecodeError as error:
            raise RuntimeError(
                "Строгий оптимизатор квот вернул некорректный ответ: "
                + (completed.stderr.strip() or completed.stdout.strip() or str(error))
            ) from error

    completed, report = run_model(model)
    enforced_targets = dict(teacher_period_target)
    target_relaxations: list[dict] = []
    if completed.returncode != 0 or not report.get("success", False):
        diagnostic_model = copy.deepcopy(model)
        diagnostic_model["allow_teacher_shortfalls"] = True
        _, diagnostic = run_model(diagnostic_model)
        deficits = diagnostic.get("teacher_shortfalls", {}) if diagnostic.get("success") else {}
        unsafe = {}
        for raw_teacher_id, raw_deficit in deficits.items():
            teacher_id = int(raw_teacher_id)
            deficit = int(raw_deficit)
            desired = teacher_period_target.get(teacher_id, 0)
            relaxed = max(0, desired - deficit)
            floor = teacher_period_floor.get(teacher_id, 0)
            if teacher_id in non_relaxable_teachers or relaxed < floor:
                unsafe[str(teacher_id)] = {
                    "desired": desired,
                    "minimum_safe": floor,
                    "diagnostic_result": relaxed,
                }
                continue
            enforced_targets[teacher_id] = relaxed
            target_relaxations.append({
                "teacher": teacher_id,
                "desired_pairs": desired,
                "minimum_pairs": relaxed,
                "minimum_safe_for_deadline": floor,
                "reason": "short_period_curriculum_balance",
            })
        if not diagnostic.get("success") or not deficits or unsafe:
            raise RuntimeError(
                "Система недельных квот несовместима даже после диагностики: "
                f"{report.get('status', 'UNKNOWN')}; "
                "небезопасные дефициты: "
                + json.dumps(unsafe or deficits, ensure_ascii=False, sort_keys=True)
            )

        relaxed_model = copy.deepcopy(model)
        for teacher in relaxed_model["teachers"]:
            teacher_id = int(teacher["id"])
            teacher["minimum"] = enforced_targets.get(teacher_id, 0)
        completed, report = run_model(relaxed_model)
        if completed.returncode != 0 or not report.get("success", False):
            raise RuntimeError(
                "Диагностически допустимое ослабление целей не дало точных квот: "
                f"{report.get('status', 'UNKNOWN')}"
            )
        report["desired_teacher_targets"] = {
            str(key): value for key, value in sorted(teacher_period_target.items())
        }
        report["target_relaxations"] = target_relaxations
    quota = {int(lesson_id): int(value) for lesson_id, value in report["quotas"].items()}
    return quota, report, enforced_targets, target_relaxations


def allocate(source: dict) -> dict:
    teachers = {int(x["id"]): x for x in source.get("teachers", [])}
    priority_teacher_ids = {
        teacher_id for teacher_id, teacher in teachers.items()
        if teacher.get("name") in PRIORITY_TEACHER_NAMES
    }
    groups = {int(x["id"]): x for x in source.get("groups", [])}
    excluded = [x for x in source.get("lessons", []) if x.get("is_block") or x.get("is_pp")]
    lessons = [
        x for x in source.get("lessons", [])
        if x.get("plan_active", True)
        and x.get("generation_active", True)
        and x not in excluded
        and int(x.get("teacher", -1)) >= 0
    ]
    unavailable = unavailable_dates(source)

    teacher_capacity = {
        teacher_id: teacher_capacity_between(
            teacher, unavailable[teacher_id], START, END
        )
        for teacher_id, teacher in teachers.items()
    }
    teacher_future_capacity = {
        teacher_id: teacher_capacity_between(
            teacher, unavailable[teacher_id], END + timedelta(days=1), SEMESTER_END
        )
        for teacher_id, teacher in teachers.items()
    }

    original = {int(x["id"]): int(x.get("total_slots", 0)) for x in lessons}
    semester_teacher_load: dict[int, int] = defaultdict(int)
    for lesson in lessons:
        semester_teacher_load[int(lesson.get("teacher", -1))] += original[int(lesson["id"])]
    teacher_period_target: dict[int, int] = {}
    teacher_period_floor: dict[int, int] = {}
    for teacher_id, semester_pairs in semester_teacher_load.items():
        current_capacity = teacher_capacity.get(teacher_id, 0)
        future_capacity = teacher_future_capacity.get(teacher_id, 0)
        full_capacity = current_capacity + future_capacity
        weighted_target = (
            math.ceil(semester_pairs * current_capacity / full_capacity)
            if full_capacity > 0 else 0
        )
        teacher_period_target[teacher_id] = min(current_capacity, weighted_target)
        # This is the minimum that must be read now to keep the remaining
        # regular load within the calendar capacity through 26 December.
        teacher_period_floor[teacher_id] = min(
            current_capacity,
            max(0, semester_pairs - future_capacity),
        )
    # Эти преподаватели имеют почти предельную семестровую нагрузку. На
    # короткой неделе используем всю подтверждённую доступность, иначе даже
    # одна потерянная пара переносит дефицит на уже перегруженные недели.
    for teacher_id in priority_teacher_ids:
        teacher_period_target[teacher_id] = teacher_capacity.get(teacher_id, 0)
    maximum = {}
    for lesson in lessons:
        lesson_id = int(lesson["id"])
        teacher_id = int(lesson.get("teacher", -1))
        group_id = int(lesson.get("group", -1))
        available_dates = {
            current for current in dates()
            if any(
                rule_allows(groups.get(group_id), current, pair)
                and (
                    teacher_id < 0
                    or (
                        current not in unavailable[teacher_id]
                        and rule_allows(teachers.get(teacher_id), current, pair)
                    )
                )
                for pair in range(1, 8)
            )
        }
        placements = sum(
            1 for current in dates() for pair in range(1, 8)
            if rule_allows(groups.get(group_id), current, pair)
            and (teacher_id < 0 or (current not in unavailable[teacher_id] and rule_allows(teachers.get(teacher_id), current, pair)))
        )
        # The normal semester share is preferred by scoring below, but it must
        # not prevent filling every day of this short presentation week.
        # A physical subgroup may legitimately have three occurrences of the
        # same subject in one day (for example one common lecture followed by
        # two subgroup lessons).  Do not make the quota pre-selector stricter
        # than the timetable model's configured per-subject ceiling.
        maximum[lesson_id] = min(original[lesson_id], placements, len(available_dates) * 3)

    def parts(lesson: dict) -> list[tuple[int, int]]:
        return lesson_parts(lesson, groups)

    target_float: dict[tuple[int, int], float] = defaultdict(float)
    for lesson in lessons:
        value = original[int(lesson["id"])] / SEMESTER_DISTRIBUTION_WEEKS
        for key in parts(lesson):
            target_float[key] += value
    required_week_pairs = len(dates()) * MIN_PAIRS_PER_STUDY_DAY
    targets = {
        # Девять пар на три дня дают ровный ритм 3+3+3 для каждой реальной
        # подгруппы. Параллельные подгруппы учитываются независимо.
        key: max(required_week_pairs, TARGET_PAIRS_PER_WEEK) if value > 0 else 0
        for key, value in target_float.items()
    }

    quota = {int(x["id"]): 0 for x in lessons}
    group_load: dict[tuple[int, int], int] = defaultdict(int)
    teacher_load: dict[int, int] = defaultdict(int)
    theory_for = defaultdict(list)
    for lesson in lessons:
        if not lesson.get("is_lab", False):
            theory_for[(int(lesson.get("group", -1)), int(lesson.get("subject_id", -1)))].append(int(lesson["id"]))

    teacher_period_desired = dict(teacher_period_target)
    quota, quota_optimization, teacher_period_target, teacher_target_relaxations = solve_strict_quotas(
        lessons=lessons,
        groups=groups,
        teachers=teachers,
        teacher_unavailable=unavailable,
        maximum=maximum,
        original=original,
        targets=targets,
        teacher_capacity=teacher_capacity,
        teacher_period_target=teacher_period_target,
        teacher_period_floor=teacher_period_floor,
        non_relaxable_teachers=priority_teacher_ids,
        theory_for=theory_for,
        rooms=source.get("rooms", []),
    )
    group_load: dict[tuple[int, int], int] = defaultdict(int)
    teacher_load: dict[int, int] = defaultdict(int)
    for lesson in lessons:
        lesson_id = int(lesson["id"])
        value = quota.get(lesson_id, 0)
        if not 0 <= value <= maximum[lesson_id]:
            raise RuntimeError(f"Оптимизатор нарушил границы квоты занятия {lesson_id}")
        for key in parts(lesson):
            group_load[key] += value
        teacher_load[int(lesson.get("teacher", -1))] += value

    shortfalls = {
        f"{group_id}:{part}": target - group_load[(group_id, part)]
        for (group_id, part), target in targets.items()
        if group_load[(group_id, part)] != target
    }
    teacher_target_shortfalls = {
        str(teacher_id): target - teacher_load.get(teacher_id, 0)
        for teacher_id, target in teacher_period_target.items()
        if teacher_load.get(teacher_id, 0) < target
    }
    if shortfalls or teacher_target_shortfalls:
        raise RuntimeError(
            "Внутренняя проверка строгих квот не пройдена: "
            + json.dumps(
                {"parts": shortfalls, "teachers": teacher_target_shortfalls},
                ensure_ascii=False,
            )
        )
    for subject_key, theory_ids in theory_for.items():
        theory_count = sum(quota.get(lesson_id, 0) for lesson_id in theory_ids)
        has_lab = any(
            lesson.get("is_lab", False)
            and (int(lesson.get("group", -1)), int(lesson.get("subject_id", -1))) == subject_key
            and quota.get(int(lesson["id"]), 0) > 0
            for lesson in lessons
        )
        if has_lab and theory_count < 2:
            raise RuntimeError(f"LPZ выбрана без двух теоретических пар: {subject_key}")

    # Exact selection is the authoritative result.  The legacy greedy/repair
    # implementation remains below temporarily for historical comparison, but
    # is deliberately unreachable and cannot silently weaken these invariants.
    return {
        "quota": quota,
        "targets": targets,
        "teacher_capacity": teacher_capacity,
        "teacher_load": teacher_load,
        "teacher_period_target": teacher_period_target,
        "teacher_target_shortfalls": teacher_target_shortfalls,
        "teacher_target_report": [
            {
                "teacher": teacher_id,
                "name": teachers[teacher_id]["name"],
                "minimum_pairs": target,
                "desired_pairs": teacher_period_desired.get(teacher_id, target),
                "minimum_safe_for_deadline": teacher_period_floor.get(teacher_id, 0),
                "selected_pairs": teacher_load.get(teacher_id, 0),
                "semester_regular_pairs": semester_teacher_load.get(teacher_id, 0),
                "current_capacity": teacher_capacity.get(teacher_id, 0),
                "future_capacity": teacher_future_capacity.get(teacher_id, 0),
            }
            for teacher_id, target in sorted(teacher_period_target.items())
            if target > 0 or teacher_period_desired.get(teacher_id, 0) > 0
        ],
        "priority_teacher_load": {
            teachers[teacher_id]["name"]: teacher_load.get(teacher_id, 0)
            for teacher_id in sorted(priority_teacher_ids)
        },
        "deferred_labs": 0,
        "campus_repairs": [],
        "quota_rebalances": [],
        "teacher_pace_repairs": [],
        "shortfalls": shortfalls,
        "quota_optimization": quota_optimization,
        "teacher_target_relaxations": teacher_target_relaxations,
    }


def prepare() -> None:
    if STATE.exists():
        raise SystemExit(f"Restore existing state first: {STATE}")
    source = read_json(DATA)
    HISTORY.mkdir(parents=True, exist_ok=True)
    backup = HISTORY / f"before_one_week_generation_{datetime.now():%Y%m%d-%H%M%S}.json"
    shutil.copy2(DATA, backup)
    allocation = allocate(source)
    if allocation["shortfalls"]:
        raise SystemExit(
            "Невозможно обеспечить минимальную нагрузку во все дни периода: "
            + json.dumps(allocation["shortfalls"], ensure_ascii=False)
        )
    if allocation["teacher_target_shortfalls"]:
        raise SystemExit(
            "Невозможно обеспечить темп вычитки преподавателей: "
            + json.dumps(allocation["teacher_target_shortfalls"], ensure_ascii=False)
        )
    active = starts = 0
    excluded = {"UP": 0, "PP": 0, "vacancy": 0, "zero_quota": 0}
    for lesson in source.get("lessons", []):
        if lesson.get("is_block", False):
            lesson["plan_active"] = False
            excluded["UP"] += 1
            continue
        if lesson.get("is_pp", False):
            lesson["plan_active"] = False
            excluded["PP"] += 1
            continue
        if int(lesson.get("teacher", -1)) < 0:
            lesson["plan_active"] = False
            excluded["vacancy"] += 1
            continue
        quota = allocation["quota"].get(int(lesson["id"]), 0)
        if quota <= 0:
            lesson["plan_active"] = False
            excluded["zero_quota"] += 1
            continue
        lesson["plan_active"] = True
        lesson["total_slots"] = quota
        lesson["week_parity"] = "all"
        active += 1
        starts += quota

    settings = source.setdefault("settings", {})
    settings["start_date"] = START.isoformat()
    settings["end_date"] = END.isoformat()
    settings["teacher_period_targets"] = allocation["teacher_target_report"]
    settings.setdefault("solver_config", {}).update({
        "solver_time_limit_seconds": 150,
        "week_time_limit_seconds": 120,
        "solver_workers": 4,
        "stop_after_first_solution": False,
        "quality_improvement_seconds": 120,
        "hard_no_student_windows": True,
        "hard_no_teacher_windows": False,
        "hard_min_study_days_per_week": True,
        # Cohorts with 8-11 real selected pairs necessarily have several
        # two-pair days; keep the 2-pair daily minimum, but do not make the
        # historical "only one two-pair day" preference a hard restriction.
        "hard_max_one_two_pair_student_day": False,
        "hard_max_two_same_subject_per_day": True,
        "max_same_subject_pairs_per_day": 3,
        "max_whole_group_same_subject_pairs_per_day": 2,
        "use_quality_objective": True,
        "optimize_teacher_windows": True,
        "optimize_student_windows": False,
        "max_student_pairs_per_day": 4,
        "min_student_pairs_per_study_day": MIN_PAIRS_PER_STUDY_DAY,
        "min_student_study_days_per_week": len(dates()),
        "student_five_pair_day_weight": 100000,
        "student_late_slot_weight": 20,
        "teacher_window_weight": 12000,
        "teacher_campus_preference_weight": 1500,
        "student_window_weight": 1000,
        "min_subject_spread_total_slots": 9999,
        "random_seed": 37,
    })
    source.setdefault("meta", {})["temporary_generation"] = {
        "kind": "one_week_semester_share",
        "semester_weeks": SEMESTER_DISTRIBUTION_WEEKS,
        "date_from": START.isoformat(),
        "date_to": END.isoformat(),
        "semester_end": SEMESTER_END.isoformat(),
        "excluded": ["УП", "ПП"],
        "lpz_requires_prior_theory_when_component_exists": True,
        "all_available_days_required": True,
        "minimum_pairs_per_day": MIN_PAIRS_PER_STUDY_DAY,
        "quota_rebalances": allocation["quota_rebalances"],
        "teacher_target_relaxations": allocation["teacher_target_relaxations"],
        "quota_optimization": allocation["quota_optimization"],
        "backup": str(backup),
    }
    state = {
        "backup": str(backup), "active_lessons": active, "starts": starts,
        "excluded": excluded, "deferred_labs": allocation["deferred_labs"],
        "campus_repairs": allocation["campus_repairs"],
        "quota_rebalances": allocation["quota_rebalances"],
        "teacher_target_relaxations": allocation["teacher_target_relaxations"],
    }
    write_json(DATA, source)
    write_json(STATE, state)
    print(json.dumps(state, ensure_ascii=False, indent=2))


def refresh() -> None:
    """Recalculate the short week while preserving current teachers and rooms."""
    if not STATE.exists():
        raise SystemExit(f"No one-week state found: {STATE}")
    state = read_json(STATE)
    current = read_json(DATA)
    semester_path = Path(state["backup"])
    if not semester_path.exists():
        # HTTP version history keeps the payload inside `data` and may rotate
        # an older plain backup. Prefer the immutable packaged semester source;
        # fall back to matching HTTP snapshots only when it is unavailable.
        candidates = []
        source_paths = [
            ROOT / "deploy" / "release-20260831-week-0902" / "data" / "timetable_data.json",
            ROOT / ".codex-temp" / "visual-transfer-site" / "data" / "timetable_data.json",
            *HISTORY.glob("version_*.json"),
        ]
        for path in source_paths:
            if not path.exists():
                continue
            payload = read_json(path)
            snapshot = payload.get("data", payload)
            lessons = snapshot.get("lessons", [])
            if len(lessons) != len(current.get("lessons", [])):
                continue
            total = sum(semester_total_slots(x) for x in lessons)
            candidates.append((total, path, snapshot))
        if not candidates:
            raise SystemExit(f"Semester source backup was rotated and no version matches: {semester_path}")
        _, semester_path, semester = max(candidates, key=lambda item: item[0])
    else:
        payload = read_json(semester_path)
        semester = payload.get("data", payload)
    working = copy.deepcopy(current)

    semester_by_uid = {x.get("uid"): x for x in semester.get("lessons", []) if x.get("uid")}
    semester_by_id = {int(x.get("id", -1)): x for x in semester.get("lessons", [])}
    restored = 0
    for lesson in working.get("lessons", []):
        original = semester_by_uid.get(lesson.get("uid")) or semester_by_id.get(int(lesson.get("id", -1)))
        if not original:
            continue
        lesson["total_hours"] = int(original.get("total_hours", lesson.get("total_hours", 0)))
        lesson["total_slots"] = semester_total_slots(lesson)
        lesson["plan_active"] = (
            bool(lesson.get("generation_active", True))
            and int(lesson.get("teacher", -1)) >= 0
            and lesson["total_slots"] > 0
        )
        restored += 1

    allocation = allocate(working)
    # Exact quota selection is fail-closed.  The fallback check remains as a
    # second independent assertion that every physical subgroup has enough
    # work for all three study days.
    blocking_shortfalls = {
        key: missing for key, missing in allocation["shortfalls"].items()
        if allocation["targets"][tuple(map(int, key.split(":")))] - missing
        < len(dates()) * MIN_PAIRS_PER_STUDY_DAY
    }
    if blocking_shortfalls:
        raise SystemExit(
            "Невозможно набрать целевую недельную нагрузку: "
            + json.dumps(blocking_shortfalls, ensure_ascii=False)
        )
    if allocation["teacher_target_shortfalls"]:
        raise SystemExit(
            "Невозможно обеспечить темп вычитки преподавателей: "
            + json.dumps(allocation["teacher_target_shortfalls"], ensure_ascii=False)
        )

    HISTORY.mkdir(parents=True, exist_ok=True)
    backup = HISTORY / f"before_refresh_one_week_{datetime.now():%Y%m%d-%H%M%S}.json"
    shutil.copy2(DATA, backup)
    active = starts = 0
    excluded = {"UP": 0, "PP": 0, "vacancy": 0, "zero_quota": 0}
    for lesson in current.get("lessons", []):
        if lesson.get("is_block", False):
            lesson["plan_active"] = False
            excluded["UP"] += 1
            continue
        if lesson.get("is_pp", False):
            lesson["plan_active"] = False
            excluded["PP"] += 1
            continue
        if int(lesson.get("teacher", -1)) < 0:
            lesson["plan_active"] = False
            excluded["vacancy"] += 1
            continue
        quota = allocation["quota"].get(int(lesson["id"]), 0)
        if quota <= 0:
            lesson["plan_active"] = False
            excluded["zero_quota"] += 1
            continue
        lesson["plan_active"] = True
        lesson["total_slots"] = quota
        lesson["week_parity"] = "all"
        active += 1
        starts += quota

    settings = current.setdefault("settings", {})
    settings["start_date"] = START.isoformat()
    settings["end_date"] = END.isoformat()
    settings["teacher_period_targets"] = allocation["teacher_target_report"]
    settings.setdefault("solver_config", {}).update({
        "solver_time_limit_seconds": 180,
        "week_time_limit_seconds": 140,
        "solver_workers": 4,
        "stop_after_first_solution": False,
        "quality_improvement_seconds": 120,
        "hard_no_student_windows": True,
        "hard_no_teacher_windows": False,
        "hard_min_study_days_per_week": True,
        "hard_max_one_two_pair_student_day": False,
        "hard_max_two_same_subject_per_day": True,
        "max_same_subject_pairs_per_day": 3,
        "max_whole_group_same_subject_pairs_per_day": 2,
        "use_quality_objective": True,
        "optimize_teacher_windows": True,
        "optimize_student_windows": False,
        "max_student_pairs_per_day": 4,
        "min_student_pairs_per_study_day": MIN_PAIRS_PER_STUDY_DAY,
        "min_student_study_days_per_week": len(dates()),
        # При максимуме 4 этот исторически названный параметр штрафует именно
        # четыре пары, поэтому отключаем его: четыре лучше двух.
        "student_five_pair_day_weight": 0,
        "student_two_pair_day_weight": 4000,
        # Мягкий штраф за поздний номер пары. Он сдвигает непрерывный блок
        # группы к утру, но никогда не делает вечерние пары запрещёнными.
        "student_late_slot_weight": 20,
        "teacher_window_weight": 12000,
        "teacher_campus_preference_weight": 1500,
        "student_window_weight": 1000,
        "min_subject_spread_total_slots": 9999,
        "min_initial_theory_slots_before_labs": 2,
        "random_seed": 37,
    })
    current.setdefault("meta", {})["temporary_generation"] = {
        "kind": "one_week_high_readout",
        "semester_weeks": SEMESTER_DISTRIBUTION_WEEKS,
        "date_from": START.isoformat(),
        "date_to": END.isoformat(),
        "semester_end": SEMESTER_END.isoformat(),
        "excluded": ["УП", "ПП"],
        "lpz_requires_prior_theory_pairs_when_component_exists": 2,
        "all_available_days_required": True,
        "minimum_pairs_per_day": MIN_PAIRS_PER_STUDY_DAY,
        "target_pairs_per_week": TARGET_PAIRS_PER_WEEK,
        "teacher_period_targets": allocation["teacher_target_report"],
        "quota_rebalances": allocation["quota_rebalances"],
        "teacher_target_relaxations": allocation["teacher_target_relaxations"],
        "quota_optimization": allocation["quota_optimization"],
        "semester_source": str(semester_path),
        "backup": str(backup),
    }
    write_json(DATA, current)
    state.update({
        "last_refresh_backup": str(backup), "active_lessons": active,
        "starts": starts, "excluded": excluded,
        "deferred_labs": allocation["deferred_labs"], "restored_lessons": restored,
        "campus_repairs": allocation["campus_repairs"],
        "quota_rebalances": allocation["quota_rebalances"],
        "teacher_target_relaxations": allocation["teacher_target_relaxations"],
        "target_pairs_per_week": TARGET_PAIRS_PER_WEEK,
    })
    write_json(STATE, state)
    print(json.dumps(state, ensure_ascii=False, indent=2))


def restore() -> None:
    state = read_json(STATE)
    backup = Path(state["backup"])
    shutil.copy2(backup, DATA)
    STATE.unlink()
    print(json.dumps({"restored": str(backup)}, ensure_ascii=False, indent=2))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("prepare", "refresh", "restore"))
    args = parser.parse_args()
    if args.action == "prepare":
        prepare()
    elif args.action == "refresh":
        refresh()
    else:
        restore()


if __name__ == "__main__":
    main()
