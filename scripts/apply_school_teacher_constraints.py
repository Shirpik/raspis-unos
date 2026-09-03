#!/usr/bin/env python3
"""Apply semester-long school timetable constraints from the 31.08.2026 photos."""

from __future__ import annotations

import copy
import hashlib
import json
import shutil
from collections import defaultdict
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "timetable_data.json"
HISTORY = ROOT / "data" / "history"
REPORT_DIR = ROOT / "build" / "reports"

# Filled school lesson -> blocked college pair: 1-2 => 1, 3-4 => 2,
# 5-6 => 3, 7 => 4. The transcription is stored at pair level because any
# one filled school lesson makes the entire overlapping college pair unusable.
BUSY = {
    "Аринкина Татьяна Юрьевна": {1:[2,3],2:[1,2,3],3:[3],4:[1,2,3],5:[2,3],6:[1,2,3]},
    "Бородай Светлана Александровна": {1:[1,2,3],2:[1,2,3],3:[1,2,3],4:[1,2,3],5:[1,2,3]},
    "Шадрина Елена Федоровна": {1:[1,2,3],2:[2,3],3:[1,2,3],4:[1,2],5:[1,2,3],6:[1,2,3]},
    "Жилина Наталья Владимировна": {1:[1,2,3],2:[1,2,3],3:[1,2,3],4:[1,2,3],5:[1,2,3,4],6:[1,2,3]},
    "Дроговейко Дарья Юрьевна": {d:[1,2,3] for d in range(1,7)},
    "Цимфер Татьяна Ивановна": {3:[1,2],4:[1],5:[1]},
    "Нуриахметова Надежда Сергеевна": {2:[2,3],3:[1,2,3],4:[1,2,3],5:[1,2,3]},
    "Силенок Марина Юрьевна": {1:[1,2,3],2:[2],3:[1,2],4:[3],5:[1,2,3],6:[1]},
    "Соболева Любовь Анатольевна": {1:[1,2,3],2:[1],3:[3],4:[2,3],5:[1],6:[1,2,3]},
    "Гусакова Наталья Михайловна": {1:[3],2:[1,2,3],3:[1,2],5:[4],6:[1,2,3]},
    "Тарасюк Татьяна Ивановна": {3:[3],5:[3],6:[1,2]},
    "Лимонова Евгения Николаевна": {4:[1,2]},
    "Бурдин Алексей Александрович": {1:[1,2,3],3:[1,2,3],4:[1,2,3],5:[1,2],6:[2,3]},
    "Тарасов Игорь Викторович": {2:[3],3:[3],5:[3],6:[1,2,3]},
    "Шалых Борис Сергеевич": {1:[1,2,3],2:[1,2,3],6:[1,2,3]},
    "Комарова Яна Николаевна": {1:[1,2,3],2:[1,2,3],3:[1,2,3],4:[1,2,3],5:[1,2,3]},
    "Полякова Юлия Александровна": {1:[1,2,3],2:[1],3:[1,2,3],4:[2,3],5:[1,2,3]},
    "Кузнецова Елизавета Олеговна": {1:[1],5:[1,2,3],6:[1,2,3]},
    "Хасанова Гузель Хамитовна": {1:[1,2],2:[1,2],3:[1,2],4:[1,2],5:[1],6:[1]},
    "Ярославцева Елена Анатольевна": {6:[1,2]},
    "Бобылева Екатерина Дмитриевна": {2:[1,2,3],4:[1,2,3]},
}

ALIASES = {
    "Аринкина Татьяна Юрьевна": "Аринкус Татьяна Юрьевна",
    "Полякова Юлия Александровна": "Попова Юлия Александровна",
}


def full_week() -> list[dict]:
    return [
        {"day": day, "enabled": day <= 6, "start_slot": 1, "end_slot": 7,
         "slots": list(range(1, 8)) if day <= 6 else []}
        for day in range(1, 8)
    ]


def normalize_days(days: list[dict] | None) -> list[dict]:
    source = {int(row.get("day", 0)): row for row in (days or [])}
    result = []
    for day in range(1, 8):
        row = source.get(day)
        if row is None:
            slots = list(range(1, 8)) if day <= 6 else []
        elif not row.get("enabled", day <= 6):
            slots = []
        elif isinstance(row.get("slots"), list):
            slots = sorted({int(x) for x in row["slots"] if 1 <= int(x) <= 7})
        else:
            start = max(1, int(row.get("start_slot", 1)))
            end = min(7, int(row.get("end_slot", 7)))
            slots = list(range(start, end + 1))
        result.append({"day": day, "enabled": bool(slots),
                       "start_slot": slots[0] if slots else 1,
                       "end_slot": slots[-1] if slots else 7, "slots": slots})
    return result


def constrained_days(base: list[dict], blocked: dict[int, list[int]]) -> list[dict]:
    result = normalize_days(base)
    for row in result:
        forbidden = set(blocked.get(int(row["day"]), []))
        slots = [slot for slot in row["slots"] if slot not in forbidden]
        row.update(enabled=bool(slots), start_slot=slots[0] if slots else 1,
                   end_slot=slots[-1] if slots else 7, slots=slots)
    return result


def slots_count(days: list[dict]) -> int:
    return sum(len(row.get("slots", [])) for row in normalize_days(days) if row["day"] <= 6)


def replace_teacher_references(data: dict, old_id: int, new_id: int) -> int:
    changed = 0
    for lesson in data.get("lessons", []):
        if int(lesson.get("teacher", -1)) == old_id:
            lesson["teacher"] = new_id; changed += 1
    for group in data.get("groups", []):
        if int(group.get("curator_teacher", -1)) == old_id:
            group["curator_teacher"] = new_id; changed += 1
    for row in data.get("teacher_unavailable", []):
        if int(row.get("teacher", -1)) == old_id:
            row["teacher"] = new_id; changed += 1
    for row in data.get("substitutions", []):
        for key in ("absent_teacher", "substitute_teacher", "actual_teacher"):
            if int(row.get(key, -1)) == old_id:
                row[key] = new_id; changed += 1
    for row in data.get("accounting_adjustments", []):
        if int(row.get("teacher", -1)) == old_id:
            row["teacher"] = new_id; changed += 1
    for room in data.get("rooms", []):
        ids = [new_id if int(value) == old_id else int(value)
               for value in room.get("responsible_teacher_ids", [])]
        if ids != room.get("responsible_teacher_ids", []):
            room["responsible_teacher_ids"] = sorted(set(ids)); changed += 1
    return changed


def main() -> None:
    HISTORY.mkdir(parents=True, exist_ok=True)
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup = HISTORY / f"before_school_teacher_constraints_{stamp}.json"
    shutil.copy2(DATA, backup)
    data = json.loads(DATA.read_text(encoding="utf-8"))
    teachers = data.setdefault("teachers", [])
    by_name = {row.get("name", ""): row for row in teachers}

    bobyleva = by_name["Бобылева Екатерина Дмитриевна"]
    duplicate = next((by_name[name] for name in
                      ("Басова Екатерина Дмитриевна", "Баскова Екатерина Дмитриевна")
                      if name in by_name), None)
    duplicate_result = {"found": bool(duplicate), "references_moved": 0}
    if duplicate:
        duplicate_result["old_name"] = duplicate["name"]
        duplicate_result["old_id"] = int(duplicate["id"])
        duplicate_result["references_moved"] = replace_teacher_references(
            data, int(duplicate["id"]), int(bobyleva["id"]))
        # Reuse the now-empty, contiguous ID for the missing teacher from page 1.
        duplicate.clear()
        duplicate.update({
            "id": duplicate_result["old_id"],
            "uid": "teacher-" + hashlib.sha1(
                "Нуриахметова Надежда Сергеевна".encode("utf-8")).hexdigest()[:16],
            "name": "Нуриахметова Надежда Сергеевна",
            "work_period": {"from": "", "to": ""}, "work_days": full_week(),
            "default_room": -1, "campus_priority": [], "allowed_campuses": [],
            "max_work_days_per_week": 0, "max_pairs_per_day": 0,
            "date_slot_overrides": [], "availability_note": "",
        })
        duplicate_result["reused_for"] = duplicate["name"]
    elif "Нуриахметова Надежда Сергеевна" not in by_name:
        next_id = max(int(row.get("id", -1)) for row in teachers) + 1
        teachers.append({"id": next_id, "uid": "teacher-" + hashlib.sha1(
            "Нуриахметова Надежда Сергеевна".encode("utf-8")).hexdigest()[:16],
            "name": "Нуриахметова Надежда Сергеевна", "work_period": {"from":"","to":""},
            "work_days": full_week(), "default_room": -1, "campus_priority": [],
            "allowed_campuses": [], "max_work_days_per_week": 0,
            "max_pairs_per_day": 0, "date_slot_overrides": [], "availability_note": ""})

    by_name = {row.get("name", ""): row for row in teachers}
    changes = []
    local_by_source = {}
    for source_name, blocked in BUSY.items():
        local_name = ALIASES.get(source_name, source_name)
        teacher = by_name.get(local_name)
        if teacher is None:
            raise RuntimeError(f"Teacher not found after alias resolution: {source_name} -> {local_name}")
        local_by_source[source_name] = teacher
        base_key = "work_days_before_school_schedule_2026"
        if base_key not in teacher:
            teacher[base_key] = copy.deepcopy(normalize_days(teacher.get("work_days")))
        before = slots_count(teacher[base_key])
        teacher["work_days"] = constrained_days(teacher[base_key], blocked)
        after = slots_count(teacher["work_days"])
        teacher["school_schedule_2026_2027"] = {
            "source_name": source_name,
            "blocked_pairs": {str(day): sorted(values) for day, values in blocked.items()},
            "source": "3 фотографии общего расписания учителей от 31.08.2026",
        }
        changes.append({"teacher_id": int(teacher["id"]), "source_name": source_name,
                        "local_name": local_name, "before_slots": before,
                        "after_slots": after, "removed_slots": before-after})

    # The primary documented room is occupied by the same school timetable.
    rooms_by_id = {int(room["id"]): room for room in data.get("rooms", [])}
    room_blocks: dict[int, dict[int, set[int]]] = defaultdict(lambda: defaultdict(set))
    room_sources: dict[int, list[str]] = defaultdict(list)
    for source_name, teacher in local_by_source.items():
        room_id = int(teacher.get("default_room", -1))
        room = rooms_by_id.get(room_id)
        if room is None or int(teacher["id"]) not in {
                int(value) for value in room.get("responsible_teacher_ids", [])}:
            continue
        room_sources[room_id].append(teacher["name"])
        for day, pairs in BUSY[source_name].items():
            room_blocks[room_id][int(day)].update(int(value) for value in pairs)

    room_changes = []
    for room_id, blocked in room_blocks.items():
        room = rooms_by_id[room_id]
        base_key = "work_days_before_school_schedule_2026"
        if base_key not in room:
            room[base_key] = copy.deepcopy(normalize_days(room.get("work_days")))
        before = slots_count(room[base_key])
        room["work_days"] = constrained_days(
            room[base_key], {day: sorted(values) for day, values in blocked.items()})
        after = slots_count(room["work_days"])
        room["school_schedule_2026_2027"] = {
            "teachers": sorted(room_sources[room_id]),
            "blocked_pairs": {str(day): sorted(values) for day, values in blocked.items()},
        }
        room_changes.append({"room_id": room_id, "room_name": room.get("name", ""),
                             "teachers": sorted(room_sources[room_id]),
                             "before_slots": before, "after_slots": after,
                             "removed_slots": before-after})

    # Capacity/readout analysis for the 16-week semester.
    active = [row for row in data.get("lessons", []) if row.get("plan_active", True)]
    load = defaultdict(lambda: {"hours": 0.0, "lessons": 0})
    for lesson in active:
        teacher_id = int(lesson.get("teacher", -1))
        if teacher_id >= 0:
            load[teacher_id]["hours"] += float(lesson.get("total_hours", 0))
            load[teacher_id]["lessons"] += 1
    for row in changes:
        teacher = next(item for item in teachers if int(item["id"]) == row["teacher_id"])
        row.update(active_lessons=load[row["teacher_id"]]["lessons"],
                   semester_hours=load[row["teacher_id"]]["hours"],
                   required_pairs_per_week=round(load[row["teacher_id"]]["hours"] / 2 / 16, 2),
                   capacity_ratio=round(row["after_slots"] /
                       max(0.01, load[row["teacher_id"]]["hours"] / 2 / 16), 2)
                       if load[row["teacher_id"]]["hours"] else None)

    data.setdefault("meta", {})["school_teacher_schedule_2026_2027"] = {
        "applied_at": datetime.now().astimezone().isoformat(),
        "source_pages": 3, "teachers_on_photos": len(BUSY),
        "conversion": "уроки 1-2 -> пара 1; 3-4 -> 2; 5-6 -> 3; 7 -> 4",
        "rule": "любая заполненная школьная ячейка блокирует пересекающуюся пару",
        "name_aliases": ALIASES, "duplicate_bobyleva": duplicate_result,
        "primary_rooms_blocked": len(room_changes), "backup": str(backup),
    }
    DATA.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    report = {"backup": str(backup), "duplicate": duplicate_result,
              "teachers": changes, "rooms": room_changes}
    report_path = REPORT_DIR / "school_teacher_constraints_2026.json"
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"ok": True, "teachers": len(changes), "rooms": len(room_changes),
                      "duplicate": duplicate_result, "report": str(report_path)},
                     ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
