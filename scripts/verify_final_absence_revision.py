#!/usr/bin/env python3
"""Independent acceptance checks for the urgent Friday/Saturday schedule."""

from __future__ import annotations

import json
from collections import Counter, defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "timetable_data.json"
SCHEDULE = ROOT / "output" / "latest" / "schedule_all.json"


def main() -> None:
    data = json.loads(DATA.read_text(encoding="utf-8-sig"))
    schedule = json.loads(SCHEDULE.read_text(encoding="utf-8-sig"))
    rooms = {int(item["id"]): item for item in data["rooms"]}
    lessons = {int(item["id"]): item for item in data["lessons"]}
    groups = {int(item["id"]): item for item in data["groups"]}
    events = []
    for group in schedule["groups"]:
        group_id = int(group["group_index"])
        for day in group.get("days", []):
            for slot in day.get("slots", []):
                for rendered in slot.get("lessons", []):
                    events.append({
                        "group": group_id,
                        "date": str(day["date_iso"]),
                        "slot": int(slot["slot"]),
                        "lesson": int(rendered["id"]),
                        "teacher": int(rendered["teacher_id"]),
                        "subgroup": int(rendered["subgroup"]),
                        "room": int(rendered["room_id"]),
                        "room_name": str(rendered.get("room_name", "")),
                        "name": str(rendered.get("name", "")),
                        "is_lab": bool(rendered.get("is_lab", False)),
                    })

    errors: list[str] = []
    if len(events) != 349:
        errors.append(f"events={len(events)}, expected=349")
    if {event["date"] for event in events} - {"2026-09-04", "2026-09-05"}:
        errors.append("schedule contains dates outside Friday/Saturday")

    for teacher_id, name in ((44, "Письмак"), (66, "Колтышев"), (65, "Михайлова")):
        count = sum(event["teacher"] == teacher_id for event in events)
        if count:
            errors.append(f"{name}: scheduled events={count}")
    bad_semenova = [event for event in events if event["teacher"] == 10 and event["date"] == "2026-09-05" and event["slot"] > 4]
    if bad_semenova:
        errors.append(f"Семенова после 4 пары в субботу: {bad_semenova}")
    bad_korobkova = [event for event in events if event["teacher"] == 39 and (event["date"] != "2026-09-04" or event["slot"] > 4)]
    if bad_korobkova:
        errors.append(f"Коробкова вне пятницы 1-4: {bad_korobkova}")

    teacher_slots: Counter[tuple[str, int, int]] = Counter()
    room_slots: Counter[tuple[str, int, int]] = Counter()
    part_slots: Counter[tuple[str, int, int, int]] = Counter()
    student_days: defaultdict[tuple[int, int, str], set[int]] = defaultdict(set)
    for event in events:
        teacher_slots[(event["date"], event["slot"], event["teacher"])] += 1
        room_slots[(event["date"], event["slot"], event["room"])] += 1
        parts = (0, 1) if event["subgroup"] == -1 else (event["subgroup"] - event["group"] * 2,)
        for part in parts:
            part_slots[(event["date"], event["slot"], event["group"], part)] += 1
            student_days[(event["group"], part, event["date"])].add(event["slot"])
    if any(count > 1 for count in teacher_slots.values()):
        errors.append("teacher conflicts found")
    if any(count > 1 for count in room_slots.values()):
        errors.append("room conflicts found")
    if any(count > 1 for count in part_slots.values()):
        errors.append("student subgroup conflicts found")
    for key, slots in student_days.items():
        if slots and len(slots) != max(slots) - min(slots) + 1:
            errors.append(f"student window {key}: {sorted(slots)}")

    for event in events:
        room = rooms[event["room"]]
        campus = int(room["campus"])
        is_lpz = "лпз" in event["name"].casefold()
        if event["teacher"] == 31 and campus != 1:
            errors.append(f"Ланитина не на Кривоусова: {event}")
        if event["teacher"] == 46 and campus != 1:
            errors.append(f"Черепанова не на Кривоусова: {event}")
        if event["date"] == "2026-09-05" and event["teacher"] == 55:
            if is_lpz and event["room_name"] != "120-121":
                errors.append(f"ЛПЗ Лимоновой не в 120-121: {event}")
            if not is_lpz and (campus != 0 or room.get("access_mode") != "general" or event["room_name"] == "210"):
                errors.append(f"Теория Лимоновой не в обычной аудитории Лесной: {event}")
        if event["date"] == "2026-09-05" and event["teacher"] == 59:
            if is_lpz and event["room_name"] not in {"115-116", "122-123"}:
                errors.append(f"ЛПЗ Самцова не в мастерской: {event}")
            if not is_lpz and (campus != 0 or room.get("access_mode") != "general" or event["room_name"] == "210"):
                errors.append(f"Теория Самцова не в обычной аудитории Лесной: {event}")
        if event["date"] == "2026-09-05" and event["teacher"] == 49 and is_lpz and event["room_name"] != "ЦПДЭ":
            errors.append(f"ЛПЗ Кальчевской не в ЦПДЭ: {event}")
        if event["room_name"] == "210" and event["teacher"] != 47:
            errors.append(f"В 210 не Азарян: {event}")
        if event["room"] < 0:
            errors.append(f"unassigned room: {event}")

    occurrences: defaultdict[int, defaultdict[str, list[int]]] = defaultdict(lambda: defaultdict(list))
    for event in events:
        occurrences[event["lesson"]][event["date"]].append(event["slot"])
    for lesson_id, by_date in occurrences.items():
        lesson = lessons[lesson_id]
        group_name = str(groups[int(lesson["group"])]["name"])
        if not bool(lesson.get("is_lab", False)):
            continue
        needs_double = group_name.startswith(("ТАКХС", "ТОиРА", "ТОРД", "СП-"))
        if needs_double:
            expected = int(lesson.get("consecutive_pairs", 1))
            if expected != 2:
                errors.append(f"{group_name} ЛПЗ lesson {lesson_id}: consecutive_pairs={expected}")
            for date_iso, slots in by_date.items():
                slots = sorted(slots)
                if len(slots) != 2 or slots[1] != slots[0] + 1:
                    errors.append(f"{group_name} ЛПЗ lesson {lesson_id} not double on {date_iso}: {slots}")
        if group_name.startswith("ПКД"):
            for date_iso, slots in by_date.items():
                slots = sorted(slots)
                if 2 in slots and 3 in slots:
                    errors.append(f"ПКД ЛПЗ разорвано обедом: lesson {lesson_id}, {date_iso}, {slots}")

    summary = {
        "passed": not errors,
        "events": len(events),
        "student_windows": sum(1 for value in errors if value.startswith("student window")),
        "koltyshev": sum(event["teacher"] == 66 for event in events),
        "pismak": sum(event["teacher"] == 44 for event in events),
        "mikhailova": sum(event["teacher"] == 65 for event in events),
        "semenova_saturday_slots": sorted({event["slot"] for event in events if event["teacher"] == 10 and event["date"] == "2026-09-05"}),
        "errors": errors,
    }
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    if errors:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
