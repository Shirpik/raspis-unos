"""Apply the temporary classroom restrictions supplied for 02.09.2026.

Rooms 41 and 42 are unavailable for the full day.  The remaining listed
classrooms are available only from 17:00 to 19:30; with the current bell
schedule only pair 6 (17:05–18:25) fits completely in that interval.
"""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "timetable_data.json"
FULL_DAY = {"41", "42"}
EVENING_ONLY = {"34", "23", "28", "27", "37", "18/1", "2", "3"}


def main() -> None:
    value = json.loads(DATA.read_text(encoding="utf-8-sig"))
    changed = []
    for room in value.get("rooms", []):
        name = str(room.get("name", "")).strip()
        if name in FULL_DAY:
            room["active"] = False
            room.pop("available_slots", None)
            room["availability_note"] = "Занят целый день (ограничение от 31.08.2026)"
            changed.append({"room": name, "active": False, "available_slots": []})
        elif name in EVENING_ONLY:
            room["active"] = True
            room["available_slots"] = [6]
            room["availability_note"] = "Свободен 17:00–19:30; подходит 6-я пара 17:05–18:25"
            changed.append({"room": name, "active": True, "available_slots": [6]})

    value.setdefault("meta", {})["room_restrictions_2026_09_02"] = {
        "source": "Фотография пользователя от 31.08.2026",
        "whole_day_unavailable": sorted(FULL_DAY),
        "available_17_00_19_30": sorted(EVENING_ONLY),
        "mapped_pair_slots": [6],
    }
    temporary = DATA.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temporary.replace(DATA)
    print(json.dumps({"changed": changed}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
