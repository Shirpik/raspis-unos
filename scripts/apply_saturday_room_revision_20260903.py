#!/usr/bin/env python3
"""Reassign Saturday rooms under the LPZ-name policy, preserving Friday exactly."""

from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SATURDAY = "2026-09-05"
TARGET_TEACHERS = {49, 55, 59}


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def iter_events(schedule: dict):
    for group in schedule.get("groups", []):
        for day in group.get("days", []):
            for slot in day.get("slots", []):
                for lesson in slot.get("lessons", []):
                    yield group, day, slot, lesson


def is_lpz(name: str) -> bool:
    return "лпз" in name.casefold()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--schedule", type=Path, required=True)
    parser.add_argument("--data", type=Path, default=ROOT / "data" / "timetable_data.json")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    schedule = read_json(args.schedule)
    data = read_json(args.data)
    rooms = {int(item["id"]): item for item in data.get("rooms", [])}
    groups = {int(item["id"]): item for item in data.get("groups", [])}
    before_friday = [
        (int(group["group_index"]), day)
        for group in schedule.get("groups", [])
        for day in group.get("days", [])
        if day.get("date_iso") == "2026-09-04"
    ]
    before_friday_json = json.dumps(before_friday, ensure_ascii=False, sort_keys=True)

    occupied: defaultdict[tuple[str, int, int], set[int]] = defaultdict(set)
    for _, day, slot, lesson in iter_events(schedule):
        room_id = int(lesson.get("room_id", -1))
        if room_id >= 0:
            occupied[(str(day.get("date_iso", "")), int(slot["slot"]), room_id)].add(
                int(lesson.get("id", -1))
            )

    changes = []
    for group, day, slot, rendered in iter_events(schedule):
        if day.get("date_iso") != SATURDAY:
            continue
        teacher = int(rendered.get("teacher_id", -1))
        if teacher not in TARGET_TEACHERS:
            continue
        lpz = is_lpz(str(rendered.get("name", "")))
        if teacher == 49 and not lpz:
            continue
        if teacher == 49:
            candidates = [68]
        elif teacher == 55 and lpz:
            candidates = [66]
        elif teacher == 59 and lpz:
            candidates = [64, 65]
        else:
            candidates = [
                room_id for room_id, room in rooms.items()
                if int(room.get("campus", -1)) == 0
                and room.get("name") != "210"
                and room.get("access_mode", "general") == "general"
                and bool(room.get("active", True))
            ]

        current = int(rendered.get("room_id", -1))
        pair = int(slot["slot"])
        lesson_id = int(rendered["id"])
        if current in candidates:
            continue
        required_capacity = int(groups[int(group["group_index"])].get("size", 0))
        free = [
            room_id for room_id in candidates
            if not (occupied[(SATURDAY, pair, room_id)] - {lesson_id})
            and (int(rooms[room_id].get("capacity", 0)) <= 0
                 or int(rooms[room_id].get("capacity", 0)) >= required_capacity)
        ]
        if not free:
            raise SystemExit(
                f"No policy-compliant room for lesson {lesson_id} at Saturday pair {pair}"
            )
        free.sort(key=lambda room_id: (
            0 if int(rooms[room_id].get("capacity", 0)) == required_capacity else 1,
            int(rooms[room_id].get("capacity", 0)) or 100000,
            room_id,
        ))
        chosen = free[0]
        occupied[(SATURDAY, pair, current)].discard(lesson_id)
        occupied[(SATURDAY, pair, chosen)].add(lesson_id)
        rendered.update({
            "room_id": chosen,
            "room_name": str(rooms[chosen]["name"]),
            "room_type": int(rooms[chosen].get("room_type", 0)),
            "room_substituted": False,
            "requested_room_id": None,
            "requested_room_name": None,
            "room_substitution_reason": None,
        })
        changes.append({
            "lesson": lesson_id,
            "teacher": teacher,
            "pair": pair,
            "from": current,
            "to": chosen,
        })

    after_friday = [
        (int(group["group_index"]), day)
        for group in schedule.get("groups", [])
        for day in group.get("days", [])
        if day.get("date_iso") == "2026-09-04"
    ]
    after_friday_json = json.dumps(after_friday, ensure_ascii=False, sort_keys=True)
    if before_friday_json != after_friday_json:
        raise SystemExit("Friday changed while applying the Saturday-only revision")

    write_json(args.output, schedule)
    print(json.dumps({
        "friday_unchanged": True,
        "saturday_room_changes": changes,
        "output": str(args.output.resolve()),
    }, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
