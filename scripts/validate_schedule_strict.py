#!/usr/bin/env python3
"""Independent fail-closed validation of a generated timetable.

The validator reads only the persisted input and ``schedule_all.json``.  It is
deliberately separate from the CP-SAT model so a successful solver status is
never accepted as proof of a complete timetable.
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter, defaultdict
from datetime import date, timedelta
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def work_schedule_allows(entity: dict, current: date, pair: int) -> bool:
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
            return pair in {int(value) for value in override.get("slots", [])}

    rule = next(
        (item for item in entity.get("work_days", [])
         if int(item.get("day", 0)) == current.isoweekday()),
        None,
    )
    if rule is None:
        return current.isoweekday() != 7
    if not bool(rule.get("enabled", current.isoweekday() != 7)):
        return False
    slots = rule.get("slots")
    if isinstance(slots, list):
        return pair in {int(value) for value in slots}
    return int(rule.get("start_slot", 1)) <= pair <= int(rule.get("end_slot", 7))


def unavailable_teacher_dates(data: dict) -> dict[int, set[date]]:
    result: dict[int, set[date]] = defaultdict(set)
    for item in data.get("teacher_unavailable", []):
        teacher_id = int(item.get("teacher", -1))
        for raw in item.get("dates", []):
            try:
                result[teacher_id].add(date.fromisoformat(raw))
            except ValueError:
                pass
        try:
            first = date.fromisoformat(str(item.get("from", "")))
            last = date.fromisoformat(str(item.get("to", "")))
        except ValueError:
            continue
        while first <= last:
            result[teacher_id].add(first)
            first += timedelta(days=1)
    return result


def lesson_parts(lesson: dict, groups: dict[int, dict]) -> tuple[int, ...]:
    group_id = int(lesson.get("group", -1))
    group = groups.get(group_id)
    if group is None:
        return ()
    count = max(1, int(group.get("parts", 2)))
    subgroup = int(lesson.get("subgroup", -1))
    if subgroup == -1:
        return tuple(range(count))
    local = subgroup - group_id * 2
    return (local,) if 0 <= local < count else ()


def parse_exact_targets(values: list[str]) -> dict[int, int]:
    result = {}
    for value in values:
        try:
            teacher, count = value.split("=", 1)
            teacher_id = int(teacher)
            expected = int(count)
        except (ValueError, TypeError) as exc:
            raise SystemExit(f"Invalid --teacher-daily-exact value: {value!r}") from exc
        if teacher_id < 0 or expected < 0 or expected > 7:
            raise SystemExit(f"Invalid --teacher-daily-exact range: {value!r}")
        result[teacher_id] = expected
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", type=Path, default=ROOT / "data" / "timetable_data.json")
    parser.add_argument("--schedule", type=Path, default=ROOT / "output" / "latest" / "schedule_all.json")
    parser.add_argument("--quality", type=Path, default=ROOT / "output" / "latest" / "quality_report.json")
    parser.add_argument("--expect-from")
    parser.add_argument("--expect-to")
    parser.add_argument("--teacher-daily-exact", action="append", default=[], metavar="ID=COUNT")
    parser.add_argument("--report", type=Path, help="also write the JSON result to this file")
    args = parser.parse_args()

    data = read_json(args.data)
    schedule = read_json(args.schedule)
    settings = data.get("settings", {})
    config = settings.get("solver_config", {})
    expected_from = date.fromisoformat(args.expect_from or settings["start_date"])
    expected_to = date.fromisoformat(args.expect_to or settings["end_date"])
    expected_dates = []
    current = expected_from
    while current <= expected_to:
        if current.isoweekday() <= 6:
            expected_dates.append(current)
        current += timedelta(days=1)
    expected_date_set = set(expected_dates)

    groups = {int(item["id"]): item for item in data.get("groups", [])}
    teachers = {int(item["id"]): item for item in data.get("teachers", [])}
    lessons = {int(item["id"]): item for item in data.get("lessons", [])}
    rooms = {int(item["id"]): item for item in data.get("rooms", [])}
    active_lessons = {
        lesson_id: lesson for lesson_id, lesson in lessons.items()
        if bool(lesson.get("plan_active", True))
        and bool(lesson.get("generation_active", True))
        and int(lesson.get("total_slots", 0)) > 0
    }
    unavailable = unavailable_teacher_dates(data)
    errors: list[dict] = []
    warnings: list[dict] = []

    # A short-week quota may defer semester work, but may never create hours
    # that do not exist in the imported curriculum.
    for lesson_id, lesson in active_lessons.items():
        academic_hours = int(lesson.get("total_hours", 0))
        divisor = 6 if lesson.get("is_block", False) else 2
        semester_limit = (academic_hours + divisor - 1) // divisor if academic_hours > 0 else 0
        short_quota = int(lesson.get("total_slots", 0))
        if short_quota > semester_limit:
            errors.append({
                "code": "short_quota_exceeds_semester",
                "lesson": lesson_id,
                "name": lesson.get("name", ""),
                "quota": short_quota,
                "semester_limit": semester_limit,
                "total_hours": academic_hours,
            })
        consecutive = int(lesson.get("consecutive_pairs", 1))
        if consecutive not in (1, 2):
            errors.append({"code": "invalid_consecutive_pairs", "lesson": lesson_id, "value": consecutive})
        if consecutive == 2 and short_quota % 2:
            errors.append({"code": "odd_consecutive_pair_quota", "lesson": lesson_id, "quota": short_quota})

    events = []
    output_dates = set()
    seen_group_ids = set()
    for group_output in schedule.get("groups", []):
        group_id = int(group_output.get("group_index", -1))
        seen_group_ids.add(group_id)
        if group_id not in groups:
            errors.append({"code": "unknown_output_group", "group": group_id})
            continue
        for day in group_output.get("days", []):
            try:
                event_date = date.fromisoformat(day["date_iso"])
            except (KeyError, ValueError):
                errors.append({"code": "invalid_output_date", "value": day.get("date_iso")})
                continue
            output_dates.add(event_date)
            for slot in day.get("slots", []):
                pair = int(slot.get("slot", 0))
                if pair < 1 or pair > 7:
                    errors.append({"code": "invalid_output_pair", "date": event_date.isoformat(), "pair": pair})
                    continue
                for rendered in slot.get("lessons", []):
                    lesson_id = int(rendered.get("id", -1))
                    lesson = lessons.get(lesson_id)
                    if lesson is None:
                        errors.append({"code": "unknown_output_lesson", "lesson": lesson_id})
                        continue
                    if int(lesson.get("group", -1)) != group_id:
                        errors.append({"code": "lesson_in_wrong_group", "lesson": lesson_id, "group": group_id})
                    events.append({
                        "date": event_date,
                        "pair": pair,
                        "group": group_id,
                        "lesson": lesson_id,
                        "teacher": int(rendered.get("teacher_id", lesson.get("teacher", -1))),
                        "room": int(rendered.get("room_id", -1)) if rendered.get("room_id") is not None else -1,
                    })

    if output_dates != expected_date_set:
        errors.append({
            "code": "wrong_output_date_range",
            "expected": [value.isoformat() for value in expected_dates],
            "actual": [value.isoformat() for value in sorted(output_dates)],
        })
    missing_groups = sorted(set(groups) - seen_group_ids)
    if missing_groups:
        errors.append({"code": "groups_missing_from_output", "groups": missing_groups})

    raw_occurrences = Counter(event["lesson"] for event in events)
    block_slots: dict[tuple[int, date], set[int]] = defaultdict(set)
    for event in events:
        if lessons[event["lesson"]].get("is_block", False):
            block_slots[(event["lesson"], event["date"])].add(event["pair"])
    block_starts = Counter()
    for (lesson_id, event_date), slots in block_slots.items():
        for pair in sorted(slots):
            if pair - 1 not in slots:
                block_starts[lesson_id] += 1
    scheduled_occurrences = Counter(raw_occurrences)
    for lesson_id, lesson in lessons.items():
        if lesson.get("is_block", False):
            scheduled_occurrences[lesson_id] = block_starts[lesson_id]

    for lesson_id, lesson in active_lessons.items():
        expected = int(lesson.get("total_slots", 0))
        actual = scheduled_occurrences[lesson_id]
        if actual != expected:
            errors.append({
                "code": "lesson_quota_mismatch",
                "lesson": lesson_id,
                "name": lesson.get("name", ""),
                "expected": expected,
                "actual": actual,
            })
    unexpected = sorted(
        lesson_id for lesson_id, count in scheduled_occurrences.items()
        if count and lesson_id not in active_lessons
    )
    if unexpected:
        errors.append({"code": "inactive_lessons_scheduled", "lessons": unexpected})

    lesson_day_pairs: dict[tuple[int, date], set[int]] = defaultdict(set)
    for event in events:
        lesson_day_pairs[(event["lesson"], event["date"])].add(event["pair"])
    for (lesson_id, event_date), pairs in lesson_day_pairs.items():
        lesson = lessons[lesson_id]
        ordered = sorted(pairs)
        if int(lesson.get("consecutive_pairs", 1)) == 2:
            valid = len(ordered) % 2 == 0 and all(
                ordered[index + 1] == ordered[index] + 1
                for index in range(0, len(ordered), 2)
            )
            if not valid:
                errors.append({
                    "code": "lesson_consecutive_pairs_broken", "lesson": lesson_id,
                    "date": event_date.isoformat(), "pairs": ordered,
                })
            if lesson.get("avoid_lunch_split", False) and any(
                ordered[index:index + 2] == [2, 3]
                for index in range(0, len(ordered) - 1, 2)
            ):
                errors.append({
                    "code": "lesson_pair_crosses_lunch", "lesson": lesson_id,
                    "date": event_date.isoformat(), "pairs": ordered,
                })
        elif lesson.get("avoid_lunch_split", False) and 2 in pairs and 3 in pairs:
            errors.append({
                "code": "lesson_pair_crosses_lunch", "lesson": lesson_id,
                "date": event_date.isoformat(), "pairs": ordered,
            })

    teacher_slot: dict[tuple[date, int, int], list[int]] = defaultdict(list)
    room_slot: dict[tuple[date, int, int], list[int]] = defaultdict(list)
    part_slot: dict[tuple[date, int, int, int], list[int]] = defaultdict(list)
    teacher_day_slots: dict[tuple[int, date], set[int]] = defaultdict(set)
    part_day_slots: dict[tuple[int, int, date], set[int]] = defaultdict(set)
    # Same-subject limits intentionally have two different scopes:
    #   * whole-group lessons are counted once for the group;
    #   * every physical subgroup counts whole-group + its own lessons.
    # This permits a 3+3 split across parallel subgroups while preventing
    # three whole-group lessons of one subject in a day.
    whole_day_subjects: Counter = Counter()
    part_day_subjects: Counter = Counter()
    teacher_day_campuses: dict[tuple[int, date], set[int]] = defaultdict(set)
    part_day_campuses: dict[tuple[int, int, date], set[int]] = defaultdict(set)

    for event in events:
        lesson = lessons[event["lesson"]]
        group = groups[event["group"]]
        teacher = teachers.get(event["teacher"])
        room = rooms.get(event["room"])
        event_iso = event["date"].isoformat()
        pair = event["pair"]
        parts = lesson_parts(lesson, groups)
        raw_subject_id = lesson.get("subject_id", -1)
        try:
            numeric_subject_id = int(raw_subject_id)
        except (TypeError, ValueError):
            numeric_subject_id = -1
        subject_key: int | str = (
            numeric_subject_id
            if numeric_subject_id >= 0
            else f"name:{str(lesson.get('name', '')).strip().casefold()}"
        )
        if not parts:
            errors.append({"code": "invalid_lesson_subgroup", "lesson": event["lesson"], "subgroup": lesson.get("subgroup")})
        teacher_slot[(event["date"], pair, event["teacher"])].append(event["lesson"])
        teacher_day_slots[(event["teacher"], event["date"])].add(pair)
        for part in parts:
            part_slot[(event["date"], pair, event["group"], part)].append(event["lesson"])
            part_day_slots[(event["group"], part, event["date"])].add(pair)
            part_day_subjects[(event["group"], part, event["date"], subject_key)] += 1
        if int(lesson.get("subgroup", -1)) == -1:
            whole_day_subjects[(event["group"], event["date"], subject_key)] += 1

        if event["date"] not in expected_date_set:
            errors.append({"code": "event_outside_period", "lesson": event["lesson"], "date": event_iso})
        if not work_schedule_allows(group, event["date"], pair):
            errors.append({"code": "group_unavailable", "group": event["group"], "date": event_iso, "pair": pair})
        if teacher is None:
            errors.append({"code": "unknown_teacher", "teacher": event["teacher"], "lesson": event["lesson"]})
        else:
            if event["date"] in unavailable[event["teacher"]] or not work_schedule_allows(teacher, event["date"], pair):
                errors.append({"code": "teacher_unavailable", "teacher": event["teacher"], "date": event_iso, "pair": pair})

        if room is None:
            errors.append({"code": "room_unassigned", "lesson": event["lesson"], "date": event_iso, "pair": pair})
            continue
        room_slot[(event["date"], pair, event["room"])].append(event["lesson"])
        campus = int(room.get("campus", -1))
        teacher_day_campuses[(event["teacher"], event["date"])].add(campus)
        for part in parts:
            part_day_campuses[(event["group"], part, event["date"])].add(campus)
        if not bool(room.get("active", True)) or room.get("access_mode") == "blocked":
            errors.append({"code": "blocked_room", "room": event["room"], "lesson": event["lesson"], "date": event_iso, "pair": pair})
        if not work_schedule_allows(room, event["date"], pair):
            errors.append({"code": "room_unavailable", "room": event["room"], "date": event_iso, "pair": pair})
        owners = {int(value) for value in room.get("responsible_teacher_ids", [])}
        if room.get("access_mode") == "exclusive" and event["teacher"] not in owners:
            errors.append({"code": "exclusive_room_access", "room": event["room"], "teacher": event["teacher"]})
        if event_iso >= "2026-09-05":
            lesson_name = str(lesson.get("name", ""))
            is_lpz = "лпз" in lesson_name.casefold()
            teacher_id = event["teacher"]
            ordinary_lesnaya = (
                campus == 0
                and str(room.get("name", "")) != "210"
                and room.get("access_mode", "general") == "general"
                and bool(room.get("active", True))
            )
            policy_ok = True
            if teacher_id == 55:
                policy_ok = event["room"] == 66 if is_lpz else ordinary_lesnaya
            elif teacher_id == 59:
                policy_ok = event["room"] in {64, 65} if is_lpz else ordinary_lesnaya
            elif teacher_id == 49 and is_lpz:
                policy_ok = event["room"] == 68
            if not policy_ok:
                errors.append({
                    "code": "operational_room_policy_mismatch",
                    "lesson": event["lesson"],
                    "teacher": teacher_id,
                    "room": event["room"],
                    "date": event_iso,
                    "pair": pair,
                })
        if int(lesson.get("fixed_room", -1)) >= 0 and event["room"] != int(lesson["fixed_room"]):
            errors.append({"code": "fixed_room_mismatch", "lesson": event["lesson"], "expected": lesson["fixed_room"], "actual": event["room"]})
        required_purpose = str(lesson.get("required_room_purpose", ""))
        if required_purpose and room.get("purpose", "") != required_purpose:
            errors.append({"code": "room_purpose_mismatch", "lesson": event["lesson"], "room": event["room"], "required": required_purpose})
        required_type = int(lesson.get("required_room_type", 0))
        if required_type > 0 and int(room.get("room_type", 0)) != required_type:
            errors.append({"code": "room_type_mismatch", "lesson": event["lesson"], "room": event["room"], "required": required_type})
        required_capacity = int(lesson.get("required_capacity", 0))
        if required_capacity > 0 and int(room.get("capacity", 0)) < required_capacity:
            errors.append({"code": "room_capacity_mismatch", "lesson": event["lesson"], "room": event["room"], "required": required_capacity})
        required_equipment = set(map(str, lesson.get("required_equipment", [])))
        if not required_equipment.issubset(set(map(str, room.get("equipment", [])))):
            errors.append({"code": "room_equipment_mismatch", "lesson": event["lesson"], "room": event["room"], "required": sorted(required_equipment)})
        lesson_campuses = {int(value) for value in lesson.get("allowed_campuses", [])}
        if lesson_campuses and campus not in lesson_campuses:
            errors.append({"code": "lesson_campus_mismatch", "lesson": event["lesson"], "campus": campus, "allowed": sorted(lesson_campuses)})
        teacher_campuses = {int(value) for value in (teacher or {}).get("allowed_campuses", [])}
        if teacher_campuses and campus not in teacher_campuses:
            errors.append({"code": "teacher_campus_mismatch", "teacher": event["teacher"], "campus": campus, "allowed": sorted(teacher_campuses)})

    for key, values in teacher_slot.items():
        if len(values) > 1:
            errors.append({"code": "teacher_conflict", "date": key[0].isoformat(), "pair": key[1], "teacher": key[2], "lessons": values})
    for key, values in room_slot.items():
        if len(values) > 1:
            errors.append({"code": "room_conflict", "date": key[0].isoformat(), "pair": key[1], "room": key[2], "lessons": values})
    for key, values in part_slot.items():
        if len(values) > 1:
            errors.append({"code": "student_conflict", "date": key[0].isoformat(), "pair": key[1], "group": key[2], "part": key[3], "lessons": values})

    minimum = int(config.get("min_student_pairs_per_study_day", 0))
    maximum = int(config.get("max_student_pairs_per_day", 7))
    minimum_days = int(config.get("min_student_study_days_per_week", 0))
    for group_id, group in groups.items():
        part_count = max(1, int(group.get("parts", 2)))
        for part in range(part_count):
            for event_date in expected_dates:
                slots = part_day_slots[(group_id, part, event_date)]
                if slots:
                    if len(slots) > maximum:
                        errors.append({"code": "student_daily_load", "group": group_id, "part": part, "date": event_date.isoformat(), "count": len(slots), "min": minimum, "max": maximum})
                    elif len(slots) < minimum:
                        issue = {"code": "student_daily_minimum_fallback", "group": group_id, "part": part, "date": event_date.isoformat(), "count": len(slots), "min": minimum}
                        if config.get("allow_single_pair_day_fallback", False):
                            warnings.append(issue)
                        else:
                            errors.append(issue)
                    if config.get("hard_no_student_windows", False) and max(slots) - min(slots) + 1 != len(slots):
                        errors.append({"code": "student_window", "group": group_id, "part": part, "date": event_date.isoformat(), "slots": sorted(slots)})
        if config.get("hard_min_study_days_per_week", False):
            dates_by_week: dict[date, list[date]] = defaultdict(list)
            for event_date in expected_dates:
                dates_by_week[event_date - timedelta(days=event_date.weekday())].append(event_date)
            for week_start, week_dates in dates_by_week.items():
                available_days = sum(
                    any(work_schedule_allows(group, event_date, pair) for pair in range(1, 8))
                    for event_date in week_dates
                )
                required_days = min(minimum_days, available_days)
                for synchronized_part in range(part_count):
                    weekly_pairs = sum(
                        len(part_day_slots[(group_id, synchronized_part, event_date)])
                        for event_date in week_dates
                    )
                    if weekly_pairs <= 0:
                        continue
                    effective_minimum = min(minimum, weekly_pairs) if minimum > 0 else 0
                    required_days = min(
                        required_days,
                        weekly_pairs // effective_minimum if effective_minimum else 0,
                    )
                for part in range(part_count):
                    actual_days = sum(
                        bool(part_day_slots[(group_id, part, event_date)])
                        for event_date in week_dates
                    )
                    if actual_days < required_days:
                        errors.append({"code": "student_study_days", "group": group_id, "part": part, "week": week_start.isoformat(), "actual": actual_days, "required": required_days})

    if config.get("hard_max_two_same_subject_per_day", False):
        subgroup_subject_limit = max(1, min(7, int(config.get("max_same_subject_pairs_per_day", 3))))
        whole_subject_limit = max(
            1,
            min(7, int(config.get("max_whole_group_same_subject_pairs_per_day", 2))),
        )
        for key, count in whole_day_subjects.items():
            if count > whole_subject_limit:
                errors.append({
                    "code": "whole_group_same_subject_daily_limit",
                    "group": key[0],
                    "date": key[1].isoformat(),
                    "subject": key[2],
                    "count": count,
                    "max": whole_subject_limit,
                })
        for key, count in part_day_subjects.items():
            if count > subgroup_subject_limit:
                errors.append({
                    "code": "physical_subgroup_same_subject_daily_limit",
                    "group": key[0],
                    "part": key[1],
                    "date": key[2].isoformat(),
                    "subject": key[3],
                    "count": count,
                    "max": subgroup_subject_limit,
                })

    for (teacher_id, event_date), slots in teacher_day_slots.items():
        teacher = teachers.get(teacher_id, {})
        limit = int(teacher.get("max_pairs_per_day", 0))
        if limit > 0 and len(slots) > min(limit, 7):
            errors.append({"code": "teacher_daily_limit", "teacher": teacher_id, "date": event_date.isoformat(), "count": len(slots), "max": min(limit, 7)})
        if config.get("hard_no_teacher_windows", False) and slots and max(slots) - min(slots) + 1 != len(slots):
            errors.append({"code": "teacher_window", "teacher": teacher_id, "date": event_date.isoformat(), "slots": sorted(slots)})

    for key, campuses in teacher_day_campuses.items():
        if len(campuses) > 1:
            errors.append({"code": "teacher_multi_campus_day", "teacher": key[0], "date": key[1].isoformat(), "campuses": sorted(campuses)})
    for key, campuses in part_day_campuses.items():
        if len(campuses) > 1:
            errors.append({"code": "student_multi_campus_day", "group": key[0], "part": key[1], "date": key[2].isoformat(), "campuses": sorted(campuses)})

    exact_targets = parse_exact_targets(args.teacher_daily_exact)
    for teacher_id, expected in exact_targets.items():
        for event_date in expected_dates:
            actual = len(teacher_day_slots[(teacher_id, event_date)])
            if actual != expected:
                errors.append({"code": "teacher_daily_exact", "teacher": teacher_id, "date": event_date.isoformat(), "expected": expected, "actual": actual})

    teacher_planned_occurrences: Counter = Counter()
    teacher_scheduled_occurrences: Counter = Counter()
    for lesson_id, lesson in active_lessons.items():
        teacher_id = int(lesson.get("teacher", -1))
        if teacher_id >= 0:
            teacher_planned_occurrences[teacher_id] += int(lesson.get("total_slots", 0))
            teacher_scheduled_occurrences[teacher_id] += scheduled_occurrences[lesson_id]

    period_target_report = []
    for raw_target in settings.get("teacher_period_targets", []):
        try:
            teacher_id = int(raw_target["teacher"])
            minimum_pairs = int(raw_target["minimum_pairs"])
        except (KeyError, TypeError, ValueError):
            errors.append({"code": "invalid_teacher_period_target", "value": raw_target})
            continue
        planned = teacher_planned_occurrences[teacher_id]
        actual = teacher_scheduled_occurrences[teacher_id]
        row = {
            "teacher": teacher_id,
            "name": teachers.get(teacher_id, {}).get("name", raw_target.get("name", "")),
            "minimum_pairs": minimum_pairs,
            "planned_pairs": planned,
            "scheduled_pairs": actual,
            "gap": max(0, minimum_pairs - actual),
        }
        period_target_report.append(row)
        if planned < minimum_pairs:
            errors.append({
                "code": "teacher_period_target_not_planned",
                "teacher": teacher_id,
                "minimum_pairs": minimum_pairs,
                "planned_pairs": planned,
            })
        if actual < minimum_pairs:
            errors.append({
                "code": "teacher_period_target_not_scheduled",
                "teacher": teacher_id,
                "minimum_pairs": minimum_pairs,
                "scheduled_pairs": actual,
            })

    if args.quality.exists():
        quality = read_json(args.quality)
        if int(quality.get("remaining_hours", -1)) != 0:
            errors.append({"code": "quality_remaining_hours", "value": quality.get("remaining_hours")})
        rooms_report = quality.get("rooms", {})
        if int(rooms_report.get("unassigned", -1)) != 0:
            errors.append({"code": "quality_unassigned_rooms", "value": rooms_report.get("unassigned")})
        if float(quality.get("completion_percent", -1)) != 100:
            errors.append({"code": "quality_incomplete", "value": quality.get("completion_percent")})
        for field in ("missing_hours", "excess_hours", "mismatched_lessons"):
            if int(quality.get(field, -1)) != 0:
                errors.append({"code": f"quality_{field}", "value": quality.get(field)})
        if quality.get("load_matches_plan_exactly") is not True:
            errors.append({
                "code": "quality_load_not_exact",
                "value": quality.get("load_matches_plan_exactly"),
            })
    else:
        warnings.append({"code": "quality_report_missing", "path": str(args.quality)})

    teacher_report = []
    for teacher_id, teacher in teachers.items():
        counts = {event_date.isoformat(): len(teacher_day_slots[(teacher_id, event_date)]) for event_date in expected_dates}
        total = sum(counts.values())
        if total:
            teacher_report.append({"id": teacher_id, "name": teacher.get("name", ""), "days": counts, "total": total})
    teacher_report.sort(key=lambda row: (-row["total"], row["name"]))

    aggregate_group_events = Counter((event["group"], event["date"]) for event in events)
    part_totals = {
        (group_id, part): sum(len(part_day_slots[(group_id, part, event_date)]) for event_date in expected_dates)
        for group_id, group in groups.items()
        for part in range(max(1, int(group.get("parts", 2))))
    }
    part_distribution = Counter(part_totals.values())
    busiest_group_day = max(aggregate_group_events, key=aggregate_group_events.get, default=None)
    parallel_group_days = []
    for (group_id, event_date), event_count in sorted(
        aggregate_group_events.items(), key=lambda item: (-item[1], item[0][0], item[0][1])
    ):
        loads = [
            len(part_day_slots[(group_id, part, event_date)])
            for part in range(max(1, int(groups[group_id].get("parts", 2))))
        ]
        if event_count > max(loads, default=0):
            parallel_group_days.append({
                "group": group_id,
                "name": groups[group_id].get("name", ""),
                "date": event_date.isoformat(),
                "events": event_count,
                "part_loads": loads,
            })
    result = {
        "passed": not errors,
        "period": {"from": expected_from.isoformat(), "to": expected_to.isoformat()},
        "active_lessons": len(active_lessons),
        "planned_occurrences": sum(int(lesson.get("total_slots", 0)) for lesson in active_lessons.values()),
        "scheduled_occurrences": sum(scheduled_occurrences[lesson_id] for lesson_id in active_lessons),
        "rendered_slot_events": len(events),
        "max_aggregate_group_events_per_day": max(aggregate_group_events.values(), default=0),
        "busiest_group_day": ({
            "group": busiest_group_day[0],
            "date": busiest_group_day[1].isoformat(),
            "events": aggregate_group_events[busiest_group_day],
            "part_loads": [
                len(part_day_slots[(busiest_group_day[0], part, busiest_group_day[1])])
                for part in range(max(1, int(groups[busiest_group_day[0]].get("parts", 2))))
            ],
        } if busiest_group_day else None),
        "parallel_group_days": parallel_group_days,
        "student_part_load": {
            "parts": len(part_totals),
            "minimum": min(part_totals.values(), default=0),
            "maximum": max(part_totals.values(), default=0),
            "distribution": {str(total): count for total, count in sorted(part_distribution.items())},
        },
        "teacher_load": teacher_report,
        "teacher_period_targets": period_target_report,
        "error_count": len(errors),
        "errors": errors,
        "warnings": warnings,
    }
    rendered = json.dumps(result, ensure_ascii=False, indent=2)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered + "\n", encoding="utf-8")
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    print(rendered)
    return 0 if not errors else 1


if __name__ == "__main__":
    sys.exit(main())
