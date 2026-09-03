"""Enable stable teacher-room allocation without automatic replacements.

The teacher's default room stays the first allocation preference, but it is not
copied into every lesson as a hard room constraint.  The allocator therefore
chooses one stable room per teacher and campus after the timetable is solved.
Only an explicitly fixed lesson room can ever be reported as a replacement.
"""

from __future__ import annotations

import json
import shutil
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "timetable_data.json"
HISTORY = ROOT / "data" / "history"


def main() -> None:
    source = json.loads(DATA.read_text(encoding="utf-8-sig"))
    HISTORY.mkdir(parents=True, exist_ok=True)
    backup = HISTORY / f"before_strict_teacher_rooms_{datetime.now():%Y%m%d-%H%M%S}.json"
    shutil.copy2(DATA, backup)

    rooms = {int(room["id"]): room for room in source.get("rooms", [])}
    teachers = {int(teacher["id"]): teacher for teacher in source.get("teachers", [])}

    # Подтверждённые диспетчером площадки. Эти ограничения важнее любых
    # прежних автоматических подстановок.
    strict_campuses = {
        "Нуров Мирзо Нуралиевич": 0,       # Лесная, спортзал
        "Силенок Марина Юрьевна": 1,      # только Кривоусова, 53
    }

    preferred = 0
    without_preference = 0
    disabled_defaults: list[str] = []
    for lesson in source.get("lessons", []):
        lesson["allow_room_substitution"] = False
        teacher = teachers.get(int(lesson.get("teacher", -1)))
        room_id = int(teacher.get("default_room", -1)) if teacher else -1
        room = rooms.get(room_id)
        lesson["fixed_room"] = -1
        strict = strict_campuses.get(str(teacher.get("name", ""))) if teacher else None
        if strict is not None:
            lesson["allowed_campuses"] = [strict]
            teacher["campus_priority"] = [strict]
        elif room is not None and bool(room.get("active", True)):
            # Закреплённый кабинет — главный приоритет распределителя. Площадка
            # остаётся soft для общего фонда: одновременная hard-фиксация всех
            # преподавателей делает подтверждённую недельную нагрузку
            # математически несовместимой. Персональные strict-исключения выше
            # при этом не ослабляются.
            lesson["allowed_campuses"] = [0, 1]
            teacher["campus_priority"] = [int(room.get("campus", 0))]
        else:
            lesson["allowed_campuses"] = [0, 1]
        if room is not None and bool(room.get("active", True)):
            preferred += 1
        else:
            without_preference += 1
            if teacher and room_id >= 0:
                disabled_defaults.append(str(teacher.get("name", teacher["id"])))

    source.setdefault("meta", {})["room_generation_mode"] = {
        "mode": "stable_teacher_rooms",
        "auto_substitution": False,
        "applied_at": datetime.now().isoformat(timespec="seconds"),
        "lessons_with_teacher_room_preference": preferred,
        "lessons_without_teacher_room_preference": without_preference,
        "backup": str(backup),
    }

    temp = DATA.with_suffix(DATA.suffix + ".tmp")
    temp.write_text(json.dumps(source, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temp.replace(DATA)
    print(json.dumps({
        "backup": str(backup),
        "lessons_with_teacher_room_preference": preferred,
        "lessons_without_teacher_room_preference": without_preference,
        "disabled_teacher_rooms": sorted(set(disabled_defaults)),
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
