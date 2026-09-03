#!/usr/bin/env python3
"""List obvious day-level bottlenecks for the current short week."""

import json
from collections import defaultdict
from datetime import date
from itertools import product
from pathlib import Path

from prepare_one_week_generation import rule_allows, unavailable_dates


root = Path(__file__).resolve().parents[1]
data = json.loads((root / "data" / "timetable_data.json").read_text(encoding="utf-8"))
teachers = {int(x["id"]): x for x in data["teachers"]}
groups = {int(x["id"]): x for x in data["groups"]}
unavailable = unavailable_dates(data)
days = [date(2026, 9, day) for day in range(2, 6)]
lessons = [x for x in data["lessons"] if x.get("plan_active") and int(x.get("total_slots", 0)) > 0]
by_group = defaultdict(list)
for lesson in lessons:
    by_group[int(lesson["group"])].append(lesson)

problems = []
for group_id, group_lessons in by_group.items():
    row = []
    for current in days:
        options = []
        for lesson in group_lessons:
            teacher_id = int(lesson["teacher"])
            if current in unavailable[teacher_id]:
                continue
            slots = [
                pair for pair in range(1, 8)
                if rule_allows(groups[group_id], current, pair)
                and rule_allows(teachers[teacher_id], current, pair)
            ]
            if slots:
                options.append((teacher_id, slots, int(lesson.get("total_slots", 0))))
        union = sorted({slot for _, slots, _ in options for slot in slots})
        if len(union) < 2:
            row.append({"date": current.isoformat(), "allowed_slots": union, "lesson_options": len(options)})
    if row:
        problems.append({"group": groups[group_id]["name"], "days": row})


def load_patterns(total: int, study_days: int) -> list[tuple[int, ...]]:
    if study_days == 0:
        return [()] if total == 0 else []
    return [values for values in product(range(2, 5), repeat=study_days) if sum(values) == total]


campus_partition_problems = []
campus_day_availability_problems = []
for group_id, group_lessons in by_group.items():
    for part in (0, 1):
        fixed = [0, 0]
        flexible = 0
        details = []
        for lesson in group_lessons:
            subgroup = int(lesson.get("subgroup", -1))
            if subgroup not in (-1, part):
                continue
            quota = int(lesson.get("total_slots", 0))
            teacher = teachers[int(lesson["teacher"])]
            allowed = [int(value) for value in teacher.get("allowed_campuses", [])]
            if len(allowed) == 1:
                fixed[allowed[0]] += quota
            else:
                flexible += quota
            details.append({"lesson": lesson.get("name"), "teacher": teacher.get("name"), "quota": quota, "campuses": allowed or [0, 1]})

        feasible = False
        for to_lesnaya in range(flexible + 1):
            loads = [fixed[0] + to_lesnaya, fixed[1] + flexible - to_lesnaya]
            for lesnaya_days in range(5):
                krivousova_days = 4 - lesnaya_days
                for left in load_patterns(loads[0], lesnaya_days):
                    for right in load_patterns(loads[1], krivousova_days):
                        if sum(value == 2 for value in (*left, *right)) <= 1:
                            feasible = True
                            break
                    if feasible:
                        break
                if feasible:
                    break
            if feasible:
                break
        if not feasible:
            campus_partition_problems.append({
                "group": groups[group_id]["name"],
                "part": part + 1,
                "fixed_lesnaya": fixed[0],
                "fixed_krivousova": fixed[1],
                "flexible": flexible,
                "lessons": details,
            })


def part_has_day_assignment(group_id: int, group_lessons: list[dict], part: int) -> bool:
    entries = []
    for lesson in group_lessons:
        if int(lesson.get("subgroup", -1)) not in (-1, part):
            continue
        quota = int(lesson.get("total_slots", 0))
        if quota <= 0:
            continue
        teacher_id = int(lesson["teacher"])
        teacher = teachers[teacher_id]
        allowed = set(int(value) for value in teacher.get("allowed_campuses", [])) or {0, 1}
        available_days = []
        for day_index, current in enumerate(days):
            if current in unavailable[teacher_id]:
                continue
            if any(rule_allows(groups[group_id], current, pair) and rule_allows(teacher, current, pair) for pair in range(1, 8)):
                available_days.append(day_index)
        entries.append((quota, allowed, available_days, lesson, teacher))

    for campus_by_day in product((0, 1), repeat=4):
        prepared = []
        possible = True
        for quota, allowed, available_days, lesson, teacher in entries:
            candidates = [day_index for day_index in available_days if campus_by_day[day_index] in allowed]
            if not candidates or quota > len(candidates) * 4:
                possible = False
                break
            prepared.append((quota, candidates, lesson, teacher))
        if not possible:
            continue
        prepared.sort(key=lambda item: (len(item[1]), -item[0]))
        loads = [0, 0, 0, 0]

        def place(index: int) -> bool:
            if index == len(prepared):
                return all(2 <= value <= 4 for value in loads) and sum(value == 2 for value in loads) <= 1
            quota, candidates, _, _ = prepared[index]

            def distribute(candidate_index: int, left: int) -> bool:
                if candidate_index == len(candidates):
                    return left == 0 and place(index + 1)
                day_index = candidates[candidate_index]
                maximum_here = min(left, 4 - loads[day_index])
                for amount in range(maximum_here, -1, -1):
                    loads[day_index] += amount
                    if distribute(candidate_index + 1, left - amount):
                        return True
                    loads[day_index] -= amount
                return False

            return distribute(0, quota)

        if place(0):
            return True
    return False


for group_id, group_lessons in by_group.items():
    for part in (0, 1):
        if part_has_day_assignment(group_id, group_lessons, part):
            continue
        campus_day_availability_problems.append({
            "group": groups[group_id]["name"],
            "part": part + 1,
        })

print(json.dumps({
    "obvious_day_bottlenecks": problems,
    "campus_partition_problems": campus_partition_problems,
    "campus_day_availability_problems": campus_day_availability_problems,
}, ensure_ascii=False, indent=2))
