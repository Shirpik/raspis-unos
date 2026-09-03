"""Apply dispatcher-confirmed sports-room rules without generating a schedule."""

from __future__ import annotations

import json
import shutil
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "timetable_data.json"
HISTORY = ROOT / "data" / "history"

BLOCKED_ROOM_IDS = {0, 1}  # 1а and 1б
SPORTS_ROOMS = {
    22: {7, 8, 72},  # Кривоусова: Кобылянская, Потапова, Бурдин
    42: {8, 34},     # Лесная: Потапова, Нуров
    67: {34},        # Лесная: Нуров
}


def main() -> None:
    source = json.loads(DATA.read_text(encoding="utf-8-sig"))
    HISTORY.mkdir(parents=True, exist_ok=True)
    backup = HISTORY / f"before_sports_room_rules_{datetime.now():%Y%m%d-%H%M%S}.json"
    shutil.copy2(DATA, backup)

    rooms = {int(room["id"]): room for room in source.get("rooms", [])}
    for room in rooms.values():
        room["purpose"] = ""
    for room_id in BLOCKED_ROOM_IDS:
        room = rooms[room_id]
        room["access_mode"] = "blocked"
        room["active"] = False
    for room_id, teacher_ids in SPORTS_ROOMS.items():
        room = rooms[room_id]
        room["purpose"] = "sports_hall"
        room["access_mode"] = "exclusive"
        room["active"] = True
        room["responsible_teacher_ids"] = sorted(teacher_ids)

    teachers = {int(teacher["id"]): teacher for teacher in source.get("teachers", [])}
    kobylyanskaya = teachers[7]
    kobylyanskaya["default_room"] = 22
    kobylyanskaya["allowed_campuses"] = [1]
    kobylyanskaya["campus_priority"] = [1]
    kobylyanskaya["room_responsibility"] = "Спортзал (Кривоусова, 53)"

    sports_lessons = 0
    for lesson in source.get("lessons", []):
        is_sports = str(lesson.get("name", "")).strip().casefold() == "физическая культура"
        lesson["required_room_purpose"] = "sports_hall" if is_sports else ""
        if not is_sports:
            continue
        sports_lessons += 1
        teacher = teachers.get(int(lesson.get("teacher", -1)))
        allowed = list(teacher.get("allowed_campuses", [])) if teacher else []
        lesson["allowed_campuses"] = allowed or [0, 1]
        lesson["fixed_room"] = -1

    source.setdefault("meta", {})["sports_room_policy"] = {
        "applied_at": datetime.now().isoformat(timespec="seconds"),
        "blocked_room_ids": sorted(BLOCKED_ROOM_IDS),
        "sports_room_ids": sorted(SPORTS_ROOMS),
        "sports_lessons": sports_lessons,
        "kobylyanskaya_campus": 1,
        "backup": str(backup),
    }

    temp = DATA.with_suffix(DATA.suffix + ".tmp")
    temp.write_text(json.dumps(source, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temp.replace(DATA)
    print(json.dumps(source["meta"]["sports_room_policy"], ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
