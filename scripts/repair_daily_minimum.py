"""Add locked adjacent theory lessons to remove one-pair student days.

Run after a feasible one-week generation with min_student_pairs_per_study_day=1.
The resulting manual schedule is used as a lock source for the final min=2 run.
"""

from __future__ import annotations

import json
import shutil
from collections import defaultdict
from datetime import date, datetime
from pathlib import Path

from prepare_one_week_generation import DATA, HISTORY, ROOT, STATE, rule_allows, unavailable_dates


SCHEDULE = ROOT / "output" / "latest" / "schedule_all.json"
MANUAL = ROOT / "output" / "manual" / "schedule_all.json"


def read(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    tmp.replace(path)


def lesson_parts(lesson: dict, group_id: int) -> set[int]:
    subgroup = int(lesson.get("subgroup", -1))
    if subgroup < 0:
        return {0, 1}
    if subgroup == group_id * 2:
        return {0}
    if subgroup == group_id * 2 + 1:
        return {1}
    return set()


def contiguous(slots: set[int]) -> bool:
    return not slots or max(slots) - min(slots) + 1 == len(slots)


def main() -> None:
    data = read(DATA)
    schedule = read(SCHEDULE)
    state = read(STATE)
    original = read(Path(state["backup"]))

    groups = {int(item["id"]): item for item in data.get("groups", [])}
    teachers = {int(item["id"]): item for item in data.get("teachers", [])}
    lessons = {int(item["id"]): item for item in data.get("lessons", [])}
    original_lessons = {int(item["id"]): item for item in original.get("lessons", [])}
    rooms = {int(item["id"]): item for item in data.get("rooms", [])}
    unavailable = unavailable_dates(data)

    group_days: dict[tuple[int, str], dict] = {}
    part_busy: dict[tuple[int, str, int], set[int]] = defaultdict(set)
    group_busy: dict[tuple[int, str], set[int]] = defaultdict(set)
    teacher_busy: dict[tuple[int, str], set[int]] = defaultdict(set)
    teacher_days: dict[int, set[str]] = defaultdict(set)
    group_campus: dict[tuple[int, str], set[int]] = defaultdict(set)
    teacher_campus: dict[tuple[int, str], set[int]] = defaultdict(set)
    room_busy: dict[tuple[int, str], set[int]] = defaultdict(set)

    for group_output in schedule.get("groups", []):
        group_id = int(group_output["group_index"])
        for day in group_output.get("days", []):
            raw_date = day["date_iso"]
            group_days[(group_id, raw_date)] = day
            for slot in day.get("slots", []):
                slot_number = int(slot["slot"])
                for placed in slot.get("lessons", []):
                    lesson = lessons[int(placed["id"])]
                    teacher_id = int(lesson.get("teacher", -1))
                    group_busy[(group_id, raw_date)].add(slot_number)
                    for part in lesson_parts(lesson, group_id):
                        part_busy[(group_id, raw_date, part)].add(slot_number)
                    if teacher_id >= 0:
                        teacher_busy[(teacher_id, raw_date)].add(slot_number)
                        teacher_days[teacher_id].add(raw_date)
                    room_id = int(placed.get("room_id", -1))
                    if room_id >= 0:
                        room_busy[(room_id, raw_date)].add(slot_number)
                        campus = int(rooms.get(room_id, {}).get("campus", -1))
                        if campus >= 0:
                            group_campus[(group_id, raw_date)].add(campus)
                            if teacher_id >= 0:
                                teacher_campus[(teacher_id, raw_date)].add(campus)

    quota = {
        lesson_id: int(lesson.get("total_slots", 0)) if lesson.get("plan_active", True) else 0
        for lesson_id, lesson in lessons.items()
    }
    additions: list[dict] = []

    # ТЭО-Пф-3501 has only Потапова on Wed/Thu/Sat and Сутягин on Fri. Two
    # additional Потапова theory occurrences are therefore unavoidable. Her
    # existing placements are deliberately left unlocked so CP-SAT can move
    # them between the four days and make 2+2+2+2 possible.
    flexible_teachers = {8}
    flexible_groups = {20}
    for lesson_id in (412, 413):
        quota[lesson_id] += 1
        lessons[lesson_id]["total_slots"] = quota[lesson_id]
        lessons[lesson_id]["plan_active"] = True
        additions.append({
            "group": 20,
            "group_name": groups[20]["name"],
            "date": "solver-selected",
            "slot": 0,
            "lesson": lesson_id,
            "lesson_name": lessons[lesson_id]["name"],
            "teacher": 8,
            "teacher_name": teachers[8]["name"],
        })

    def remaining(lesson_id: int) -> int:
        return int(original_lessons.get(lesson_id, {}).get("total_slots", 0)) - quota[lesson_id]

    def options(group_id: int, raw_date: str, missing_parts: set[int]) -> list[tuple]:
        current_date = date.fromisoformat(raw_date)
        result = []
        for lesson_id, lesson in lessons.items():
            if int(lesson.get("group", -1)) != group_id:
                continue
            if lesson.get("is_lab") or lesson.get("is_block") or lesson.get("is_pp"):
                continue
            if remaining(lesson_id) <= 0:
                continue
            teacher_id = int(lesson.get("teacher", -1))
            if teacher_id < 0 or teacher_id not in teachers or current_date in unavailable[teacher_id]:
                continue
            covered = lesson_parts(lesson, group_id) & missing_parts
            if not covered:
                continue
            teacher = teachers[teacher_id]
            max_daily = int(teacher.get("max_pairs_per_day", 0)) or 7
            if len(teacher_busy[(teacher_id, raw_date)]) >= max_daily:
                continue
            max_days = int(teacher.get("max_work_days_per_week", 0))
            if max_days and raw_date not in teacher_days[teacher_id] and len(teacher_days[teacher_id]) >= max_days:
                continue

            candidate_slots = set(range(1, 8))
            for part in covered:
                existing = part_busy[(group_id, raw_date, part)]
                candidate_slots &= {slot for slot in range(1, 8) if contiguous(existing | {slot})}
            for slot_number in sorted(candidate_slots):
                if slot_number in teacher_busy[(teacher_id, raw_date)]:
                    continue
                if any(slot_number in part_busy[(group_id, raw_date, part)] for part in lesson_parts(lesson, group_id)):
                    continue
                if not rule_allows(groups[group_id], current_date, slot_number) or not rule_allows(teacher, current_date, slot_number):
                    continue

                fixed_room = int(lesson.get("fixed_room", -1))
                hard_room = fixed_room >= 0 and not lesson.get("allow_room_substitution", True)
                if hard_room and slot_number in room_busy[(fixed_room, raw_date)]:
                    continue
                allowed_campuses = {int(x) for x in lesson.get("allowed_campuses", [0, 1])}
                if hard_room:
                    allowed_campuses &= {int(rooms.get(fixed_room, {}).get("campus", -1))}
                gc = group_campus[(group_id, raw_date)]
                tc = teacher_campus[(teacher_id, raw_date)]
                if gc:
                    allowed_campuses &= gc
                if tc:
                    allowed_campuses &= tc
                if not allowed_campuses:
                    continue

                score = (
                    len(covered),
                    max_daily - len(teacher_busy[(teacher_id, raw_date)]),
                    1 if quota[lesson_id] > 0 else 0,
                    remaining(lesson_id),
                    -slot_number,
                )
                result.append((score, lesson_id, teacher_id, slot_number, covered, fixed_room if hard_room else -1))
        return sorted(result, reverse=True)

    while True:
        needs = []
        for group_id, raw_date in group_days:
            if group_id in flexible_groups:
                continue
            missing = {part for part in (0, 1) if len(part_busy[(group_id, raw_date, part)]) < 2}
            if not missing:
                continue
            available = options(group_id, raw_date, missing)
            needs.append((len(available), group_id, raw_date, missing, available))
        if not needs:
            break
        needs.sort(key=lambda item: (item[0], item[1], item[2]))
        count, group_id, raw_date, missing, available = needs[0]
        if count == 0:
            unresolved = [
                {"group": groups[item[1]]["name"], "date": item[2], "parts": sorted(item[3])}
                for item in needs if item[0] == 0
            ]
            raise SystemExit("Не найдена совместимая соседняя пара: " + json.dumps(unresolved, ensure_ascii=False))

        _, lesson_id, teacher_id, slot_number, covered, fixed_room = available[0]
        quota[lesson_id] += 1
        lesson = lessons[lesson_id]
        lesson["total_slots"] = quota[lesson_id]
        lesson["plan_active"] = True
        teacher_busy[(teacher_id, raw_date)].add(slot_number)
        teacher_days[teacher_id].add(raw_date)
        group_busy[(group_id, raw_date)].add(slot_number)
        for part in lesson_parts(lesson, group_id):
            part_busy[(group_id, raw_date, part)].add(slot_number)
        if fixed_room >= 0:
            room_busy[(fixed_room, raw_date)].add(slot_number)

        day = group_days[(group_id, raw_date)]
        slot = next(item for item in day["slots"] if int(item["slot"]) == slot_number)
        slot.setdefault("lessons", []).append({"id": lesson_id})
        additions.append({
            "group": group_id,
            "group_name": groups[group_id]["name"],
            "date": raw_date,
            "slot": slot_number,
            "lesson": lesson_id,
            "lesson_name": lesson["name"],
            "teacher": teacher_id,
            "teacher_name": teachers[teacher_id]["name"],
        })

    config = data.setdefault("settings", {}).setdefault("solver_config", {})
    config["min_student_pairs_per_study_day"] = 2
    config["min_student_study_days_per_week"] = 4
    config["hard_min_study_days_per_week"] = True
    config["hard_no_student_windows"] = True
    data.setdefault("meta", {})["daily_minimum_repair"] = {
        "created_at": datetime.now().isoformat(),
        "added_occurrences": additions,
        "lock_source": "manual",
    }

    HISTORY.mkdir(parents=True, exist_ok=True)
    backup = HISTORY / f"before_daily_minimum_repair_{datetime.now():%Y%m%d-%H%M%S}.json"
    shutil.copy2(DATA, backup)
    for group_output in schedule.get("groups", []):
        for day in group_output.get("days", []):
            for slot in day.get("slots", []):
                slot["lessons"] = [
                    placed for placed in slot.get("lessons", [])
                    if int(lessons[int(placed["id"])].get("teacher", -1)) not in flexible_teachers
                ]
    write(DATA, data)
    write(MANUAL, schedule)
    print(json.dumps({"added": len(additions), "backup": str(backup), "manual": str(MANUAL)}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
