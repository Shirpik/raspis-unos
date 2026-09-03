"""Idempotently add the Lesnaya gym and assign it to Nurov."""

from __future__ import annotations

import hashlib
import json
import shutil
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "timetable_data.json"
HISTORY = ROOT / "data" / "history"
TEACHER_NAME = "Нуров Мирзо Нуралиевич"


def main() -> None:
    source = json.loads(DATA.read_text(encoding="utf-8-sig"))
    HISTORY.mkdir(parents=True, exist_ok=True)
    backup = HISTORY / f"before_add_lesnaya_gym_{datetime.now():%Y%m%d-%H%M%S}.json"
    shutil.copy2(DATA, backup)

    rooms = source.setdefault("rooms", [])
    room = next((item for item in rooms
                 if item.get("name") == "Спортзал" and int(item.get("campus", -1)) == 0), None)
    if room is None:
        room_id = max((int(item.get("id", -1)) for item in rooms), default=-1) + 1
        token = hashlib.sha1("Лесная|Спортзал".encode("utf-8")).hexdigest()[:16]
        room = {
            "id": room_id,
            "uid": f"room-{token}",
            "name": "Спортзал",
            "campus": 0,
            "capacity": 0,
            "room_type": 0,
            "equipment": [],
            "active": True,
            "description": "Спортивный зал",
            "source": "Ручное добавление",
            "responsible_note": TEACHER_NAME,
        }
        rooms.append(room)
    else:
        room["active"] = True
        room["room_type"] = 0
        room["equipment"] = []
        room["responsible_note"] = TEACHER_NAME

    teacher = next((item for item in source.get("teachers", []) if item.get("name") == TEACHER_NAME), None)
    if teacher is None:
        raise SystemExit(f"Teacher not found: {TEACHER_NAME}")
    teacher["default_room"] = int(room["id"])
    teacher["campus_priority"] = [0, 1]
    responsibilities = [part.strip() for part in str(teacher.get("room_responsibility", "")).split(";") if part.strip()]
    gym_label = "Спортзал (Лесная, 1)"
    responsibilities = [part for part in responsibilities if part != gym_label]
    teacher["room_responsibility"] = "; ".join([gym_label, *responsibilities])

    temp = DATA.with_suffix(DATA.suffix + ".tmp")
    temp.write_text(json.dumps(source, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temp.replace(DATA)
    print(json.dumps({
        "backup": str(backup),
        "room_id": room["id"],
        "room": room["name"],
        "campus": "Лесная",
        "teacher": TEACHER_NAME,
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
