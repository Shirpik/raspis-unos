"""Import the 2025/26 auditorium fund from the supplied legacy Word document."""

from __future__ import annotations

import copy
import datetime as dt
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

from extract_legacy_doc import extract

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "timetable_data.json"
HISTORY = ROOT / "data" / "history"


def compact(value):
    return re.sub(r"\s+", " ", str(value or "")).strip()


def normalized(value):
    return compact(value).casefold().replace("ё", "е")


def stable_uid(value):
    return "room-" + hashlib.sha256(value.encode("utf-8")).hexdigest()[:16]


def initials_key(value):
    match = re.fullmatch(r"([А-ЯЁ][А-Яа-яЁё-]+)\s+([А-ЯЁ])\.\s*([А-ЯЁ])\.?", compact(value))
    return (normalized(match.group(1)), match.group(2).casefold(), match.group(3).casefold()) if match else None


def full_name_key(value):
    words = compact(value).split()
    if len(words) < 3:
        return None
    return normalized(words[0]), normalized(words[1])[0], normalized(words[2])[0]


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Укажите путь к файлу .doc")
    source = Path(sys.argv[1])
    text = extract(source)
    start = text.index("1 корпус, ул. Кривоусова, 53")
    end = text.index("Директор", start)
    parts = re.split(r"\t{2,}(?=\d+\.?\t)", text[start:end])[1:]
    if len(parts) != 67:
        raise RuntimeError(f"Ожидалось 67 помещений, распознано {len(parts)}")

    current = json.loads(DATA.read_text(encoding="utf-8-sig"))
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    HISTORY.mkdir(parents=True, exist_ok=True)
    backup = HISTORY / f"before_auditorium_import_{stamp}.json"
    shutil.copy2(DATA, backup)

    teachers_by_key = {}
    for teacher in current.get("teachers", []):
        key = full_name_key(teacher.get("name"))
        if key:
            teachers_by_key[key] = teacher
        teacher["room_responsibility"] = ""
        teacher["default_room"] = -1

    rooms = []
    linked_teachers = set()
    initials_pattern = re.compile(r"[А-ЯЁ][А-Яа-яЁё-]+\s+[А-ЯЁ]\.\s*[А-ЯЁ]\.?$")
    for index, part in enumerate(parts):
        fields = [compact(value) for value in part.split("\t") if compact(value)]
        # 29 rooms on Krivousova, 22 on Lesnaya, 12 workshops on Krivousova,
        # 4 workshops on Lesnaya — exactly as ordered in the source tables.
        campus = 1 if index < 29 or 51 <= index < 63 else 0
        room_no = fields[1] if len(fields) > 1 else ""
        description = fields[2] if len(fields) > 2 else "Помещение"
        if not room_no:
            room_no = description
        responsible_fields = fields[3:]
        if initials_pattern.fullmatch(description):
            responsible_fields = fields[2:]
            description = room_no
        room = {
            "id": index, "uid": stable_uid(f"{campus}:{room_no}"), "name": room_no,
            # Специализация из приказа является описанием ответственности, а не
            # ограничением для расписания. Автоподбор учитывает корпус,
            # доступность и закрепление преподавателя, но не тип/оборудование.
            "campus": campus, "capacity": 0, "room_type": 0,
            "equipment": [],
            "active": True, "description": description,
            "source": "Приказ № 322 л/с от 01.09.2025",
        }
        responsible = []
        for field in responsible_fields:
            candidate = compact(field)
            if not initials_pattern.fullmatch(candidate):
                continue
            responsible.append(candidate)
            key = initials_key(candidate)
            teacher = teachers_by_key.get(key)
            if not teacher:
                continue
            linked_teachers.add(teacher["id"])
            if teacher.get("default_room", -1) < 0:
                teacher["default_room"] = index
            note = f"{room_no} ({'Кривоусова, 53' if campus == 1 else 'Лесная, 1'})"
            old_note = teacher.get("room_responsibility", "")
            teacher["room_responsibility"] = f"{old_note}; {note}".strip("; ")
        room["responsible_note"] = ", ".join(responsible)
        rooms.append(room)

    rooms_by_key = {(room["campus"], normalized(room["name"])): room for room in rooms}
    teachers_by_name = {normalized(teacher.get("name")): teacher for teacher in current.get("teachers", [])}

    # Актуальные назначения, подтверждённые диспетчером 31.08.2026. Они имеют
    # приоритет над приказом 2025/26: 409 — Михайлова Т.А., 417 —
    # Гарбузов А.Е.; кабинет 411 остаётся как в приказе.
    ownership_overrides = {
        (0, "204"): "Меренчуков Иван Александрович",
        (0, "409"): "Михайлова Татьяна Алексеевна",
        (0, "417"): "Гарбузов Андрей Евгеньевич",
    }

    override_room_ids = set()
    for (campus, room_name), teacher_name in ownership_overrides.items():
        room = rooms_by_key.get((campus, normalized(room_name)))
        if not room:
            raise RuntimeError(f"Не найден кабинет для актуального закрепления: {room_name}, корпус {campus}")
        override_room_ids.add(room["id"])
        room["responsible_note"] = teacher_name

    # Убираем старые связи преподавателей с переопределёнными кабинетами, затем
    # назначаем актуальные кабинеты как основные.
    for teacher in current.get("teachers", []):
        if teacher.get("default_room") in override_room_ids:
            teacher["default_room"] = -1
        notes = []
        for note in str(teacher.get("room_responsibility") or "").split("; "):
            if not any(note.startswith(f"{rooms[room_id]['name']} (") for room_id in override_room_ids):
                if note:
                    notes.append(note)
        teacher["room_responsibility"] = "; ".join(notes)

    for (campus, room_name), teacher_name in ownership_overrides.items():
        if not teacher_name:
            continue
        teacher = teachers_by_name.get(normalized(teacher_name))
        room = rooms_by_key[(campus, normalized(room_name))]
        if not teacher:
            raise RuntimeError(f"Не найден преподаватель для актуального закрепления: {teacher_name}")
        teacher["default_room"] = room["id"]
        teacher["campus_priority"] = [campus, 1 - campus]
        teacher["room_responsibility"] = f"{room_name} ({'Кривоусова, 53' if campus == 1 else 'Лесная, 1'})"

    # Площадка закреплённого кабинета становится первым мягким приоритетом
    # преподавателя. Решатель всё ещё может выбрать второй корпус, но только
    # когда этого требуют ограничения группы или доступность.
    for teacher in current.get("teachers", []):
        room_id = teacher.get("default_room", -1)
        if isinstance(room_id, int) and 0 <= room_id < len(rooms):
            campus = rooms[room_id]["campus"]
            teacher["campus_priority"] = [campus, 1 - campus]

    # Кабинет 52 не используется ни как основной, ни как автоматическая замена.
    room_52 = rooms_by_key.get((1, "52"))
    if room_52:
        room_52["active"] = False
        room_52["responsible_note"] = "Не используется"
        room_52["unavailable_reason"] = "Нерабочий кабинет"

    # Старые требования к типу и оборудованию больше не участвуют в подборе.
    for lesson in current.get("lessons", []):
        lesson["required_room_type"] = 0
        lesson["required_equipment"] = []
        lesson["required_capacity"] = 0

    current["rooms"] = rooms
    current.setdefault("meta", {})["auditorium_fund"] = {
        "source": source.name, "order": "№ 322 л/с", "dated": "2025-09-01",
        "imported_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "rooms": len(rooms), "teachers_linked": len(linked_teachers), "backup": backup.name,
    }
    tmp = DATA.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(current, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    tmp.replace(DATA)
    print(json.dumps({
        "ok": True, "rooms": len(rooms), "active_rooms": sum(x["active"] for x in rooms),
        "specialization_constraints": False,
        "teachers_linked": len(linked_teachers), "backup": str(backup),
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
