#!/usr/bin/env python3
"""Fail-closed audit of teacher readout pace through the semester deadline.

The audit intentionally keeps regular lessons, educational practice (UP), and
industrial practice (PP) separate.  The currently generated timetable is used
only for the regular-lesson pace gate; UP/PP require their own calendars.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from collections import defaultdict
from datetime import date, timedelta
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DATA = ROOT / "data" / "timetable_data.json"
DEFAULT_SCHEDULE = ROOT / "output" / "latest" / "schedule_all.json"
DEFAULT_REPORT = ROOT / "output" / "latest" / "semester_readout_report.json"
SEMESTER_DEADLINE = date(2026, 12, 26)
SLOTS_PER_DAY = 7


def parse_iso(value: Any, field: str) -> date:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{field}: expected a non-empty ISO date")
    try:
        return date.fromisoformat(value)
    except ValueError as exc:
        raise ValueError(f"{field}: invalid ISO date {value!r}") from exc


def optional_iso(value: Any) -> date | None:
    if not isinstance(value, str) or not value.strip():
        return None
    try:
        return date.fromisoformat(value)
    except ValueError:
        return None


def integer(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return default
    if isinstance(value, (int, float)) and math.isfinite(float(value)):
        return int(round(float(value)))
    return default


def lesson_category(lesson: dict[str, Any]) -> str:
    if bool(lesson.get("is_pp", False)):
        return "pp"
    if bool(lesson.get("is_block", False)):
        return "up"
    return "regular"


def semester_occurrences(lesson: dict[str, Any]) -> int:
    """Convert academic hours to independently auditable occurrences."""
    hours = max(0, integer(lesson.get("total_hours"), 0))
    divisor = 6 if lesson_category(lesson) == "up" else 2
    return math.ceil(hours / divisor) if hours else 0


def iter_days(start: date, end: date):
    current = start
    while current <= end:
        # The solver's GenerateSchoolDays excludes Sunday.
        if current.isoweekday() != 7:
            yield current
        current += timedelta(days=1)


def unavailable_dates(
    entries: list[dict[str, Any]], teacher_id: int, start: date, end: date
) -> set[date]:
    result: set[date] = set()
    for entry in entries:
        if integer(entry.get("teacher"), -1) != teacher_id:
            continue
        raw_dates = entry.get("dates", [])
        if isinstance(raw_dates, list):
            for raw in raw_dates:
                parsed = optional_iso(raw)
                if parsed is not None and start <= parsed <= end:
                    result.add(parsed)
        range_from = optional_iso(entry.get("from"))
        range_to = optional_iso(entry.get("to"))
        if range_from is None or range_to is None or range_to < range_from:
            continue
        cursor = max(start, range_from)
        limit = min(end, range_to)
        while cursor <= limit:
            result.add(cursor)
            cursor += timedelta(days=1)
    return result


def work_period(teacher: dict[str, Any]) -> tuple[date, date] | None:
    raw = teacher.get("work_period", {})
    if not isinstance(raw, dict):
        return None
    start = optional_iso(raw.get("from"))
    end = optional_iso(raw.get("to"))
    # Match the solver: an incomplete/invalid work period does not constrain it.
    if start is None or end is None or end < start:
        return None
    return start, end


def normal_day_slots(teacher: dict[str, Any], weekday: int) -> set[int]:
    raw_days = teacher.get("work_days")
    if not isinstance(raw_days, list):
        return set(range(1, SLOTS_PER_DAY + 1)) if weekday != 7 else set()

    selected_day: dict[str, Any] | None = None
    for raw_day in raw_days:
        if isinstance(raw_day, dict) and integer(raw_day.get("day"), 0) == weekday:
            selected_day = raw_day
            break
    if selected_day is None:
        return set(range(1, SLOTS_PER_DAY + 1)) if weekday != 7 else set()
    if not bool(selected_day.get("enabled", weekday != 7)):
        return set()

    raw_slots = selected_day.get("slots")
    if isinstance(raw_slots, list):
        return {
            slot
            for raw in raw_slots
            if 1 <= (slot := integer(raw, -1)) <= SLOTS_PER_DAY
        }
    first = min(SLOTS_PER_DAY, max(1, integer(selected_day.get("start_slot"), 1)))
    last = min(
        SLOTS_PER_DAY,
        max(first, integer(selected_day.get("end_slot"), SLOTS_PER_DAY)),
    )
    return set(range(first, last + 1))


def date_overrides(teacher: dict[str, Any]) -> dict[date, set[int]]:
    result: dict[date, set[int]] = {}
    raw_overrides = teacher.get("date_slot_overrides", [])
    if not isinstance(raw_overrides, list):
        return result
    for raw in raw_overrides:
        if not isinstance(raw, dict):
            continue
        override_date = optional_iso(raw.get("date"))
        if override_date is None:
            continue
        raw_slots = raw.get("slots", [])
        result[override_date] = {
            slot
            for value in raw_slots
            if 1 <= (slot := integer(value, -1)) <= SLOTS_PER_DAY
        } if isinstance(raw_slots, list) else set()
    return result


def daily_capacity(
    teacher: dict[str, Any], current: date, unavailable: set[date]
) -> int:
    if current in unavailable or current.isoweekday() == 7:
        return 0
    period = work_period(teacher)
    if period is not None and not (period[0] <= current <= period[1]):
        return 0

    overrides = date_overrides(teacher)
    slots = overrides[current] if current in overrides else normal_day_slots(
        teacher, current.isoweekday()
    )
    capacity = len(slots)
    maximum = integer(teacher.get("max_pairs_per_day"), 0)
    if maximum > 0:
        capacity = min(capacity, maximum, SLOTS_PER_DAY)
    return capacity


def teacher_capacity_between(
    teacher: dict[str, Any],
    teacher_unavailable: list[dict[str, Any]],
    start: date,
    end: date,
    week_anchor: date,
) -> int:
    if end < start:
        return 0
    blocked = unavailable_dates(
        teacher_unavailable, integer(teacher.get("id"), -1), start, end
    )
    # Keep the signature stable for callers, but use real ISO teaching weeks.
    # An arbitrary date anchor can incorrectly put Monday in the preceding
    # Thursday-based bucket and break max_work_days_per_week accounting.
    del week_anchor
    by_week: dict[tuple[int, int], list[int]] = defaultdict(list)
    for current in iter_days(start, end):
        iso = current.isocalendar()
        by_week[(iso.year, iso.week)].append(daily_capacity(teacher, current, blocked))

    maximum_days = integer(teacher.get("max_work_days_per_week"), 0)
    total = 0
    for capacities in by_week.values():
        positive = sorted((value for value in capacities if value > 0), reverse=True)
        if maximum_days > 0:
            positive = positive[:maximum_days]
        total += sum(positive)
    return total


def parse_period_targets(
    settings: dict[str, Any], known_teachers: set[int]
) -> tuple[dict[int, int], list[dict[str, Any]]]:
    targets: dict[int, int] = {}
    issues: list[dict[str, Any]] = []
    raw_targets = settings.get("teacher_period_targets", [])
    if raw_targets is None:
        raw_targets = []
    if not isinstance(raw_targets, list):
        return {}, [{"code": "teacher_period_targets_not_array"}]
    for index, raw in enumerate(raw_targets):
        if not isinstance(raw, dict):
            issues.append({"code": "teacher_period_target_not_object", "index": index})
            continue
        teacher_id = integer(raw.get("teacher", raw.get("teacher_id")), -1)
        target_value = raw.get("minimum_pairs", raw.get("target_pairs"))
        target = integer(target_value, -1)
        if teacher_id not in known_teachers:
            issues.append(
                {
                    "code": "teacher_period_target_unknown_teacher",
                    "index": index,
                    "teacher_id": teacher_id,
                }
            )
            continue
        if target < 0:
            issues.append(
                {
                    "code": "teacher_period_target_invalid_minimum",
                    "index": index,
                    "teacher_id": teacher_id,
                }
            )
            continue
        targets[teacher_id] = max(targets.get(teacher_id, 0), target)
    return targets, issues


def scheduled_teacher_occurrences(
    schedule: dict[str, Any],
    lessons_by_id: dict[int, dict[str, Any]],
    period_start: date,
    period_end: date,
) -> tuple[dict[int, dict[str, int]], dict[str, Any]]:
    """Count unique lesson occurrences; contiguous UP slots count as one start."""
    regular_or_pp: set[tuple[int, date, int]] = set()
    up_slots: dict[tuple[int, date], set[int]] = defaultdict(set)
    seen_raw: set[tuple[int, date, int]] = set()
    duplicate_items = 0
    outside_period = 0
    unknown_lessons: set[int] = set()
    teacher_mismatches: list[dict[str, int]] = []

    groups = schedule.get("groups", [])
    if not isinstance(groups, list):
        raise ValueError("schedule.groups: expected an array")
    for group in groups:
        if not isinstance(group, dict):
            continue
        days = group.get("days", [])
        if not isinstance(days, list):
            continue
        for raw_day in days:
            if not isinstance(raw_day, dict):
                continue
            raw_date = raw_day.get("date_iso")
            if not isinstance(raw_date, str):
                continue
            try:
                current = date.fromisoformat(raw_date)
            except ValueError:
                continue
            slots = raw_day.get("slots", [])
            if not isinstance(slots, list):
                continue
            for raw_slot in slots:
                if not isinstance(raw_slot, dict):
                    continue
                slot = integer(raw_slot.get("slot"), -1)
                if not 1 <= slot <= SLOTS_PER_DAY:
                    continue
                items = raw_slot.get("lessons", [])
                if not isinstance(items, list):
                    continue
                for item in items:
                    if not isinstance(item, dict):
                        continue
                    lesson_id = integer(item.get("id"), -1)
                    lesson = lessons_by_id.get(lesson_id)
                    if lesson is None:
                        unknown_lessons.add(lesson_id)
                        continue
                    raw_key = (lesson_id, current, slot)
                    if raw_key in seen_raw:
                        duplicate_items += 1
                        continue
                    seen_raw.add(raw_key)
                    if not (period_start <= current <= period_end):
                        outside_period += 1
                        continue
                    item_teacher = integer(item.get("teacher_id"), -1)
                    lesson_teacher = integer(lesson.get("teacher"), -1)
                    if item_teacher >= 0 and item_teacher != lesson_teacher:
                        teacher_mismatches.append(
                            {
                                "lesson_id": lesson_id,
                                "schedule_teacher": item_teacher,
                                "data_teacher": lesson_teacher,
                            }
                        )
                    if lesson_category(lesson) == "up":
                        up_slots[(lesson_id, current)].add(slot)
                    else:
                        regular_or_pp.add(raw_key)

    result: dict[int, dict[str, int]] = defaultdict(
        lambda: {"regular": 0, "up": 0, "pp": 0}
    )
    for lesson_id, _current, _slot in regular_or_pp:
        lesson = lessons_by_id[lesson_id]
        teacher_id = integer(lesson.get("teacher"), -1)
        if teacher_id >= 0:
            result[teacher_id][lesson_category(lesson)] += 1
    for (lesson_id, _current), slots in up_slots.items():
        lesson = lessons_by_id[lesson_id]
        teacher_id = integer(lesson.get("teacher"), -1)
        if teacher_id < 0:
            continue
        # Every slot without the immediately preceding slot is one block start.
        starts = sum(1 for slot in slots if slot - 1 not in slots)
        result[teacher_id]["up"] += starts

    integrity = {
        "unique_lesson_slot_items": len(seen_raw),
        "deduplicated_items": duplicate_items,
        "outside_period_items": outside_period,
        "unknown_lesson_ids": sorted(unknown_lessons),
        "teacher_mismatches": teacher_mismatches,
    }
    return dict(result), integrity


def build_report(
    data: dict[str, Any], schedule: dict[str, Any], data_path: Path, schedule_path: Path
) -> dict[str, Any]:
    settings = data.get("settings", {})
    if not isinstance(settings, dict):
        raise ValueError("data.settings: expected an object")
    period_start = parse_iso(settings.get("start_date"), "settings.start_date")
    period_end = parse_iso(settings.get("end_date"), "settings.end_date")
    if period_end < period_start:
        raise ValueError("settings.end_date is earlier than settings.start_date")
    if period_end > SEMESTER_DEADLINE:
        raise ValueError("generation period ends after the semester deadline")

    raw_teachers = data.get("teachers", [])
    raw_lessons = data.get("lessons", [])
    raw_unavailable = data.get("teacher_unavailable", [])
    if not isinstance(raw_teachers, list) or not isinstance(raw_lessons, list):
        raise ValueError("data.teachers and data.lessons must be arrays")
    if not isinstance(raw_unavailable, list):
        raise ValueError("data.teacher_unavailable must be an array")

    teachers_by_id = {
        integer(teacher.get("id"), -1): teacher
        for teacher in raw_teachers
        if isinstance(teacher, dict) and integer(teacher.get("id"), -1) >= 0
    }
    lessons_by_id = {
        integer(lesson.get("id"), -1): lesson
        for lesson in raw_lessons
        if isinstance(lesson, dict) and integer(lesson.get("id"), -1) >= 0
    }
    targets, target_issues = parse_period_targets(settings, set(teachers_by_id))
    desired_targets: dict[int, int] = {}
    for raw in settings.get("teacher_period_targets", []):
        if not isinstance(raw, dict):
            continue
        teacher_id = integer(raw.get("teacher", raw.get("teacher_id")), -1)
        if teacher_id not in teachers_by_id:
            continue
        desired_targets[teacher_id] = max(
            targets.get(teacher_id, 0),
            integer(raw.get("desired_pairs"), targets.get(teacher_id, 0)),
        )
    temporary = data.get("meta", {}).get("temporary_generation", {})
    for raw in temporary.get("teacher_target_relaxations", []):
        if not isinstance(raw, dict):
            continue
        teacher_id = integer(raw.get("teacher"), -1)
        if teacher_id in teachers_by_id:
            desired_targets[teacher_id] = max(
                desired_targets.get(teacher_id, 0),
                integer(raw.get("desired_pairs"), 0),
            )
    actual, schedule_integrity = scheduled_teacher_occurrences(
        schedule, lessons_by_id, period_start, period_end
    )

    planned: dict[int, dict[str, int]] = defaultdict(
        lambda: {"regular": 0, "up": 0, "pp": 0}
    )
    ignored_inactive = {"regular": 0, "up": 0, "pp": 0}
    unassigned = {"regular": 0, "up": 0, "pp": 0}
    for lesson in lessons_by_id.values():
        category = lesson_category(lesson)
        occurrences = semester_occurrences(lesson)
        if not bool(lesson.get("generation_active", True)):
            ignored_inactive[category] += occurrences
            continue
        teacher_id = integer(lesson.get("teacher"), -1)
        if teacher_id not in teachers_by_id:
            unassigned[category] += occurrences
            continue
        planned[teacher_id][category] += occurrences

    included_teacher_ids = sorted(set(planned) | set(actual) | set(targets))
    teacher_rows: list[dict[str, Any]] = []
    target_shortfalls: list[dict[str, Any]] = []
    future_infeasible: list[dict[str, Any]] = []
    full_infeasible: list[dict[str, Any]] = []

    for teacher_id in included_teacher_ids:
        teacher = teachers_by_id[teacher_id]
        loads = planned[teacher_id]
        scheduled = actual.get(teacher_id, {"regular": 0, "up": 0, "pp": 0})
        regular_need = loads["regular"]
        full_capacity = teacher_capacity_between(
            teacher,
            raw_unavailable,
            period_start,
            SEMESTER_DEADLINE,
            period_start,
        )
        current_capacity = teacher_capacity_between(
            teacher, raw_unavailable, period_start, period_end, period_start
        )
        future_start = period_end + timedelta(days=1)
        future_capacity = teacher_capacity_between(
            teacher,
            raw_unavailable,
            future_start,
            SEMESTER_DEADLINE,
            period_start,
        )
        must_now = max(0, regular_need - future_capacity)
        configured_target = targets.get(teacher_id)
        desired_target = desired_targets.get(teacher_id, configured_target or 0)
        # The configured target is the enforceable gate for this generated
        # period.  ``must_now`` remains a separate deadline diagnostic: when
        # the semester is already physically impossible, folding it into the
        # period target would falsely claim that an exactly generated week
        # missed its own declared target.
        required_target = configured_target or 0
        actual_regular = scheduled["regular"]
        target_gap = max(0, required_target - actual_regular)
        desired_gap = max(0, desired_target - actual_regular)
        remaining = max(0, regular_need - actual_regular)
        future_gap = max(0, remaining - future_capacity)
        full_gap = max(0, regular_need - full_capacity)

        flags: list[str] = []
        if full_gap:
            flags.append("full_semester_physically_infeasible")
            full_infeasible.append(
                {
                    "teacher_id": teacher_id,
                    "teacher": teacher.get("name", f"#{teacher_id}"),
                    "gap_pairs": full_gap,
                }
            )
        if target_gap:
            flags.append("period_target_shortfall")
            target_shortfalls.append(
                {
                    "teacher_id": teacher_id,
                    "teacher": teacher.get("name", f"#{teacher_id}"),
                    "target_pairs": required_target,
                    "actual_pairs": actual_regular,
                    "gap_pairs": target_gap,
                }
            )
        if future_gap:
            flags.append("future_readout_infeasible_after_period")
            future_infeasible.append(
                {
                    "teacher_id": teacher_id,
                    "teacher": teacher.get("name", f"#{teacher_id}"),
                    "remaining_pairs": remaining,
                    "future_capacity_pairs": future_capacity,
                    "gap_pairs": future_gap,
                }
            )

        teacher_rows.append(
            {
                "teacher_id": teacher_id,
                "teacher": teacher.get("name", f"#{teacher_id}"),
                "semester_need_occurrences": {
                    "regular": regular_need,
                    "up": loads["up"],
                    "pp": loads["pp"],
                },
                "scheduled_period_occurrences": {
                    "regular": actual_regular,
                    "up_block_starts": scheduled["up"],
                    "pp": scheduled["pp"],
                },
                "capacity_pairs": {
                    "full_from_period_start": full_capacity,
                    "current_period": current_capacity,
                    "future_after_period": future_capacity,
                },
                "must_now_pairs": must_now,
                "configured_period_target_pairs": configured_target,
                "desired_period_target_pairs": desired_target,
                "required_period_target_pairs": required_target,
                "actual_period_pairs": actual_regular,
                "target_gap_pairs": target_gap,
                "desired_target_gap_pairs": desired_gap,
                "remaining_regular_pairs": remaining,
                "future_gap_pairs": future_gap,
                "future_capacity_margin_pairs": future_capacity - remaining,
                "full_capacity_gap_pairs": full_gap,
                "full_capacity_margin_pairs": full_capacity - regular_need,
                "status": "ok" if not flags else "+".join(flags),
            }
        )

    integrity_failures: list[dict[str, Any]] = list(target_issues)
    if schedule_integrity["unknown_lesson_ids"]:
        integrity_failures.append(
            {
                "code": "schedule_unknown_lessons",
                "lesson_ids": schedule_integrity["unknown_lesson_ids"],
            }
        )
    if schedule_integrity["teacher_mismatches"]:
        integrity_failures.append(
            {
                "code": "schedule_teacher_mismatches",
                "count": len(schedule_integrity["teacher_mismatches"]),
            }
        )
    if schedule_integrity["outside_period_items"]:
        integrity_failures.append(
            {
                "code": "schedule_items_outside_configured_period",
                "count": schedule_integrity["outside_period_items"],
            }
        )

    strict_ok = not integrity_failures and not target_shortfalls and not future_infeasible
    return {
        "audit": "semester_teacher_readout",
        "data": str(data_path.resolve()),
        "schedule": str(schedule_path.resolve()),
        "period": {
            "from": period_start.isoformat(),
            "to": period_end.isoformat(),
            "semester_deadline": SEMESTER_DEADLINE.isoformat(),
            "future_from": (period_end + timedelta(days=1)).isoformat(),
        },
        "conversion": {
            "regular": "ceil(total_hours / 2)",
            "up": "ceil(total_hours / 6), scheduled occurrences counted by block starts",
            "pp": "ceil(total_hours / 2)",
        },
        "summary": {
            "teachers_audited": len(teacher_rows),
            "configured_period_targets": len(targets),
            "period_targets_met": not target_shortfalls,
            "period_target_shortfalls": len(target_shortfalls),
            "soft_desired_target_shortfalls": sum(
                row["desired_target_gap_pairs"] > 0 for row in teacher_rows
            ),
            "full_semester_physically_infeasible": len(full_infeasible),
            "future_infeasible_after_period": len(future_infeasible),
            "strict_ok": strict_ok,
        },
        "strict_failures": {
            "integrity": integrity_failures,
            "period_target_shortfalls": target_shortfalls,
            "future_infeasible_after_period": future_infeasible,
        },
        "diagnostics": {
            "full_semester_physically_infeasible": full_infeasible,
            "schedule_integrity": schedule_integrity,
            "ignored_inactive_occurrences": ignored_inactive,
            "unassigned_active_occurrences": unassigned,
        },
        "teachers": teacher_rows,
        "limitations": ([
            "The timetable for 2026-09-02 is outside this audit period and is excluded from actual readout and capacity calculations."
        ] if period_start > date(2026, 9, 2) else []) + [
            "UP and PP are reported separately and are not credited toward the regular-lesson pace gate; they require separate practice calendars.",
            "Capacity is an availability upper bound; group, room, subject, and cross-teacher conflicts can only reduce achievable readout.",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Audit teacher readout pace through 2026-12-26."
    )
    parser.add_argument("--data", type=Path, default=DEFAULT_DATA)
    parser.add_argument("--schedule", type=Path, default=DEFAULT_SCHEDULE)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Exit 1 on integrity errors, a period-target gap, or future infeasibility.",
    )
    args = parser.parse_args()

    try:
        data = json.loads(args.data.read_text(encoding="utf-8-sig"))
        schedule = json.loads(args.schedule.read_text(encoding="utf-8-sig"))
        if not isinstance(data, dict) or not isinstance(schedule, dict):
            raise ValueError("input JSON roots must be objects")
        report = build_report(data, schedule, args.data, args.schedule)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"SEMESTER READOUT AUDIT ERROR: {exc}", file=sys.stderr)
        return 2

    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    summary = report["summary"]
    print(
        "Semester readout audit: "
        f"teachers={summary['teachers_audited']}, "
        f"target_shortfalls={summary['period_target_shortfalls']}, "
        f"future_infeasible={summary['future_infeasible_after_period']}, "
        f"strict_ok={str(summary['strict_ok']).lower()}"
    )
    print(f"Report: {args.report.resolve()}")
    return 1 if args.strict and not summary["strict_ok"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
