#!/usr/bin/env python3
"""Independent checks for the generated two-week timetable."""

from __future__ import annotations

import json
import argparse
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def load(path: Path):
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", type=Path, default=ROOT / "data" / "timetable_data.json")
    parser.add_argument("--schedule", type=Path, default=ROOT / "output" / "latest" / "schedule_all.json")
    args = parser.parse_args()
    data = load(args.data)
    schedule = load(args.schedule)
    lessons = {int(item["id"]): item for item in data.get("lessons", [])}

    group_day_events: dict[tuple[int, str], list[tuple[int, int]]] = defaultdict(list)
    teacher_slots: dict[tuple[int, str], set[int]] = defaultdict(set)
    teacher_uses: dict[tuple[int, str, int], list[tuple[int, int]]] = defaultdict(list)
    room_uses: dict[tuple[int, str, int], list[tuple[int, int]]] = defaultdict(list)
    occurrences: dict[int, list[tuple[str, int, int]]] = defaultdict(list)
    scheduled_kinds = defaultdict(int)

    first_date = None
    last_date = None
    for group in schedule.get("groups", []):
        group_id = int(group["group_index"])
        for day in group.get("days", []):
            date = day.get("date_iso", "")
            for slot in day.get("slots", []):
                slot_number = int(slot["slot"])
                for event in slot.get("lessons", []):
                    lesson_id = int(event["id"])
                    source = lessons.get(lesson_id)
                    if source is None:
                        continue
                    first_date = date if first_date is None else min(first_date, date)
                    last_date = date if last_date is None else max(last_date, date)
                    occurrences[group_id].append((date, slot_number, lesson_id))
                    group_day_events[(group_id, date)].append(
                        (slot_number, int(event.get("subgroup", -1)))
                    )
                    raw_teacher_id = event.get("teacher_id", -1)
                    teacher_id = int(raw_teacher_id) if raw_teacher_id is not None else -1
                    if teacher_id >= 0:
                        teacher_slots[(teacher_id, date)].add(slot_number)
                        teacher_uses[(teacher_id, date, slot_number)].append((group_id, lesson_id))
                    raw_room_id = event.get("room_id", -1)
                    room_id = -1 if raw_room_id is None else int(raw_room_id)
                    if room_id >= 0:
                        room_uses[(room_id, date, slot_number)].append((group_id, lesson_id))
                    if source.get("is_pp"):
                        scheduled_kinds["pp"] += 1
                    elif source.get("is_block"):
                        scheduled_kinds["up"] += 1
                    elif source.get("is_lab"):
                        scheduled_kinds["lpz"] += 1
                    else:
                        scheduled_kinds["theory"] += 1
    student_window_days = []
    student_under_minimum = []
    student_over_limit = []
    # Check actual student cohorts. A whole-group lesson (subgroup=-1) is combined
    # with the lessons of each subgroup; parallel subgroup lessons are not treated
    # as one student's day.
    for key, events in group_day_events.items():
        subgroup_ids = sorted({subgroup for _, subgroup in events if subgroup >= 0})
        cohorts = subgroup_ids or [-1]
        for cohort in cohorts:
            ordered = sorted(
                {
                    slot
                    for slot, subgroup in events
                    if subgroup == -1 or subgroup == cohort
                }
            )
            cohort_key = (*key, cohort)
            if 0 < len(ordered) < 2:
                student_under_minimum.append((cohort_key, ordered))
            if len(ordered) > 4:
                student_over_limit.append((cohort_key, ordered))
            if ordered and ordered != list(range(ordered[0], ordered[-1] + 1)):
                student_window_days.append((cohort_key, ordered))

    teacher_conflicts = {
        key: uses for key, uses in teacher_uses.items() if len(set(uses)) > 1
    }
    room_conflicts = {
        key: uses for key, uses in room_uses.items() if len(set(uses)) > 1
    }
    max_teacher_pairs = max((len(slots) for slots in teacher_slots.values()), default=0)

    lab_violations = []
    lab_occurrences = 0
    for group_id, group_occurrences in occurrences.items():
        ordered = sorted(group_occurrences)
        prior_theory: dict[tuple[int, int], int] = defaultdict(int)
        for date, slot, lesson_id in ordered:
            lesson = lessons[lesson_id]
            subject_id = int(lesson.get("subject_id", -1))
            subgroup = int(lesson.get("subgroup", -1))
            if lesson.get("is_lab"):
                lab_occurrences += 1
                preceding = prior_theory[(subject_id, -1)]
                if subgroup >= 0:
                    preceding += prior_theory[(subject_id, subgroup)]
                if preceding < 1:
                    lab_violations.append(
                        {
                            "group": group_id,
                            "date": date,
                            "slot": slot,
                            "lesson": lesson_id,
                            "subject_id": subject_id,
                        }
                    )
            elif not lesson.get("is_block") and not lesson.get("is_pp"):
                prior_theory[(subject_id, subgroup)] += 1

    result = {
        "first_scheduled_date": first_date,
        "last_scheduled_date": last_date,
        "scheduled_event_kinds": dict(scheduled_kinds),
        "student_window_days": len(student_window_days),
        "student_days_below_two_pairs": len(student_under_minimum),
        "student_days_over_four_pairs": len(student_over_limit),
        "max_teacher_pairs_per_day": max_teacher_pairs,
        "teacher_slot_conflicts": len(teacher_conflicts),
        "room_slot_conflicts": len(room_conflicts),
        "lpz_occurrences": lab_occurrences,
        "lpz_before_theory_violations": len(lab_violations),
        "lpz_violation_examples": lab_violations[:10],
        "passed": not any(
            (
                student_window_days,
                student_under_minimum,
                student_over_limit,
                teacher_conflicts,
                room_conflicts,
                lab_violations,
                scheduled_kinds.get("up", 0),
                scheduled_kinds.get("pp", 0),
                max_teacher_pairs > 7,
            )
        ),
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
