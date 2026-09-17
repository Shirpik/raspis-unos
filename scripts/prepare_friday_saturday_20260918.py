"""Build a locked 18–19 September draft from confirmed teaching balances.

The script is deliberately isolated: it writes a generation snapshot under an
output folder and never overwrites the semester curriculum or the confirmed
teaching ledger.  The CP-SAT quota pass enforces four consecutive pairs for
every physical subgroup on *each* day before the timetable solver assigns
rooms.
"""
from __future__ import annotations

import argparse
import copy
import json
import math
import subprocess
from collections import defaultdict
from datetime import date, timedelta
from pathlib import Path

from prepare_one_week_generation import lesson_parts, rule_allows, unavailable_dates
from teaching_balance import balances, deadline as group_deadline, calendar_allows

ROOT = Path(__file__).resolve().parents[1]
DAYS = (date(2026, 9, 18), date(2026, 9, 19))
HEAVY = {"Меренчуков": 7, "Гарбузов": 7, "Вальдиянов": 7,
         "Саламатина": 7, "Комарова": 7, "Письмак": 7, "Ахметов": 7}
SOFT_PRIORITY = set(HEAVY) | {"Кошелев", "Тимеров", "Буркова", "Усков"}
DAILY_MINIMUMS = {"Кошелев": (0, 7), "Буркова": (6, 6),
                  "Тимеров": (6, 6),
                  "Дроговейко": (2, 2), "Усков": (7, 1),
                  "Осипчук": (7, 7), "Рабенок": (5, 5), "Самцов": (6, 7),
                  "Сивилькаев": (6, 6), "Рахматулина": (5, 5), "Семенова": (6, 6), "Новосёлова": (4,0),
                  "Цимфер": (2,2), "Кропотова": (2,2), "Коробкова": (4,4), "Ханьжина": (0,7)}
HARD_DAILY = set(HEAVY) | {'Тимеров','Кошелев','Дроговейко','Рабенок','Самцов','Осипчук','Усков','Сивилькаев','Рахматулина','Семенова','Михайлова','Буркова','Новосёлова','Цимфер','Кропотова','Коробкова','Ханьжина'}


def write(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def course(group: dict) -> int:
    tail = str(group["name"]).rsplit("-", 1)[-1]
    return int(tail[0]) if tail and tail[0].isdigit() else 0


def prepare(source: dict, folder: Path) -> None:
    data = copy.deepcopy(source)
    groups = {g["id"]: g for g in data["groups"]}
    teachers = {t["id"]: t for t in data["teachers"]}
    rooms = {r["id"]: r for r in data["rooms"]}
    blocked = unavailable_dates(data)
    last = date.fromisoformat(data["settings"]["semester_end_date"])

    def named(prefix: str) -> dict:
        return next(t for t in teachers.values() if t["name"].startswith(prefix + " "))

    # Explicit instructions for this distance Friday/Saturday.  Date overrides
    # preserve the regular recurring availability in the main dataset.
    for prefix in ("Саламатина", "Буркова", "Меренчуков", "Гарбузов", "Вальдиянов",
                   "Тимеров", "Рабенок", "Комарова", "Дроговейко", "Усков", "Осипчук", "Письмак", "Ахметов", "Коробкова"):
        t = named(prefix)
        for day in DAYS:
            t["date_slot_overrides"] = [x for x in t.get("date_slot_overrides", []) if x["date"] != day.isoformat()]
            t["date_slot_overrides"].append({"date": day.isoformat(), "slots": list(range(1, 8))})
        t["max_pairs_per_day"] = 7
        if t.get("max_work_days_per_week", 0):
            t["max_work_days_per_week"] = max(2, int(t["max_work_days_per_week"]))
    koby = named("Кобылянская")
    for day, slots in zip(DAYS, ([1, 2], [])):
        koby["date_slot_overrides"] = [x for x in koby.get("date_slot_overrides", []) if x["date"] != day.isoformat()]
        koby["date_slot_overrides"].append({"date": day.isoformat(), "slots": slots})
    tretyak = named("Третяк")
    tretyak["scheduling_active"] = True
    tretyak["availability_note"] = "Дистанционные теоретические занятия 18–19.09.2026"
    for day in DAYS:
        tretyak["date_slot_overrides"] = [x for x in tretyak.get("date_slot_overrides", []) if x["date"] != day.isoformat()]
        tretyak["date_slot_overrides"].append({"date": day.isoformat(), "slots": list(range(1, 8))})
    tretyak["max_pairs_per_day"] = 7
    tretyak["max_work_days_per_week"] = 2

    for prefix, overrides in (('Ханьжина', (None, list(range(1,8)))), ('Тарасов', ([],[])), ('Садриева', ([],[])), ('Семенова', (None, list(range(1,8)))),
                              ('Круглова', (list(range(1,6)), list(range(1,6)))),
                              ('Михайлова Татьяна', (list(range(1,6)), list(range(1,6)) if data['settings'].get('mikhailova_saturday') else []))):
        t = named(prefix)
        for day, slots in zip(DAYS, overrides):
            if slots is None: continue
            t['date_slot_overrides'] = [x for x in t.get('date_slot_overrides', []) if x['date'] != day.isoformat()]
            t['date_slot_overrides'].append({'date':day.isoformat(),'slots':slots})
        if prefix in ('Круглова','Михайлова Татьяна'): t['max_pairs_per_day'] = 5
    named('Рабенок')['max_pairs_per_day'] = 6
    for prefix in ('Цимфер','Кропотова'): named(prefix)['max_pairs_per_day']=3
    named('Коробкова')['max_pairs_per_day']=4

    # Saturday curators may teach so that every available student subgroup gets
    # four lessons.  Curator-only staff remain excluded.
    curators = {g.get("curator_teacher") for g in groups.values()}
    for t in teachers.values():
        if t["id"] not in curators or t.get("curator_only"):
            continue
        if any(x['date'] == DAYS[1].isoformat() for x in t.get('date_slot_overrides', [])):
            continue
        if any(rule_allows(t, DAYS[1], slot) for slot in range(1, 8)):
            continue
        slots = sorted({slot for w in t.get("work_days", []) if w.get("enabled") for slot in w.get("slots", [])})
        if slots:
            t["date_slot_overrides"] = [x for x in t.get("date_slot_overrides", []) if x["date"] != DAYS[1].isoformat()]
            t["date_slot_overrides"].append({"date": DAYS[1].isoformat(), "slots": slots})

    lesson_by_id = {l["id"]: l for l in data["lessons"]}
    confirmed, reserved, prior_entries = balances(data, DAYS[0])
    theory_before, rabenoк_groups = defaultdict(int), set()
    for entry in prior_entries:
        lesson = lesson_by_id.get(entry["lesson_id"], {})
        if lesson.get("teacher") == named("Рабенок")["id"] and entry['status'] == 'confirmed':
            rabenoк_groups.add(lesson.get("group"))
        if not any(lesson.get(flag, False) for flag in ("is_lab", "is_block", "is_pp")):
            theory_before[(lesson.get("group"), lesson.get("subject_id"))] += int(entry.get("hours", 0)) // 2
    for item in data["settings"].get("prior_theory_pairs", []):
        key = (item["group"], item["subject"])
        theory_before[key] = max(theory_before[key], int(item["pairs"]))

    def group_available(group: dict, current: date) -> bool:
        if not calendar_allows(group, current) or current > deadline(group):
            return False
        if any(p["from"] <= current.isoformat() <= p["to"] for p in group.get("practice_periods", [])):
            return False
        return not any((u.get("group") == group["id"] or u.get('all_groups')) and
                       (current.isoformat() in u.get("dates", []) or
                        u.get("from", "9999") <= current.isoformat() <= u.get("to", "0000"))
                       for u in data.get("unavailable", []))

    def deadline(group: dict) -> date:
        return group_deadline(group, data['settings'])

    def candidates(lesson: dict, teacher: dict) -> list[dict]:
        result = []
        for room in rooms.values():
            if not room.get("active", True) or room.get("access_mode") == "blocked":
                continue
            if room.get("access_mode") == "exclusive" and teacher["id"] not in room.get("responsible_teacher_ids", []):
                continue
            if lesson.get("fixed_room", -1) >= 0 and room["id"] != lesson["fixed_room"]:
                continue
            if lesson.get("allowed_campuses") and room["campus"] not in lesson["allowed_campuses"]:
                continue
            if teacher.get("allowed_campuses") and room["campus"] not in teacher["allowed_campuses"]:
                continue
            if set(lesson.get("required_equipment", [])) - set(room.get("equipment", [])):
                continue
            if lesson.get("required_room_type", 0) and room.get("room_type") != lesson["required_room_type"]:
                continue
            if lesson.get("required_capacity", 0) and room.get("capacity", 0) < lesson["required_capacity"]:
                continue
            if (lesson.get("required_room_purpose") == "sports_hall") != (room.get("purpose") == "sports_hall"):
                continue
            result.append(room)
        return result

    variables, theories, labs, arithmetic = [], defaultdict(list), defaultdict(list), defaultdict(float)
    balance_report = []
    for lesson in data["lessons"]:
        if not lesson.get("curriculum_active", True) or not lesson.get('plan_active', True) or lesson.get("is_block") or lesson.get("is_pp"):
            continue
        group, teacher = groups[lesson["group"]], teachers.get(lesson["teacher"])
        if not teacher or not teacher.get("scheduling_active", True):
            continue
        surname = teacher["name"].split()[0]
        if surname in ("Тимеров", "Усков") and course(group) != 3:
            continue
        if surname == "Третяк" and (lesson.get("is_lab") or lesson.get('subgroup', -1) >= 0):
            continue
        remaining = max(0, int(lesson.get("total_hours", 0)) - confirmed[lesson["id"]] - reserved[lesson['id']])
        if remaining < 2:
            continue
        end = deadline(group)
        for rule in teacher.get('desired_load_rules', []):
            if rule.get('deadline') and (not rule.get('course_year') or rule['course_year'] == course(group)) and \
                    (not rule.get('group_ids') or group['id'] in rule['group_ids']):
                end = min(end, date.fromisoformat(rule['deadline']))
        available_days = 0
        d = DAYS[0]
        while d <= end:
            if d.isoweekday() <= 6 and d not in blocked[teacher["id"]] and group_available(group, d) and any(rule_allows(group, d, s) and rule_allows(teacher, d, s) for s in range(1, 8)):
                available_days += 1
            d += timedelta(days=1)
        arithmetic[teacher["id"]] += remaining / 2 / max(1, available_days)
        # Distance lessons have no physical room/campus capacity restrictions.
        usable_rooms = list(rooms.values())
        slots = [day_index * 7 + slot - 1 for day_index, day in enumerate(DAYS)
                 if day not in blocked[teacher["id"]] and group_available(group, day)
                 for slot in range(1, 8) if rule_allows(group, day, slot) and rule_allows(teacher, day, slot)
                 ]
        paired_lab = lesson.get('is_lab') and group['name'].startswith(('ПКД-', 'СП-', 'ТОРД-', 'ТОиРА-', 'ТАКХС-'))
        step = 2 if lesson.get("consecutive_pairs") == 2 or paired_lab else 1
        if paired_lab:
            lesson['consecutive_pairs'] = 2
            if group['name'].startswith('ПКД-'):
                lesson['block_start_slots'] = [0, 2, 4, 5]
        max_per_day = 4 if lesson.get("subgroup", -1) < 0 else (4 if teacher["id"] == 51 else 3)
        maximum = min(remaining // 2, max_per_day * 2, len(slots))
        maximum -= maximum % step
        if surname == 'Рабенок':
            maximum = min(maximum, 1)
        if not slots or maximum <= 0:
            continue
        parts = [group_id * 2 + part for group_id, part in lesson_parts(lesson, groups)]
        period_days = len({s // 7 for s in slots})
        period_target = remaining / 2 * period_days / max(1, available_days)
        balance_report.append({'lesson_id': lesson['id'], 'group': group['name'], 'teacher': teacher['name'],
                               'subject': lesson['name'], 'planned_hours': lesson['total_hours'],
                               'confirmed_hours': confirmed[lesson['id']], 'reserved_hours': reserved[lesson['id']],
                               'remaining_hours': remaining, 'deadline': end.isoformat(),
                               'remaining_calendar_weeks': max(0, (end-DAYS[0]).days+1)/7,
                               'available_teacher_days': available_days, 'period_target_pairs': period_target})
        variables.append({"id": lesson["id"], "minimum": 0, "maximum": maximum,
                          "semester_total": remaining // 2, "target_pairs_milli": round(period_target * 1000),
                          "priority_weight": max(1, 100 // max(1, math.ceil((end - DAYS[0]).days / 7))),
                          "teacher": teacher["id"], "group": group["id"], "parts": parts,
                          "part_weight": len(parts), "whole_group": lesson.get("subgroup", -1) < 0,
                          "subject": str(lesson["subject_id"]), "allowed_slots": slots,
                          "allowed_campuses": [0],
                          "consecutive_pairs": step, "avoid_lunch_split": lesson.get("avoid_lunch_split", False),
                          "block_start_slots": lesson.get("block_start_slots", []),
                          "restricted_room": -1, "sports_room": False, "computer_room": False})
        if teacher["id"] == 51:
            # Dispatcher removed the usual three-pair subject cap for
            # Koshelev on this Saturday: 4 + 3 LPZ pairs are allowed.
            variables[-1]["daily_subject_limits"] = [{"day": 1, "maximum": 4}]
        (labs if lesson.get("is_lab") else theories)[(group["id"], lesson["subject_id"])].append(lesson["id"])

    variables_by_teacher = defaultdict(list)
    for variable in variables:
        variables_by_teacher[variable["teacher"]].append(variable)
    teachers_model, target_report = [], []
    for teacher in teachers.values():
        day_caps = [min(teacher.get('max_pairs_per_day') or 7, 7, sum(rule_allows(teacher, day, s) for s in range(1, 8))) if day not in blocked[teacher["id"]] else 0 for day in DAYS]
        surname = teacher["name"].split()[0]
        requested = HEAVY.get(surname)
        daily = DAILY_MINIMUMS.get(surname)
        if surname == 'Буркова': daily=(5,7)
        if teacher['name'].startswith('Михайлова Татьяна'): daily=(5,5 if data['settings'].get('mikhailova_saturday') else 0)
        if requested:
            daily = (requested, requested)
        targets = []
        for index, cap in enumerate(day_caps):
            # Arithmetic load guides the objective through the remaining
            # subjects, but only the dispatcher-requested teachers are hard
            # daily constraints. Making every teacher's average hard made a
            # four-pair student day mathematically infeasible.
            goal = min(cap, daily[index]) if daily else 0
            if daily and goal:
                targets.append({"day": index, "minimum": goal})
            data_target = goal if surname in HARD_DAILY else 0
            teacher["date_load_targets"] = [x for x in teacher.get("date_load_targets", []) if x["date"] != DAYS[index].isoformat()]
            if data_target:
                teacher["date_load_targets"].append({"date": DAYS[index].isoformat(), "minimum_pairs": data_target})
        maximum = sum(day_caps)
        # A full 90-subgroup, two-day layout has a fixed student load. Keep
        # teacher priorities in the report for the second placement pass; the
        # first CP-SAT pass must establish a feasible four-pair skeleton.
        minimum = min(maximum, math.ceil(arithmetic[teacher['id']] * sum(cap > 0 for cap in day_caps)))
        hard_days = targets if surname in HARD_DAILY else []
        hard_min = sum(x['minimum'] for x in hard_days)
        if surname in ('Дроговейко','Тимеров','Коробкова'):
            maximum = hard_min
        if surname == 'Рабенок':
            hard_min = maximum = len({v['group'] for v in variables_by_teacher[teacher['id']]})
        if surname == 'Третяк':
            theory_pairs = sum(v['semester_total'] for v in variables_by_teacher[teacher['id']])
            hard_min = maximum = max(0, theory_pairs - 2)
        preserved = next((x['pairs'] for x in data['settings'].get('preserve_teacher_period_loads',[]) if x['teacher']==teacher['id']),0)
        hard_min = max(hard_min,preserved)
        preferred = next((x['pairs'] for x in data['settings'].get('preferred_teacher_period_loads',[]) if x['teacher']==teacher['id']),0)
        minimum = max(minimum,preferred)
        teachers_model.append({"id": teacher["id"], "minimum": min(maximum,max(minimum,hard_min)), "hard_minimum": hard_min,
                               "maximum": maximum, "maximum_daily": teacher.get('max_pairs_per_day') or 7, "day_targets": hard_days})
        if maximum:
            target_report.append({"teacher_id": teacher["id"], "name": teacher["name"],
                                  "arithmetic_pairs_per_workday": round(arithmetic[teacher["id"]], 3),
                                  "daily_minimums": [next((x["minimum"] for x in targets if x["day"] == i), 0) for i in range(2)],
                                  "maximum": day_caps})

    part_ids = defaultdict(list)
    for variable in variables:
        for part in variable["parts"]:
            part_ids[part].append(variable["id"])
    def capacity(kind: str, campus: int, day: date, slot: int) -> int:
        return sum(room.get("active", True) and room.get("access_mode") != "blocked" and room["campus"] == campus and
                   ((kind == "sports" and room.get("purpose") == "sports_hall") or
                    (kind == "general" and room.get("purpose") != "sports_hall" and room.get("access_mode") != "exclusive")) and
                   rule_allows(room, day, slot) and (not room.get("available_slots") or slot in room["available_slots"])
                   for room in rooms.values())
    model = {"variables": variables, "teachers": teachers_model,
             "parts": [{"key": group_id * 2 + part, "group": group_id, "part": part,
                        "minimum_target": 8, "maximum_target": 8, "lesson_ids": part_ids[group_id * 2 + part]}
                       for group_id, group in groups.items() for part in range(group["parts"])],
             "lab_rules": [{"theory_ids": theories[key], "lab_ids": ids, "prior_theory": theory_before[key]}
                           for key, ids in labs.items() if theories[key] or theory_before[key]],
             "day_count": 2, "slots_per_day": 7, "min_student_pairs_per_day": 4,
             "max_student_pairs_per_day": 4, "require_all_student_days": True,
             "preferred_student_pairs_per_day": 4, "student_day_shortfall_weight": 100000,
             "hard_no_student_windows": True, "hard_no_teacher_windows": True,
             "whole_group_same_subject_limit": 2, "physical_part_same_subject_limit": 3,
             "allow_teacher_shortfalls": True, "teacher_shortfall_weight": 10000000,
             "teacher_overload_weight": 0, "maximize_part_load": False,
             "room_capacity_by_time": [[200,200]] * 14,
             "time_limit_seconds": 180, "workers": 8, "random_seed": 77}
    # Biology was absent from the original вклейки but is a real, continuing
    # subject for ТЭиРП-2901.  Put two more pairs into this two-day draft so
    # the optimiser must offer it instead of silently deferring it.
    biology_ids = [v["id"] for v in variables if v["group"] == 12 and v["subject"] == "10001"]
    if biology_ids:
        model["lesson_load_targets"] = [{"lesson_ids": biology_ids, "minimum": 2}]
    for gid in groups:
        ids = [v['id'] for v in variables if v['teacher'] == named('Рабенок')['id'] and v['group'] == gid]
        if ids:
            model.setdefault('lesson_load_targets', []).append({'lesson_ids':ids,'minimum':1,'maximum':1})
    write(folder / 'balance-inputs.json', balance_report)
    write(folder / "quota-model.json", model)
    write(folder / "teacher-targets.json", target_report)
    data["settings"]["start_date"], data["settings"]["end_date"] = (day.isoformat() for day in (DAYS[0], DAYS[1]))
    data['settings']['distance_learning'] = True
    data['settings']['distance_revision'] = 2
    data["settings"].setdefault("solver_config", {})["min_student_pairs_per_study_day"] = 4
    data["settings"]["solver_config"]["max_student_pairs_per_study_day"] = 4
    data["settings"]["automatic_period_quotas"] = False
    data["settings"]["teacher_period_targets"] = []
    data["settings"]["prior_theory_pairs"] = [{"group": group, "subject": subject, "pairs": pairs}
                                                  for (group, subject), pairs in theory_before.items()
                                                  if group is not None and subject is not None]
    write(folder / "base-data.json", data)
    print(json.dumps({"variables": len(variables), "parts": len(part_ids), "days": [d.isoformat() for d in DAYS]}, ensure_ascii=False))


def select(folder: Path) -> None:
    result = subprocess.run([str(ROOT / ".tmp/build-sep7/Release/quota_optimizer.exe"), str((folder / "quota-model.json").resolve())],
                            capture_output=True, text=True, encoding="utf-8", check=False)
    report = json.loads(result.stdout)
    write(folder / "quota-result.json", report)
    write(folder / "quota-result-relaxed.json", report)
    if not report.get("success"):
        raise RuntimeError(report)
    data = json.loads((folder / "base-data.json").read_text(encoding="utf-8-sig"))
    for lesson in data["lessons"]:
        lesson["total_slots"] = report["quotas"].get(str(lesson["id"]), 0)
        lesson["generation_active"] = lesson["total_slots"] > 0
    write(folder / "data/timetable_data.json", data)
    # The CP-SAT model uses zero-based time offsets; timetable lock payloads
    # use human pair numbers 1–7.
    locks = [{"lesson_id": int(lesson), "date": DAYS[time // 7].isoformat(), "slot": time % 7 + 1}
             for lesson, times in report["placement_witness"].items() for time in times]
    write(folder / "locks.json", {"source": "CP-SAT: остатки часов, практики, четыре пары ежедневно", "assignments": locks})
    print(json.dumps({"status": report["status"], "pairs": sum(report["quotas"].values()),
                      "teacher_shortfalls": report.get("teacher_shortfalls", [])}, ensure_ascii=False))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("prepare", "select"))
    parser.add_argument("--folder", default="outputs/friday-saturday-20260918")
    parser.add_argument("--data", default=str(ROOT / "data/timetable_data.json"))
    args = parser.parse_args()
    folder = Path(args.folder)
    if args.mode == "prepare":
        prepare(json.loads(Path(args.data).read_text(encoding="utf-8-sig")), folder)
    else:
        select(folder)
