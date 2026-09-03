"""Apply auditorium-fund ownership, access and teacher-campus rules.

The source Word order is data, not an instruction set.  The room inventory has
already been transcribed to timetable_data.json; this migration turns the
transcribed responsible_note values into solver-readable teacher ids.
"""

from __future__ import annotations

import json
import re
import shutil
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "timetable_data.json"
HISTORY = ROOT / "data" / "history"

# These are not teaching rooms, even though they exist in the source order.
BLOCKED_ROOM_IDS = {0, 1, 26, 50, 62}  # 1а, 1б, 52, Тренажёрный зал, Актовый зал

# Workshop rooms are not part of the general pool.  The responsible teacher is
# read from responsible_note; an empty id list deliberately makes the room
# unavailable when that employee is absent from the current teacher roster.
EXCLUSIVE_ROOM_IDS = {63, 64, 65, 66}  # 124, 115-116, 122-123, 120-121

SPORTS_ROOM_IDS = {22, 42, 67}  # Кривоусова: Спортзал; Лесная: 330 и Спортзал

# Dispatcher-confirmed overrides have priority over the historical order.
STRICT_CAMPUS_BY_NAME = {
    "Цимфер Татьяна Ивановна": [1],
    "Силенок Марина Юрьевна": [1],
    "Нуров Мирзо Нуралиевич": [0],
    "Кобылянская Татьяна Александровна": [1],
}


def letters(value: str) -> str:
    return re.sub(r"[^а-яa-z0-9]", "", value.casefold().replace("ё", "е"))


def teacher_signature(name: str) -> str:
    parts = [part for part in re.split(r"\s+", name.strip()) if part]
    if not parts:
        return ""
    initials = "".join(part[0] for part in parts[1:3] if part)
    return letters(parts[0] + initials)


def note_signatures(note: str) -> list[str]:
    result: list[str] = []
    for chunk in re.split(r"[,;]", note):
        chunk = chunk.strip()
        if not chunk or chunk.casefold() in {"-", "не используется"}:
            continue
        parts = [part for part in re.split(r"\s+", chunk) if part]
        if not parts:
            continue
        # Full name and the common "Фамилия И.О." notation both collapse to
        # the same surname+initials signature.
        surname = parts[0]
        if len(parts) >= 3 and all(len(part) > 1 for part in parts[1:3]):
            initials = parts[1][0] + parts[2][0]
        else:
            initials = "".join(re.findall(r"[А-Яа-яЁёA-Za-z]", "".join(parts[1:])))[:2]
        result.append(letters(surname + initials))
    return result


def main() -> None:
    source = json.loads(DATA.read_text(encoding="utf-8-sig"))
    HISTORY.mkdir(parents=True, exist_ok=True)
    backup = HISTORY / f"before_room_access_policies_{datetime.now():%Y%m%d-%H%M%S}.json"
    shutil.copy2(DATA, backup)

    teachers = source.get("teachers", [])
    teachers_by_id = {int(item["id"]): item for item in teachers}
    signature_to_ids: dict[str, list[int]] = {}
    for teacher in teachers:
        signature_to_ids.setdefault(teacher_signature(str(teacher.get("name", ""))), []).append(int(teacher["id"]))

    unresolved: dict[str, list[str]] = {}
    rooms_by_id: dict[int, dict] = {}
    for room in source.get("rooms", []):
        room_id = int(room["id"])
        rooms_by_id[room_id] = room
        owner_ids: list[int] = []
        for signature in note_signatures(str(room.get("responsible_note", ""))):
            matches = signature_to_ids.get(signature, [])
            if len(matches) == 1:
                owner_ids.append(matches[0])
            elif signature:
                unresolved.setdefault(str(room.get("name", room_id)), []).append(signature)
        room["responsible_teacher_ids"] = sorted(set(owner_ids))
        room["purpose"] = "sports_hall" if room_id in SPORTS_ROOM_IDS else ""
        if room_id in BLOCKED_ROOM_IDS:
            room["access_mode"] = "blocked"
            room["active"] = False
        elif room_id in EXCLUSIVE_ROOM_IDS:
            room["access_mode"] = "exclusive"
            room["active"] = True
        else:
            room["access_mode"] = "general"

    # Диспетчерское правило физкультуры: спортивные залы являются отдельным
    # фондом и доступны только преподавателям физической культуры.
    sports_owners = {22: {7, 8, 72}, 42: {8, 34}, 67: {34}}
    for room_id, teacher_ids in sports_owners.items():
        room = rooms_by_id[room_id]
        room["access_mode"] = "exclusive"
        room["active"] = True
        room["responsible_teacher_ids"] = sorted(teacher_ids)

    owned_campuses: dict[int, set[int]] = {teacher_id: set() for teacher_id in teachers_by_id}
    for room in rooms_by_id.values():
        if not bool(room.get("active", True)) or room.get("access_mode") == "blocked":
            continue
        for teacher_id in room.get("responsible_teacher_ids", []):
            owned_campuses.setdefault(int(teacher_id), set()).add(int(room.get("campus", 0)))

    restricted_teachers = 0
    for teacher in teachers:
        teacher_id = int(teacher["id"])
        name = str(teacher.get("name", ""))
        campuses = set(owned_campuses.get(teacher_id, set()))
        default_room = rooms_by_id.get(int(teacher.get("default_room", -1)))
        if default_room and default_room.get("access_mode") != "blocked" and bool(default_room.get("active", True)):
            campuses.add(int(default_room.get("campus", 0)))
        allowed = STRICT_CAMPUS_BY_NAME.get(name, sorted(campuses))
        teacher["allowed_campuses"] = list(allowed)
        if allowed:
            restricted_teachers += 1
            priority = [int(value) for value in teacher.get("campus_priority", []) if int(value) in (0, 1)]
            default_campus = []
            if default_room and default_room.get("access_mode") != "blocked" and bool(default_room.get("active", True)):
                default_campus = [int(default_room.get("campus", 0))]
            teacher["campus_priority"] = list(dict.fromkeys([*default_campus, *priority, *allowed]))

    for lesson in source.get("lessons", []):
        teacher = teachers_by_id.get(int(lesson.get("teacher", -1)))
        allowed = list(teacher.get("allowed_campuses", [])) if teacher else []
        lesson["allowed_campuses"] = allowed or [0, 1]
        # A teacher room is a strong preference in the allocator, not a fixed
        # room request.  Explicit dispatcher fixes can be added later per lesson.
        lesson["fixed_room"] = -1
        lesson["required_room_purpose"] = (
            "sports_hall" if str(lesson.get("name", "")).strip().casefold() == "физическая культура" else ""
        )

    source.setdefault("meta", {})["room_access_policy"] = {
        "applied_at": datetime.now().isoformat(timespec="seconds"),
        "source": "Аудиторный фонд 2025-2026 + подтверждения диспетчера",
        "blocked_room_ids": sorted(BLOCKED_ROOM_IDS),
        "exclusive_room_ids": sorted(EXCLUSIVE_ROOM_IDS),
        "restricted_teachers": restricted_teachers,
        "backup": str(backup),
    }

    temp = DATA.with_suffix(DATA.suffix + ".tmp")
    temp.write_text(json.dumps(source, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temp.replace(DATA)
    print(json.dumps({
        "backup": str(backup),
        "blocked": [{"id": room_id, "name": rooms_by_id[room_id]["name"]} for room_id in sorted(BLOCKED_ROOM_IDS)],
        "exclusive": [{
            "id": room_id,
            "name": rooms_by_id[room_id]["name"],
            "teacher_ids": rooms_by_id[room_id]["responsible_teacher_ids"],
        } for room_id in sorted(EXCLUSIVE_ROOM_IDS)],
        "restricted_teachers": restricted_teachers,
        "unresolved_responsible_notes": unresolved,
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
