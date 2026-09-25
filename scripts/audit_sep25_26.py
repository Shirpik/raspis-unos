"""Independent audit for the 25–26 September candidate."""
import json
from collections import Counter, defaultdict
from datetime import date
from pathlib import Path
from teaching_balance import course

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "outputs" / "generation-20260925-26"
DAYS = ("2026-09-25", "2026-09-26")
AI = {"Абрамчук", "Азарян", "Ахметов", "Вагайская", "Дроговейко", "Ермолина", "Журавский", "Комарова", "Конева", "Михайлова", "Нифонтова", "Новосёлова", "Рахматулина", "Рабенок", "Саламатина", "Самцов", "Семенова", "Серянина", "Сивилькаев", "Синельникова", "Силенок", "Соболева", "Тарасов", "Тимеров", "Третяк", "Хасанова", "Черепанова", "Шадрина"}
MORNING = {"ТАКХС-Пф-2202", "МЦМ-Пф-301", "ТЭиРП-2901", "ТАКХС-3201", "ТМ-2417", "ТМ-3416п", "МЦМ-Пф-202", "СП-4611", "ОСА-491", "ТМ-4413"}

def main():
    data = json.loads((ROOT / "data" / "timetable_data.json").read_text(encoding="utf-8"))
    rows = json.loads((OUT / "assignments.json").read_text(encoding="utf-8"))
    lessons = {x["id"]: x for x in data["lessons"]}
    teachers = {x["id"]: x for x in data["teachers"]}
    groups = {x["id"]: x for x in data["groups"]}
    rooms = {x["id"]: x for x in data["rooms"]}
    errors, warnings = [], []
    teacher_slot, subgroup_slot, room_slot = Counter(), Counter(), Counter()
    teacher_slots, subgroup_slots = defaultdict(set), defaultdict(set)
    used_hours = Counter()
    by_teacher_day = Counter()
    by_group_day = Counter()
    podchin = defaultdict(list)
    up_by_group_day = defaultdict(list)
    for r in rows:
        l, g, t = lessons[r["lesson_id"]], groups[r["group"]], teachers[r["teacher"]]
        key = (r["date"], r["slot"])
        teacher_slot[(r["teacher"],) + key] += 1
        teacher_slots[(r["teacher"], r["date"])].add(r["slot"])
        room_slot[key + (r["room_id"],)] += 1
        by_teacher_day[r["teacher"], r["date"]] += 1
        by_group_day[r["group"], r["date"]] += 1
        if r["teacher"] == 57:
            podchin[r["date"]].append(r)
        if l.get("is_block") and l["name"].startswith("УП"):
            up_by_group_day[r["group"], r["date"]].append(r)
        used_hours[l["id"]] += 2
        if r["teacher"] in {5, 8, 35, 82} or (r["date"] == DAYS[0] and r["teacher"] in {4, 25, 47, 50, 60, 68}) or (r["date"] == DAYS[1] and r["teacher"] == 65):
            errors.append(["forbidden_teacher", t["name"], r["date"], r["slot"]])
        if r["date"] == DAYS[0] and t["name"].split()[0] in AI and r["slot"] >= 4:
            errors.append(["ai_block", t["name"], r["slot"]])
        if r["teacher"] == 44:
            errors.append(["pismak_forbidden", r["date"], r["slot"]])
        if r["teacher"] == 21:
            errors.append(["komarova_forbidden", r["date"], r["slot"]])
        if r["date"] == DAYS[1] and r["teacher"] == 28:
            errors.append(["novoselova_saturday_forbidden", r["slot"]])
        if r["date"] == DAYS[1] and g["name"] in MORNING and r["slot"] > 4:
            errors.append(["morning_group_late", g["name"], r["slot"]])
        if r["date"] == DAYS[1] and g["name"] in {x["name"] for x in groups.values() if course(x) == 1} and g["name"] not in {"РУП-1301п", "ТМ-1420"} and r["slot"] > 3:
            errors.append(["first_course_late", g["name"], r["slot"]])
        if l["teacher"] == 57 and (not l.get("is_block") or not g["name"].startswith("СП-Пф-36") or not l["name"].startswith("УП")):
            errors.append(["podchinennov_non_third_course", g["name"], l["name"]])
        for group_id, part in _parts(l, g):
            subgroup_slot[group_id, part, r["date"], r["slot"]] += 1
            subgroup_slots[group_id, part, r["date"]].add(r["slot"])
    for key, n in teacher_slot.items():
        if n > 1: errors.append(["teacher_collision", key, n])
    for key, n in subgroup_slot.items():
        if n > 1: errors.append(["student_collision", key, n])
    for key, n in room_slot.items():
        if n > 1: errors.append(["room_collision", key, n])
    for key, slots in subgroup_slots.items():
        if len(slots) > 4: errors.append(["student_over_4", key, sorted(slots)])
        if slots and list(sorted(slots)) != list(range(min(slots), max(slots) + 1)):
            errors.append(["student_window", key, sorted(slots)])
    for gid, group in groups.items():
        if group["name"] not in MORNING:
            continue
        for part in range(max(1, group.get("parts", 2))):
            slots = subgroup_slots.get((gid, part, DAYS[1]), set())
            if len(slots) < 2 or len(slots) > 4:
                errors.append(["handwritten_group_load", group["name"], part + 1, sorted(slots)])
        group_slots = {r["slot"] for r in rows if r["group"] == gid and r["date"] == DAYS[1]}
        if group_slots and min(group_slots) != 1:
            errors.append(["handwritten_group_late_start", group["name"], sorted(group_slots)])
    for (gid, day), up_rows in up_by_group_day.items():
        other = [r for r in rows if r["group"] == gid and r["date"] == day and r not in up_rows]
        if other:
            errors.append(["lesson_with_up", groups[gid]["name"], day, len(other)])
        up_by_lesson = defaultdict(list)
        for r in up_rows:
            up_by_lesson[r["lesson_id"]].append(r["slot"])
        if len(up_by_lesson) > 2 or any(sorted(slots) not in ([1, 2, 3], [5, 6, 7]) for slots in up_by_lesson.values()):
            errors.append(["invalid_up_shift", groups[gid]["name"], day, sorted(r["slot"] for r in up_rows)])
        if len(up_by_lesson) == 2:
            parts = [lessons[lid].get("subgroup", -1) for lid in up_by_lesson]
            shifts = {tuple(sorted(slots)) for slots in up_by_lesson.values()}
            if -1 in parts or len(set(parts)) != 2 or shifts != {(1, 2, 3), (5, 6, 7)}:
                errors.append(["up_same_subgroup", groups[gid]["name"], day, parts])
    totals = {}
    for lid, h in used_hours.items():
        l = lessons[lid]
        totals[lid] = {"subject": l["name"], "used": h, "total": l.get("total_hours", 0), "remaining_after": l.get("total_hours", 0) - h}
        if h > l.get("total_hours", 0): errors.append(["lesson_hours_overflow", lid, h, l.get("total_hours", 0)])
    for d in DAYS:
        ps = podchin[d]
        groups_used = {groups[r["group"]]["name"] for r in ps}
        if len(ps) != 6 or len(groups_used) != 2: errors.append(["podchinennov_daily_up", d, len(ps), sorted(groups_used)])
        for name in groups_used:
            slots = sorted(r["slot"] for r in ps if groups[r["group"]]["name"] == name)
            if slots not in ([1, 2, 3], [5, 6, 7]): errors.append(["podchinennov_block", d, name, slots])
    if by_teacher_day[9, DAYS[0]] != 2:
        errors.append(["sutyagin_friday", by_teacher_day[9, DAYS[0]]])
    rabenok_friday = [r for r in rows if r["teacher"] == 12 and r["date"] == DAYS[0]]
    if len(rabenok_friday) != 1 or [r["slot"] for r in rabenok_friday] != [2] or any(groups[r["group"]]["name"] != "ТМ-1420" for r in rabenok_friday):
        errors.append(["rabenok_friday", [(groups[r["group"]]["name"], r["slot"]) for r in rabenok_friday]])
    for day in DAYS:
        for gid, group in groups.items():
            if not any(r["group"] == gid and r["date"] == day for r in rows):
                errors.append(["group_without_classes", group["name"], day])
        for tid, name in ((16, "Буркова"),):
            if by_teacher_day[tid, day] < 1:
                errors.append(["teacher_without_classes", name, day])
    if by_teacher_day[39, DAYS[0]] != 4 or by_teacher_day[39, DAYS[1]] != 0:
        errors.append(["korobkova_load", by_teacher_day[39, DAYS[0]], by_teacher_day[39, DAYS[1]]])
    for r in rows:
        if r["teacher"] == 7 and r["slot"] > 2:
            errors.append(["kobylyanskaya_late", r["date"], r["slot"]])
        if r["teacher"] == 24 and r["slot"] > 5:
            errors.append(["kruglova_late", r["date"], r["slot"]])
    for day in DAYS:
        target = 0
        if by_teacher_day[44, day] != target:
            errors.append(["pismak_load", day, by_teacher_day[44, day], target])
    if by_teacher_day[40, DAYS[0]] != 3:
        errors.append(["sivilkaev_friday", by_teacher_day[40, DAYS[0]]])
    if by_teacher_day[68, DAYS[1]] != 7:
        errors.append(["hanzhina_saturday", by_teacher_day[68, DAYS[1]]])
    if by_teacher_day[51, DAYS[1]] != 6:
        errors.append(["koshelev_saturday_feasible_max", by_teacher_day[51, DAYS[1]]])
    for tid, target in ((62, 7), (64, 7), (67, 7)):
        for d in DAYS:
            if by_teacher_day[tid, d] != target: errors.append(["heavy_load", teachers[tid]["name"], d, by_teacher_day[tid, d], target])
    for d in DAYS:
        isp = sum(1 for r in rows if r["date"] == d and r["teacher"] == 67 and groups[r["group"]]["name"] == "ИСП-2308")
        if isp < 3: errors.append(["garbuzov_isp2308", d, isp])
    for name in MORNING:
        if not any(r["date"] == DAYS[1] and groups[r["group"]]["name"] == name and r["slot"] == 1 for r in rows):
            errors.append(["morning_group_missing_first_pair", name])
    for d in DAYS:
        if not any(r["date"] == d and groups[r["group"]]["name"] == "ИСП-3307п" for r in rows):
            errors.append(["isp3307_missing", d])
    report = {"passed": not errors, "errors": errors, "warnings": warnings, "events": len(rows), "teacher_counts": [{"teacher": teachers[t]["name"], "friday": by_teacher_day[t, DAYS[0]], "saturday": by_teacher_day[t, DAYS[1]]} for t in sorted({r["teacher"] for r in rows})], "lesson_hours": totals}
    (OUT / "audit_report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": report["passed"], "errors": len(errors), "warnings": len(warnings), "events": len(rows)}, ensure_ascii=False))
    return 0 if not errors else 2

def _parts(lesson, group):
    count = max(1, int(group.get("parts", 2)))
    sg = int(lesson.get("subgroup", -1))
    if sg < 0: return [(group["id"], p) for p in range(count)]
    local = sg - group["id"] * 2
    return [(group["id"], local)] if 0 <= local < count else []

if __name__ == "__main__": raise SystemExit(main())
