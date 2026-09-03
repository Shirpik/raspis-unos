#!/usr/bin/env python3
"""Find weekly subject families that cannot fit under the 2-pairs/day rule."""

from __future__ import annotations

import json
from collections import Counter, defaultdict, deque
from datetime import date, datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA_PATH = ROOT / "data" / "timetable_data.json"
SCHEDULE_PATH = ROOT / "output" / "latest" / "schedule_all.json"


def load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def iso_date(value: str) -> date:
    return datetime.strptime(value, "%Y-%m-%d").date()


def is_unavailable(rows, entity_id: int, day: date) -> bool:
    day_text = day.isoformat()
    for row in rows:
        target = row.get("teacher", row.get("group", row.get("id", -1)))
        if target != entity_id:
            continue
        if day_text in row.get("dates", []):
            return True
        start = row.get("from", "")
        end = row.get("to", "")
        if start and end and start <= day_text <= end:
            return True
    return False


def allowed_slots(entity: dict, day: date) -> set[int]:
    day_text = day.isoformat()
    for override in entity.get("date_slot_overrides", []):
        if override.get("date") == day_text:
            return {int(slot) for slot in override.get("slots", [])}
    for work_day in entity.get("work_days", []):
        if int(work_day.get("day", 0)) == day.isoweekday():
            if not work_day.get("enabled", False):
                return set()
            return {int(slot) for slot in work_day.get("slots", [])}
    return set(range(1, 8))


def max_flow(lesson_demands: dict[int, int], availability: dict[int, list[str]]) -> int:
    source = ("source", 0)
    sink = ("sink", 0)
    capacity = defaultdict(int)
    adjacency = defaultdict(list)

    def add_edge(left, right, value):
        capacity[left, right] += value
        adjacency[left].append(right)
        adjacency[right].append(left)

    all_days = sorted({day for days in availability.values() for day in days})
    for lesson_id, demand in lesson_demands.items():
        lesson_node = ("lesson", lesson_id)
        add_edge(source, lesson_node, demand)
        for day_text in availability[lesson_id]:
            add_edge(lesson_node, ("day", day_text), demand)
    for day_text in all_days:
        add_edge(("day", day_text), sink, 2)

    flow = 0
    while True:
        previous = {source: None}
        queue = deque([source])
        while queue and sink not in previous:
            node = queue.popleft()
            for other in adjacency[node]:
                if other not in previous and capacity[node, other] > 0:
                    previous[other] = node
                    queue.append(other)
        if sink not in previous:
            return flow
        amount = 10**9
        node = sink
        while previous[node] is not None:
            parent = previous[node]
            amount = min(amount, capacity[parent, node])
            node = parent
        node = sink
        while previous[node] is not None:
            parent = previous[node]
            capacity[parent, node] -= amount
            capacity[node, parent] += amount
            node = parent
        flow += amount


def main() -> None:
    data = load_json(DATA_PATH)
    schedule = load_json(SCHEDULE_PATH)
    lessons = {int(row["id"]): row for row in data["lessons"]}
    teachers = {int(row["id"]): row for row in data["teachers"]}
    groups = {int(row["id"]): row for row in data["groups"]}

    quota = Counter()
    dates = set()
    for group_schedule in schedule.get("groups", []):
        for day in group_schedule.get("days", []):
            dates.add(day["date_iso"])
            for slot in day.get("slots", []):
                for lesson in slot.get("lessons", []):
                    quota[int(lesson["id"])] += 1

    parsed_dates = [iso_date(value) for value in sorted(dates)]
    families = defaultdict(dict)
    for lesson_id, demand in quota.items():
        lesson = lessons[lesson_id]
        group_id = int(lesson["group"])
        real_parts = max(1, min(2, int(groups[group_id].get("parts", 2))))
        subgroup = int(lesson.get("subgroup", -1))
        affected_parts = range(real_parts) if subgroup < 0 else [subgroup - group_id * 2]
        for part in affected_parts:
            if 0 <= part < real_parts:
                key = (group_id, part, bool(lesson.get("is_lab", False)), lesson["name"])
                families[key][lesson_id] = demand

    problems = []
    for (group_id, part, is_lab, subject), demands in families.items():
        available = {}
        for lesson_id in demands:
            lesson = lessons[lesson_id]
            teacher_id = int(lesson.get("teacher", -1))
            lesson_days = []
            for day in parsed_dates:
                if is_unavailable(data.get("unavailable", []), group_id, day):
                    continue
                group_slots = allowed_slots(groups[group_id], day)
                teacher_slots = set(range(1, 8))
                if teacher_id >= 0:
                    if is_unavailable(data.get("teacher_unavailable", []), teacher_id, day):
                        continue
                    teacher_slots = allowed_slots(teachers[teacher_id], day)
                if group_slots & teacher_slots:
                    lesson_days.append(day.isoformat())
            available[lesson_id] = lesson_days
        required = sum(demands.values())
        possible = max_flow(demands, available)
        if possible < required:
            problems.append(
                {
                    "group": groups[group_id]["name"],
                    "group_id": group_id,
                    "part": part + 1,
                    "subject": subject,
                    "is_lab": is_lab,
                    "required": required,
                    "possible": possible,
                    "lesson_ids": sorted(demands),
                    "demands": demands,
                    "available": available,
                }
            )

    print(json.dumps(problems, ensure_ascii=False, indent=2))
    print(f"\nНеразмещаемых семейств: {len(problems)}")


if __name__ == "__main__":
    main()
