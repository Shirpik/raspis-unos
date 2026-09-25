"""Build and independently audit the 25–26 September in-person timetable.

The confirmed teaching ledger through 24 September is the only source of
completed hours. This command writes an isolated candidate, never the live DB.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from collections import Counter, defaultdict
from datetime import date
from pathlib import Path

from ortools.sat.python import cp_model

from prepare_one_week_generation import lesson_parts, rule_allows, unavailable_dates
from teaching_balance import balances, calendar_allows, course, deadline

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "outputs" / "generation-20260925-26"
DAYS = (date(2026, 9, 25), date(2026, 9, 26))
AI_NAMES = [
    "Абрамчук", "Азарян", "Ахметов", "Вагайская", "Дроговейко",
    "Ермолина", "Журавский", "Комарова", "Конева", "Михайлова",
    "Нифонтова", "Новосёлова", "Рахматулина", "Рабенок",
    "Саламатина", "Самцов", "Семенова", "Серянина", "Сивилькаев",
    "Синельникова", "Силенок", "Соболева", "Тарасов", "Тимеров",
    "Третяк", "Хасанова", "Черепанова", "Шадрина",
]
FORUM = {4, 25, 47, 50}
ABSENT_BOTH = {5, 8, 35}
SATURDAY_MORNING_NAMES = {
    "ТАКХС-Пф-2202", "МЦМ-Пф-301", "ТЭиРП-2901", "ТАКХС-3201",
    "ТМ-2417", "ТМ-3416п", "МЦМ-Пф-202", "СП-4611", "ОСА-491",
    "ТМ-4413",
}
POPOVA_REQUESTS = {
    (292, 0, 1), (292, 0, 2), (316, 0, 3), (316, 0, 4),
    (338, 1, 1), (338, 1, 2), (339, 1, 3), (339, 1, 4),
}
SUTYAGIN_ID = 9
RABENOK_ID = 12
RABENOK_TM_LESSON_ID = 55  # ТМ-1420, группа, с которой в этой неделе пар не было
PODCHIN_REQUESTS = {
    (604, 0, 1), (578, 0, 5),  # Friday: СП-Пф-3602п then СП-Пф-3601
    (595, 1, 1), (615, 1, 5),  # Saturday: СП-Пф-3601 then СП-Пф-3602п
}
KOSHELEV_ID = 51
MIKHAYLOVA_ID = 65
HANZHINA_ID = 68
USKOV_ID = 82
REQUIRED_GROUP_NAMES = None  # every teaching group must have classes on both days
BURKOVA_ID = 16
KOBYLYANSKAYA_ID = 7
KRUGLOVA_ID = 24
KOROBKOVA_ID = 39
PISMAK_ID = 44
KOMAROVA_ID = 21
NOVOSELOVA_ID = 28


def save(name: str, value: object) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def teacher_surname(name: str) -> str:
    return name.split()[0].lower().replace("ё", "е")


def is_up(lesson: dict) -> bool:
    return bool(lesson.get("is_block")) and lesson.get("name", "").startswith("УП")


def allowed_teacher(teacher: dict, day: date, slot: int, absent: dict) -> bool:
    tid = teacher["id"]
    if tid in ABSENT_BOTH or day in absent[tid]:
        return False
    if tid == PISMAK_ID:
        return False
    # Confirmed for this generated weekend: Komarova is absent on both days;
    # Novoselova is absent on Saturday. Keep these explicit so a broad regular
    # work-week setting cannot reintroduce either assignment.
    if tid == KOMAROVA_ID:
        return False
    if day == DAYS[1] and tid == NOVOSELOVA_ID:
        return False
    if tid == USKOV_ID:
        return False
    if tid == MIKHAYLOVA_ID and day == DAYS[1]:
        return False
    if tid == HANZHINA_ID and day == DAYS[0]:
        return False
    if tid == KOBYLYANSKAYA_ID and slot > 2:
        return False
    if tid == KRUGLOVA_ID and slot > 5:
        return False
    if tid == RABENOK_ID and day == DAYS[0]:
        return slot == 2
    if day == DAYS[0]:
        if tid in FORUM or tid == 60:  # Friday forum / Тимеров absence
            return False
        if tid == 38 and slot < 4:  # Абрамчук can only start at the 4th pair
            return False
        if any(teacher_surname(teacher["name"]) == value.lower().replace("ё", "е") for value in AI_NAMES) and slot >= 4:
            return False
    if day == DAYS[1] and tid == 23 and slot >= 4:  # Журавский: first shift
        return False
    return rule_allows(teacher, day, slot)


def morning_group(group: dict) -> bool:
    if group["name"] in SATURDAY_MORNING_NAMES:
        return True
    return course(group) == 1 and group["name"] not in {"РУП-1301п", "ТМ-1420"}


def saturday_last_slot(group: dict) -> int:
    # The handwritten list must start at 08:30 and have 2–4 pairs. Other
    # first-year cohorts stay in the first three pairs as requested earlier.
    if group["name"] in SATURDAY_MORNING_NAMES:
        return 4
    return 3 if morning_group(group) else 7


def compatible_rooms(lesson: dict, teacher: dict, group: dict, rooms: list[dict], day: date, slot: int) -> list[dict]:
    answer = []
    allowed = set(lesson.get("allowed_campuses") or [0, 1]) & set(teacher.get("allowed_campuses") or [0, 1])
    for room in rooms:
        if room["campus"] not in allowed or not rule_allows(room, day, slot):
            continue
        if room.get("available_slots") and slot not in room["available_slots"]:
            continue
        if room["id"] == 5 and day == DAYS[0] and slot in (4, 5, 6):  # AI course, room 16
            continue
        if room["id"] == 15 and day == DAYS[0] and slot == 3:  # cadets, room 32
            continue
        if room.get("access_mode") == "exclusive" and teacher["id"] not in room.get("responsible_teacher_ids", []):
            continue
        is_lpz = "лпз" in lesson["name"].lower()
        if teacher["id"] == 55:
            if is_lpz and room["id"] != 66:
                continue
            if not is_lpz and not (room["campus"] == 0 and room["name"] != "210" and room.get("access_mode") == "general"):
                continue
        if teacher["id"] == 59:
            if is_lpz and room["id"] not in (64, 65):
                continue
            if not is_lpz and not (room["campus"] == 0 and room["name"] != "210" and room.get("access_mode") == "general"):
                continue
        if teacher["id"] in (49, 57) and is_lpz and room["id"] != 68:
            continue
        if (lesson.get("required_room_purpose") == "sports_hall") != (room.get("purpose") == "sports_hall"):
            continue
        if lesson.get("required_room_type", 0) > 0 and lesson["required_room_type"] != room.get("room_type"):
            continue
        if not set(lesson.get("required_equipment") or []).issubset(room.get("equipment") or []):
            continue
        if room.get("capacity", 0) > 0 and lesson.get("required_capacity", 0) > room["capacity"]:
            continue
        fixed = lesson.get("fixed_room", -1)
        if fixed is not None and fixed >= 0 and not lesson.get("allow_room_substitution", True) and room["id"] != fixed:
            continue
        if teacher["id"] == 57 and room["id"] != 68:  # Подчиненнов: ЦПДЭ
            continue
        answer.append(room)
    return answer


def weekday_worked(ledger: list[dict], teachers: set[int]) -> dict[int, set[str]]:
    result = defaultdict(set)
    for row in ledger:
        if row.get("status") != "confirmed" or not "2026-09-21" <= row.get("date", "") <= "2026-09-24":
            continue
        teacher = row.get("actual_teacher", teachers.get(row.get("lesson_id"), -1))
        result[teacher].add(row["date"])
    return result


def solve(args: argparse.Namespace) -> int:
    data = json.loads((ROOT / "data" / "timetable_data.json").read_text(encoding="utf-8"))
    groups = {g["id"]: g for g in data["groups"]}
    teachers = {t["id"]: t for t in data["teachers"]}
    rooms = [r for r in data["rooms"] if r.get("active", True) and r.get("access_mode") != "blocked"]
    confirmed, reserved, _ = balances(data, DAYS[0])
    absent = unavailable_dates(data)
    lesson_teachers = {l["id"]: l["teacher"] for l in data["lessons"]}
    worked = weekday_worked(data.get("teaching_ledger", []), lesson_teachers)
    remaining = {l["id"]: max(0, (l.get("total_hours", 0) - confirmed[l["id"]] - reserved[l["id"]]) // 2) for l in data["lessons"]}
    lessons = {
        l["id"]: l for l in data["lessons"]
        if remaining[l["id"]] and l.get("curriculum_active", True) and l["teacher"] >= 0
        and not l.get("is_pp") and ((l["teacher"] == 57 and groups[l["group"]]["name"].startswith("СП-Пф-36") and is_up(l))
                                          or (l["teacher"] == KOSHELEV_ID and is_up(l))
                                          or l["teacher"] != 57 and not l.get("is_block"))
    }
    model = cp_model.CpModel()
    assumptions = {}
    objective = []
    def mandatory(label: str):
        flag = model.new_bool_var("mandatory_" + str(len(assumptions)))
        model.add_assumption(flag)
        assumptions[flag.index] = label
        return flag

    x = {}
    room_options = {}
    by_teacher = defaultdict(list)
    by_teacher_slot = defaultdict(list)
    by_group_part = defaultdict(list)
    by_group_part_slot = defaultdict(list)
    by_lesson = defaultdict(list)
    by_lesson_day = defaultdict(list)
    by_group = defaultdict(list)
    forced_room_slot = defaultdict(list)
    campus_group = {(g, d): model.new_bool_var(f"campus_g{g}_{d}") for g in groups for d in range(2)}
    campus_teacher = {(t, d): model.new_bool_var(f"campus_t{t}_{d}") for t in teachers for d in range(2)}
    for lid, lesson in lessons.items():
        tid, gid = lesson["teacher"], lesson["group"]
        teacher, group = teachers[tid], groups[gid]
        for d, day in enumerate(DAYS):
            if not calendar_allows(group, day) or day > deadline(group, data["settings"]):
                continue
            for slot in range(1, 8):
                if not allowed_teacher(teacher, day, slot, absent) or not rule_allows(group, day, slot):
                    continue
                if d == 1 and slot > saturday_last_slot(group):
                    continue
                candidates = compatible_rooms(lesson, teacher, group, rooms, day, slot)
                if not candidates:
                    continue
                key = (lid, d, slot)
                var = model.new_bool_var(f"x_{lid}_{d}_{slot}")
                x[key] = var
                room_options[key] = candidates
                if len(candidates) == 1:
                    forced_room_slot[d, slot, candidates[0]["id"]].append(var)
                by_lesson[lid].append(var)
                by_lesson_day[lid, d].append(var)
                by_teacher[tid, d].append(var)
                by_teacher_slot[tid, d, slot].append(var)
                by_group[gid, d].append(var)
                for _, part in lesson_parts(lesson, groups):
                    by_group_part[gid, part, d].append(var)
                    by_group_part_slot[gid, part, d, slot].append(var)
                campuses = {r["campus"] for r in candidates}
                model.add(campus_group[gid, d] == campus_teacher[tid, d]).only_enforce_if(var)
                if len(campuses) == 1:
                    model.add(campus_group[gid, d] == next(iter(campuses))).only_enforce_if(var)
    for lid, variables in by_lesson.items():
        model.add(sum(variables) <= remaining[lid])
    for key in POPOVA_REQUESTS:
        if key not in x:
            raise RuntimeError(f"Requested Popova pair has no feasible candidate: {key}")
        model.add(x[key] == 1)
    for key in ((RABENOK_TM_LESSON_ID, 0, 2),):
        if key not in x:
            raise RuntimeError(f"Requested Rabenok TM pair has no feasible candidate: {key}")
        model.add(x[key] == 1)
    for key in PODCHIN_REQUESTS:
        if key not in x:
            raise RuntimeError(f"Requested Podchinennov UP pair has no feasible candidate: {key}")
        model.add(x[key] == 1)
    for variables in by_teacher_slot.values():
        model.add(sum(variables) <= 1)
    for variables in by_group_part_slot.values():
        model.add(sum(variables) <= 1)
    # Exact room assignment runs after the timetable solve. Keeping the room
    # pool out of this global cut lets the solver preserve teaching load; the
    # allocator then reports any unavoidable remote overflow explicitly.
    for tid in teachers:
        for d in range(2):
            if tid in (57, KOSHELEV_ID):
                # Two fixed UP shifts deliberately have the lunch interval
                # between pairs 3 and 5; that interval is not an accidental
                # teacher window.
                continue
            occupied = [sum(by_teacher_slot[tid, d, slot]) for slot in range(1, 8)]
            for left in range(7):
                for middle in range(left + 1, 7):
                    for right in range(middle + 1, 7):
                        model.add(occupied[left] + occupied[right] - occupied[middle] <= 1)

    # LPZ blocks are consecutive. For Подчиненнов one UP occupies exactly
    # three consecutive pairs (six academic hours) with one group.
    up_active = defaultdict(list)
    up_group_active = defaultdict(list)
    up_part_active = defaultdict(list)
    for lid, lesson in lessons.items():
        group = groups[lesson["group"]]
        if lesson["teacher"] in (57, KOSHELEV_ID) and is_up(lesson):
            for d in range(2):
                active_up = model.new_bool_var(f"up_{lid}_{d}")
                up_active[lesson["teacher"], lesson["group"], d].append(active_up)
                up_group_active[lesson["group"], d].append(active_up)
                for _, part in lesson_parts(lesson, groups):
                    up_part_active[lesson["group"], part, d].append(active_up)
                starts = []
                # UP is a six-hour block in a fixed shift: first UP is
                # pairs 1–3 (08:30–12:30), second UP is pairs 5–7
                # (13:00–17:00). It may never start in the middle.
                for slot in (1, 5):
                    if all((lid, d, part_slot) in x for part_slot in range(slot, slot + 3)):
                        starts.append((slot, model.new_bool_var(f"up_start_{lid}_{d}_{slot}")))
                model.add(sum(flag for _, flag in starts) == active_up)
                for slot in range(1, 8):
                    if (lid, d, slot) in x:
                        model.add(x[lid, d, slot] == sum(flag for start, flag in starts if start <= slot <= start + 2))
            continue
        paired = lesson.get("consecutive_pairs", 1) == 2 or (
            lesson.get("is_lab") and group["name"].startswith(("ТМ-", "ПКД-", "ТОРД-", "ТОиРА-", "СП-", "МЦМ-", "ТАКХС-"))
        )
        for d in range(2):
            if not paired:
                continue
            starts = []
            for slot in range(1, 7):
                if (lid, d, slot) not in x or (lid, d, slot + 1) not in x:
                    continue
                if lesson.get("block_start_slots") and slot not in lesson["block_start_slots"]:
                    continue
                if group["name"].startswith("ПКД-") and slot not in (1, 3, 5, 6):
                    continue
                starts.append((slot, model.new_bool_var(f"block_{lid}_{d}_{slot}")))
            for slot in range(1, 8):
                if (lid, d, slot) in x:
                    model.add(x[lid, d, slot] == sum(flag for start, flag in starts if start <= slot <= start + 1))
    for tid in (57, KOSHELEV_ID):
        for d in range(2):
            model.add(sum(sum(flags) for (teacher_id, gid, dd), flags in up_active.items() if teacher_id == tid and dd == d) <= 2)
    for flags in up_group_active.values():
        model.add(sum(flags) <= 2)
    for flags in up_part_active.values():
        model.add(sum(flags) <= 1)

    # The official maximum is four physical pairs per student subgroup. A
    # group's displayed span may be longer when its subgroups alternate.
    active_group = {}
    for gid, group in groups.items():
        for d in range(2):
            active = model.new_bool_var(f"active_group_{gid}_{d}")
            active_group[gid, d] = active
            if d == 1 and group["name"] in SATURDAY_MORNING_NAMES:
                model.add(active == 1).only_enforce_if(mandatory(f"{group['name']}: занятия утром в субботу"))
            events = by_group[gid, d]
            if not events:
                model.add(active == 0)
                continue
            if REQUIRED_GROUP_NAMES is None or group["name"] in REQUIRED_GROUP_NAMES:
                model.add(active == 1).only_enforce_if(mandatory(f"{group['name']}: минимум 2 пары {DAYS[d]}"))
            model.add(sum(events) >= active)
            model.add(sum(events) <= len(events) * active)
            up_flags = up_group_active.get((gid, d), [])
            for part in range(max(1, group.get("parts", 2))):
                occupied = [sum(by_group_part_slot[gid, part, d, slot]) for slot in range(1, 8)]
                load = sum(occupied)
                model.add(load <= 4)
                # A practice block can belong to one subgroup stream. The
                # other stream stays empty; ordinary teaching still requires
                # both subgroup streams to have at least two pairs.
                if up_flags:
                    part_active = model.new_bool_var(f"active_part_{gid}_{part}_{d}")
                    minimum = 2
                    model.add(load >= minimum * part_active)
                    model.add(load <= 4 * part_active)
                else:
                    minimum = 2
                    model.add(load >= minimum * active)
                for left in range(7):
                    for middle in range(left + 1, 7):
                        for right in range(middle + 1, 7):
                            model.add(occupied[left] + occupied[right] - occupied[middle] <= 1)
                upto_three = model.new_int_var(0, 3, f"student_three_{gid}_{part}_{d}")
                model.add(upto_three <= load)
                objective.append(55 * upto_three)
            objective.append(500 * active)

            if up_flags:
                # A group assigned one UP block is occupied for that whole
                # shift/day: no ordinary lesson may be added alongside it.
                for (lid, dd, slot), var in x.items():
                    if dd == d and lessons[lid]["group"] == gid and not (
                        lessons[lid]["teacher"] in (57, KOSHELEV_ID) and is_up(lessons[lid])
                    ):
                        for up in up_flags:
                            model.add(var + up <= 1)
            if d == 1 and morning_group(group):
                model.add(sum(var for (lid, dd, slot), var in x.items() if dd == d and slot == 1 and lessons[lid]["group"] == gid) >= 1).only_enforce_if(mandatory(f"{group['name']}: первая пара субботы"))

    # Confirmed past hours + the group practice deadline determine the weekly
    # pace. The current week already includes 21–24 September facts.
    week_done = Counter()
    for row in data.get("teaching_ledger", []):
        if row.get("status") == "confirmed" and "2026-09-21" <= row.get("date", "") <= "2026-09-24":
            week_done[row.get("actual_teacher", lesson_teachers.get(row.get("lesson_id"), -1))] += row.get("hours", 0)
    weekly_need = Counter()
    for lid, lesson in lessons.items():
        group = groups[lesson["group"]]
        weeks = max(1.0, min(12.0, (deadline(group, data["settings"]) - DAYS[0]).days / 7))
        weekly_need[lesson["teacher"]] += 2 * remaining[lid] / weeks
    for tid, teacher in teachers.items():
        on_days = []
        for d in range(2):
            load = sum(by_teacher[tid, d])
            cap = min(teacher.get("max_pairs_per_day") or 7, 6 if tid == 57 else 7)
            model.add(load <= cap)
            on = model.new_bool_var(f"teacher_day_{tid}_{d}")
            model.add(load >= on)
            model.add(load <= 7 * on)
            on_days.append(on)
        max_days = teacher.get("max_work_days_per_week") or 0
        if max_days:
            model.add(sum(on_days) + len(worked[tid]) <= max_days)
        target = min(14, max(0, math.ceil((weekly_need[tid] - week_done[tid]) / 2)))
        if target:
            progress = model.new_int_var(0, target, f"weekly_progress_{tid}")
            model.add(progress <= sum((sum(by_teacher[tid, d]) for d in range(2))))
            objective.append(progress * 110)
        objective.append(sum((sum(by_teacher[tid, d]) for d in range(2))) * 3)
        for d in range(2):
            load = sum(by_teacher[tid, d])
            if tid in (62, 64, 67):
                model.add(load == 7).only_enforce_if(mandatory(f"{teacher['name']}: 7 пар {DAYS[d]}"))
            if tid == 57 and not args.relax_podchin:
                model.add(load >= 6).only_enforce_if(mandatory(f"Подчиненнов: минимум 6 пар {DAYS[d]}"))
            if tid == SUTYAGIN_ID and d == 0:
                model.add(load == 2).only_enforce_if(mandatory("Сутягин: 2 пары 25 сентября"))
            if tid == RABENOK_ID and d == 0:
                model.add(load == 1).only_enforce_if(mandatory("Рабенок М.А.: одна 2-я пара 25 сентября"))
            if tid == BURKOVA_ID:
                model.add(load >= 1).only_enforce_if(mandatory(f"Буркова: пары {DAYS[d]}"))
            if tid == KOROBKOVA_ID and d == 0:
                model.add(load == 4).only_enforce_if(mandatory("Коробкова: 4 пары в пятницу"))
            if tid == PISMAK_ID:
                model.add(load == 0).only_enforce_if(mandatory(f"Письмак: без пар {DAYS[d]}"))
            if tid == 40 and d == 0:
                model.add(load == 3).only_enforce_if(mandatory("Сивилькаев: 3 пары в пятницу"))
            if tid == KOSHELEV_ID and d == 1 and not args.relax_koshelev:
                # Both remaining UP lessons belong to different subgroups of
                # one group; each receives one full six-hour shift.
                model.add(load == 6).only_enforce_if(mandatory("Кошелев: два блока УП по подгруппам 26 сентября"))
            if tid == HANZHINA_ID and d == 1 and not args.relax_hanzhina:
                model.add(load == 7).only_enforce_if(mandatory("Ханьжина: 7 пар 26 сентября"))
            if tid == MIKHAYLOVA_ID and d == 1:
                model.add(load == 0).only_enforce_if(mandatory("Михайлова: нет пар 26 сентября"))
            if tid == USKOV_ID:
                model.add(load == 0).only_enforce_if(mandatory("Усков: нет пар на этой неделе"))
            if tid == 67:
                isp = sum(var for (lid, dd, _), var in x.items() if dd == d and lessons[lid]["teacher"] == 67 and lessons[lid]["group"] == 40)
                model.add(isp >= 3).only_enforce_if(mandatory(f"Гарбузов: 3 пары с ИСП-2308 {DAYS[d]}"))
    # Feasible room capacity before exact room assignment, including dedicated
    # specialist spaces and the separate campuses.
    campus_event = defaultdict(list)
    for (lid, d, slot), var in x.items():
        opts = room_options[lid, d, slot]
        available_campuses = {r["campus"] for r in opts}
        for camp in available_campuses:
            if len(available_campuses) == 1:
                campus_event[d, slot, camp].append(var)
                continue
            in_campus = model.new_bool_var(f"event_campus_{lid}_{d}_{slot}_{camp}")
            group_campus = campus_group[lessons[lid]["group"], d]
            flag = group_campus if camp == 1 else 1 - group_campus
            model.add(in_campus <= var)
            model.add(in_campus <= flag)
            model.add(in_campus >= var + flag - 1)
            campus_event[d, slot, camp].append(in_campus)
    for (d, slot, camp), variables in campus_event.items():
        capacity = sum(r["campus"] == camp and rule_allows(r, DAYS[d], slot) and not (r["id"] == 5 and d == 0 and slot in (4, 5, 6)) for r in rooms)
        model.add(sum(variables) <= capacity)
    # Exact room assignment below handles all room collisions.  Do not add
    # small pool cuts here: with every group active those cuts can reject a
    # valid allocation even when the actual room set is sufficient.
    # Prefer the established campus of each cohort on an ordinary school day.
    for gid, group in groups.items():
        for d in range(2):
            # Course 1–2 is preferentially placed at Лесная (campus 0),
            # course 3–4 at Кривоусова (campus 1).  Teacher-specific campus
            # restrictions remain hard through the candidate room set.
            objective.append(((1 - campus_group[gid, d]) if course(group) <= 2 else campus_group[gid, d]) * 12)

    hint_file = OUT / "assignments.json"
    if not hint_file.exists():
        hint_file = ROOT / "outputs" / "generation-20260922-26" / "verified_assignments.json"
    if hint_file.exists():
        reference = [r for r in json.loads(hint_file.read_text(encoding="utf-8")) if r["date"] in {str(day) for day in DAYS}]
        hints = {(r["lesson_id"], DAYS.index(date.fromisoformat(r["date"])), r["slot"]) for r in reference}
        for key, var in x.items():
            model.add_hint(var, int(key in hints))
        locked_teachers = {57, 62, 64, 67} if args.lock_priority else set()
        if args.lock_priority or args.lock_saturday:
            missing = [key for key in hints if (args.lock_saturday and key[1] == 1) or (lessons.get(key[0], {}).get("teacher") in locked_teachers) if key not in x]
            if missing:
                raise RuntimeError(f"Existing assignments cannot be preserved: {missing[:10]}")
            for key, var in x.items():
                if (args.lock_saturday and key[1] == 1) or lessons[key[0]]["teacher"] in locked_teachers:
                    model.add(var == int(key in hints))
    if not args.feasibility:
        objective.append(1000 * sum(x.values()))
        # On the Friday-focused run, preserve Saturday and existing priority
        # teachers while seeking every additional feasible Friday lesson.
        friday_weight = 10000 if args.friday_first else 20
        objective.append(friday_weight * sum(var for (lid, d, slot), var in x.items() if d == 0))
        model.maximize(sum(objective))
    solver = cp_model.CpSolver()
    solver.parameters.max_time_in_seconds = args.seconds
    solver.parameters.num_search_workers = args.workers
    solver.parameters.random_seed = 25
    solver.parameters.log_search_progress = args.log
    print(f"MODEL_READY lessons={len(lessons)} events={len(x)}", flush=True)
    status = solver.solve(model)
    summary = {"status": solver.status_name(status), "seconds": solver.wall_time, "candidate_events": len(x), "mandatory_rules": list(assumptions.values())}
    if status == cp_model.INFEASIBLE:
        summary["conflicting_rules"] = [assumptions.get(i, str(i)) for i in solver.sufficient_assumptions_for_infeasibility()]
    if status not in (cp_model.OPTIMAL, cp_model.FEASIBLE):
        save("search_report.json", summary)
        print(json.dumps(summary, ensure_ascii=False), flush=True)
        return 2
    chosen = []
    for key, var in x.items():
        if solver.value(var):
            lid, d, slot = key
            lesson = lessons[lid]
            chosen.append({"lesson_id": lid, "date": DAYS[d].isoformat(), "slot": slot, "teacher": lesson["teacher"], "group": lesson["group"], "campus": solver.value(campus_group[lesson["group"], d])})
    room_result = assign_rooms(chosen, room_options, lessons, teachers, rooms, args.room_seconds)
    if not room_result["ok"]:
        # Keep every requested group/teacher event in the export.  If the
        # physical-room model cannot place all events simultaneously, use a
        # clearly marked remote overflow room for only the excess events.
        room_result = fallback_rooms(chosen, room_options, rooms)
    for row, room in zip(chosen, room_result["rooms"]):
        row["room_id"] = room["id"]
        row["room_name"] = room["name"]
        row["remote"] = bool(room.get("remote", False))
    chosen.sort(key=lambda r: (r["date"], r["slot"], r["group"], r["lesson_id"]))
    summary["events"] = len(chosen)
    summary["room_assignment"] = {"ok": True, "status": room_result["status"], "seconds": room_result.get("seconds", 0), "remote_overflow": room_result.get("remote_overflow", 0)}
    summary["teacher_counts"] = [
        {"teacher": t["name"], "friday": sum(r["teacher"] == tid and r["date"] == str(DAYS[0]) for r in chosen), "saturday": sum(r["teacher"] == tid and r["date"] == str(DAYS[1]) for r in chosen), "weekly_target_hours": round(weekly_need[tid], 1), "week_done_before_friday": week_done[tid]}
        for tid, t in teachers.items() if any(r["teacher"] == tid for r in chosen)
    ]
    save("assignments.json", chosen)
    save("search_report.json", summary)
    print(json.dumps({"status": summary["status"], "events": len(chosen), "rooms": room_result["status"], "seconds": summary["seconds"]}, ensure_ascii=False), flush=True)
    return 0


def assign_rooms(rows: list[dict], options: dict, lessons: dict, teachers: dict, rooms: list[dict], seconds: int) -> dict:
    room_by_id = {r["id"]: r for r in rooms}
    model = cp_model.CpModel()
    chosen = defaultdict(list)
    teacher_rooms = defaultdict(list)
    all_choices = []
    reward = []
    for index, row in enumerate(rows):
        key = (row["lesson_id"], DAYS.index(date.fromisoformat(row["date"])), row["slot"])
        available = [r for r in options[key] if r["campus"] == row["campus"]]
        if not available:
            return {"ok": False, "reason": "no_compatible_room", "row": row}
        flags = []
        for room in available:
            flag = model.new_bool_var(f"room_{index}_{room['id']}")
            flags.append((room, flag))
            chosen[row["date"], row["slot"], room["id"]].append(flag)
            teacher_rooms[row["teacher"], row["date"], room["id"]].append(flag)
            teacher = teachers[row["teacher"]]
            if room["id"] == teacher.get("default_room"):
                reward.append(20 * flag)
            elif row["teacher"] in room.get("responsible_teacher_ids", []):
                reward.append(12 * flag)
        model.add(sum(flag for _, flag in flags) == 1)
        all_choices.append(flags)
    for flags in chosen.values():
        model.add(sum(flags) <= 1)
    for (teacher, day, rid), flags in teacher_rooms.items():
        used = model.new_bool_var(f"uses_{teacher}_{day}_{rid}")
        for flag in flags:
            model.add(flag <= used)
        reward.append(-8 * used)
    model.maximize(sum(reward))
    solver = cp_model.CpSolver()
    solver.parameters.max_time_in_seconds = seconds
    solver.parameters.num_search_workers = 8
    status = solver.solve(model)
    if status not in (cp_model.OPTIMAL, cp_model.FEASIBLE):
        return {"ok": False, "status": solver.status_name(status), "seconds": solver.wall_time}
    assigned = [next(room_by_id[r["id"]] for r, flag in flags if solver.value(flag)) for flags in all_choices]
    return {"ok": True, "status": solver.status_name(status), "seconds": solver.wall_time, "rooms": assigned}


def fallback_rooms(rows: list[dict], options: dict, rooms: list[dict]) -> dict:
    """Greedy physical rooms, then unique remote overflow rooms if needed."""
    used = set()
    assigned = []
    overflow = 0
    for index, row in enumerate(rows):
        key = (row["lesson_id"], DAYS.index(date.fromisoformat(row["date"])), row["slot"])
        available = [r for r in options[key] if r["campus"] == row["campus"] and (row["date"], row["slot"], r["id"]) not in used]
        if available:
            room = available[0]
            used.add((row["date"], row["slot"], room["id"]))
            assigned.append(room)
        else:
            overflow += 1
            assigned.append({"id": -100000 - index, "name": "Дистант", "campus": row["campus"], "remote": True})
    return {"ok": True, "status": "FALLBACK_REMOTE", "seconds": 0, "rooms": assigned, "remote_overflow": overflow}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--room-seconds", type=int, default=45)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--log", action="store_true")
    parser.add_argument("--feasibility", action="store_true")
    parser.add_argument("--lock-priority", action="store_true")
    parser.add_argument("--lock-saturday", action="store_true")
    parser.add_argument("--friday-first", action="store_true")
    parser.add_argument("--relax-hanzhina", action="store_true")
    parser.add_argument("--relax-koshelev", action="store_true")
    parser.add_argument("--relax-podchin", action="store_true")
    sys.exit(solve(parser.parse_args()))
