#!/usr/bin/env python3
"""Apply the urgent 2026-09-04/05 scheduling constraints atomically."""

from __future__ import annotations

import json
import shutil
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "timetable_data.json"
HISTORY = ROOT / "data" / "history"


def work_day(day: int, slots: list[int]) -> dict:
    return {
        "day": day,
        "enabled": bool(slots),
        "end_slot": max(slots, default=7),
        "slots": slots,
        "start_slot": min(slots, default=1),
    }


def set_teacher_days(teacher: dict, allowed: dict[int, list[int]]) -> None:
    teacher["work_days"] = [work_day(day, allowed.get(day, [])) for day in range(1, 8)]


def main() -> None:
    payload = json.loads(DATA.read_text(encoding="utf-8"))
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    HISTORY.mkdir(parents=True, exist_ok=True)
    backup = HISTORY / f"before_friday_saturday_revision_{stamp}.json"
    shutil.copy2(DATA, backup)

    teachers = {int(item["id"]): item for item in payload["teachers"]}

    # Korobkova: only Thursday/Friday, pairs 1-4. In the current Fri/Sat horizon
    # this means Friday 1-4 and no Saturday work.
    set_teacher_days(teachers[39], {4: [1, 2, 3, 4], 5: [1, 2, 3, 4]})

    # Koltyshev: Saturday is prohibited without exceptions.
    saturday = next(day for day in teachers[66]["work_days"] if int(day["day"]) == 6)
    saturday.update(work_day(6, []))
    overrides = [
        item for item in teachers[66].get("date_slot_overrides", [])
        if item.get("date") != "2026-09-05"
    ]
    overrides.append({"date": "2026-09-05", "slots": []})
    teachers[66]["date_slot_overrides"] = overrides

    # Kalchevskaya's LPZ is now held at Lesnaya, so her former Krivousova-only
    # restriction must be updated consistently as well.
    teachers[49]["allowed_campuses"] = [0]
    teachers[49]["campus_priority"] = [0]
    teachers[55]["allowed_campuses"] = [0]
    teachers[55]["campus_priority"] = [0]

    # ЦПДЭ was absent from the room directory. Add it at Lesnaya as a room
    # reserved for Kalchevskaya's lab/practical classes.
    cpde = next((room for room in payload["rooms"] if room.get("name") == "ЦПДЭ"), None)
    if cpde is None:
        cpde_id = max(int(room["id"]) for room in payload["rooms"]) + 1
        cpde = {
            "access_mode": "exclusive",
            "active": True,
            "campus": 0,
            "capacity": 0,
            "date_slot_overrides": [],
            "description": "ЦПДЭ, специализированная площадка для ЛПЗ",
            "equipment": [],
            "id": cpde_id,
            "name": "ЦПДЭ",
            "purpose": "",
            "responsible_note": "Кальчевская Н.В.",
            "responsible_teacher_ids": [49],
            "room_type": 0,
            "source": "Оперативное ограничение от 03.09.2026",
            "uid": f"room-cpde-{cpde_id}",
            "work_days": [work_day(day, list(range(1, 8)) if day <= 6 else []) for day in range(1, 8)],
            "work_period": {"from": "", "to": ""},
        }
        payload["rooms"].append(cpde)
    else:
        cpde_id = int(cpde["id"])
        cpde.update({"active": True, "campus": 0, "access_mode": "exclusive"})
        cpde["responsible_teacher_ids"] = [49]

    changed = {
        "kalchevskaya_lpz": [],
        "limonova_lpz": [],
        "limonova_theory": [],
        "samtsov_lpz": [],
        "samtsov_theory": [],
    }
    for lesson in payload["lessons"]:
        teacher = int(lesson.get("teacher", -1))
        name = str(lesson.get("name", ""))
        # The dispatcher's source of truth is the text marker itself.  Imported
        # is_lab flags may be stale, therefore any occurrence of "ЛПЗ" wins.
        is_lpz = "лпз" in name.casefold()

        if teacher == 49 and is_lpz:
            lesson.update({
                "is_lab": True,
                "allowed_campuses": [0],
                "fixed_room": cpde_id,
                "preferred_room": cpde_id,
                "allow_room_substitution": False,
                "required_room_type": 0,
            })
            changed["kalchevskaya_lpz"].append(int(lesson["id"]))

        if teacher == 55:
            lesson.update({"is_lab": is_lpz, "allowed_campuses": [0], "required_room_type": 0})
            if is_lpz:
                lesson.update({
                    "fixed_room": 66,
                    "preferred_room": 66,
                    "allow_room_substitution": False,
                })
                changed["limonova_lpz"].append(int(lesson["id"]))
            else:
                lesson.update({
                    "fixed_room": -1,
                    "preferred_room": -1,
                    "allow_room_substitution": True,
                })
                changed["limonova_theory"].append(int(lesson["id"]))

        if teacher == 59:
            lesson.update({"is_lab": is_lpz, "allowed_campuses": [0], "required_room_type": 0})
            if is_lpz:
                # Both 115-116 and 122-123 are valid automotive workshops.
                # 115-116 is preferred, while the allocator may use 122-123.
                lesson.update({
                    "fixed_room": -1,
                    "preferred_room": 64,
                    "allow_room_substitution": True,
                })
                changed["samtsov_lpz"].append(int(lesson["id"]))
            else:
                lesson.update({
                    "fixed_room": -1,
                    "preferred_room": -1,
                    "allow_room_substitution": True,
                })
                changed["samtsov_theory"].append(int(lesson["id"]))

    settings = payload.setdefault("settings", {})
    solver = settings.setdefault("solver_config", {})
    solver["hard_no_student_windows"] = True
    solver["optimize_student_windows"] = False
    # This is a two-day remainder after Wednesday/Thursday were already fixed.
    # Requiring every group to study on both remaining days can make valid room
    # restrictions impossible; compact no-window days remain mandatory.
    solver["hard_min_study_days_per_week"] = False
    # CPDE at Lesnaya makes the former global four-pair ceiling infeasible for
    # the affected subgroup. Permit a fifth pair but penalize it heavily so it
    # is used only where required by the hard campus/room constraints.
    solver["max_student_pairs_per_day"] = 5
    solver["student_five_pair_day_weight"] = 100000
    solver["quality_improvement_seconds"] = min(int(solver.get("quality_improvement_seconds", 30)), 30)
    solver["week_time_limit_seconds"] = min(int(solver.get("week_time_limit_seconds", 90)), 90)

    notes = payload.setdefault("meta", {}).setdefault("operational_constraints", [])
    marker = "revision-2026-09-03-friday-saturday-v3"
    notes = [
        item for item in notes
        if not isinstance(item, dict) or item.get("id") not in {
            "revision-2026-09-03-friday-saturday-v2", marker,
        }
    ]
    notes.append({
        "id": marker,
        "scope": ["2026-09-04", "2026-09-05"],
        "rules": [
            "Колтышев: суббота запрещена",
            "Коробкова: четверг и пятница, пары 1-4",
            "Окна у студентов запрещены",
            "С 05.09: у Лимоновой только занятия с буквами ЛПЗ идут в мастерскую 120-121; остальные — обычная аудитория на Лесной, кроме 210",
            "С 05.09: у Самцова только занятия с буквами ЛПЗ идут в мастерские 115-116/122-123; остальные — обычная аудитория на Лесной, кроме 210",
            "С 05.09: занятия Кальчевской с буквами ЛПЗ идут в ЦПДЭ",
            "Расписание пятницы 04.09 сохраняется без изменений",
        ],
    })
    payload["meta"]["operational_constraints"] = notes

    temp = DATA.with_suffix(".json.tmp")
    temp.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temp.replace(DATA)

    print(json.dumps({
        "backup": str(backup),
        "cpde_room_id": cpde_id,
        "changed_lessons": changed,
        "hard_no_student_windows": solver["hard_no_student_windows"],
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
