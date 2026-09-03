"""Measure how often scheduled lessons use their teacher's default room."""

from __future__ import annotations

import json
from collections import defaultdict
from pathlib import Path
from urllib.request import urlopen


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    data = json.loads((ROOT / "data" / "timetable_data.json").read_text(encoding="utf-8-sig"))
    with urlopen("http://127.0.0.1:8080/api/schedule") as response:
        schedule = json.load(response)

    teachers = {int(item["id"]): item for item in data.get("teachers", [])}
    rooms = {int(item["id"]): item for item in data.get("rooms", [])}
    stats = defaultdict(lambda: {"eligible": 0, "hits": 0, "rooms": set()})
    total_preferred = eligible = hits = 0
    nurov = []

    for group in schedule.get("groups", []):
        for day in group.get("days", []):
            for slot in day.get("slots", []):
                for lesson in slot.get("lessons", []):
                    teacher = teachers.get(int(lesson.get("teacher_id", -1)))
                    assigned = rooms.get(int(lesson.get("room_id", -1)))
                    if not teacher or not assigned or int(teacher.get("default_room", -1)) < 0:
                        continue
                    default = rooms.get(int(teacher["default_room"]))
                    if not default:
                        continue
                    total_preferred += 1
                    row = stats[teacher["name"]]
                    row["rooms"].add(f"{assigned['name']}@{assigned['campus']}")
                    if int(default.get("campus", -1)) == int(assigned.get("campus", -2)):
                        eligible += 1
                        row["eligible"] += 1
                        if int(default["id"]) == int(assigned["id"]):
                            hits += 1
                            row["hits"] += 1
                    if teacher["name"] == "Нуров Мирзо Нуралиевич":
                        nurov.append({
                            "date": day.get("date"), "slot": slot.get("slot"),
                            "group": group.get("group_name"), "room": assigned["name"],
                            "campus": assigned["campus"],
                        })

    lowest = []
    for name, row in stats.items():
        if not row["eligible"]:
            continue
        lowest.append({
            "teacher": name,
            "eligible": row["eligible"],
            "hits": row["hits"],
            "percent": round(100 * row["hits"] / row["eligible"], 1),
            "rooms": sorted(row["rooms"]),
        })
    lowest.sort(key=lambda item: (item["percent"], item["teacher"]))
    print(json.dumps({
        "events_with_teacher_room": total_preferred,
        "eligible_same_campus": eligible,
        "own_room_hits": hits,
        "hit_percent": round(100 * hits / eligible, 1) if eligible else 0,
        "lowest": lowest[:15],
        "nurov": nurov,
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
