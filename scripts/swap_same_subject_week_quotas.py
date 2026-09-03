"""Swap forced third daily repeats for another real subject of the same group."""

from __future__ import annotations

import json
import shutil
from collections import Counter, defaultdict
from datetime import datetime
from pathlib import Path

from prepare_one_week_generation import dates, rule_allows, unavailable_dates


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "timetable_data.json"
SCHEDULE = ROOT / "output" / "latest" / "schedule_all.json"
HISTORY = ROOT / "data" / "history"
REPORT = ROOT / "build" / "reports" / "same_subject_quota_swap.json"


def read(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    tmp.replace(path)


data = read(DATA)
schedule = read(SCHEDULE)
teachers = {int(x["id"]): x for x in data.get("teachers", [])}
groups = {int(x["id"]): x for x in data.get("groups", [])}
lessons = {int(x["id"]): x for x in data.get("lessons", [])}
unavailable = unavailable_dates(data)

family_daily: Counter[tuple[int, int, bool, str, str]] = Counter()
seen: set[tuple[int, str, int]] = set()
for group_schedule in schedule.get("groups", []):
    group_id = int(group_schedule.get("group_index", -1))
    real_parts = max(1, min(2, int(groups.get(group_id, {}).get("parts", 2))))
    for day in group_schedule.get("days", []):
        date_iso = str(day.get("date_iso", ""))
        for slot in day.get("slots", []):
            pair = int(slot.get("slot", 0))
            for event in slot.get("lessons", []):
                lesson_id = int(event.get("id", -1))
                unique = (lesson_id, date_iso, pair)
                if unique in seen:
                    continue
                seen.add(unique)
                lesson = lessons.get(lesson_id, {})
                subgroup = int(event.get("subgroup", lesson.get("subgroup", -1)))
                name = str(event.get("name", lesson.get("name", "")))
                is_lab = bool(event.get("is_lab", lesson.get("is_lab", False)))
                for part in range(real_parts):
                    if subgroup in (-1, part):
                        family_daily[(group_id, part, is_lab, name, date_iso)] += 1

violating: dict[int, Counter[tuple[bool, str]]] = defaultdict(Counter)
for (group_id, _part, is_lab, name, _date), count in family_daily.items():
    if count > 2:
        violating[group_id][(is_lab, name)] += count - 2

teacher_load: Counter[int] = Counter()
for lesson in lessons.values():
    if lesson.get("plan_active", True) and int(lesson.get("total_slots", 0)) > 0:
        teacher_load[int(lesson.get("teacher", -1))] += int(lesson.get("total_slots", 0))


def teacher_capacity(teacher_id: int) -> int:
    teacher = teachers.get(teacher_id)
    if teacher is None or teacher_id < 0:
        return 0
    daily = []
    for current in dates():
        if current in unavailable[teacher_id]:
            daily.append(0)
            continue
        value = sum(1 for pair in range(1, 8) if rule_allows(teacher, current, pair))
        max_daily = int(teacher.get("max_pairs_per_day", 0))
        daily.append(min(value, max_daily) if max_daily > 0 else value)
    max_days = int(teacher.get("max_work_days_per_week", 0))
    if max_days > 0:
        daily = sorted(daily, reverse=True)[:max_days]
    return sum(daily)


changes = []
skipped = []
for group_id, families in sorted(violating.items()):
    family_order = sorted(families.items(), key=lambda item: item[1], reverse=True)
    swap = None
    for (is_lab, name), excess in family_order:
        outgoing_options = [
            x for x in lessons.values()
            if int(x.get("group", -1)) == group_id
            and x.get("plan_active", True)
            and int(x.get("total_slots", 0)) > 0
            and bool(x.get("is_lab", False)) == is_lab
            and str(x.get("name", "")) == name
        ]
        outgoing_options.sort(key=lambda x: int(x.get("total_slots", 0)), reverse=True)
        for outgoing in outgoing_options:
            subgroup = int(outgoing.get("subgroup", -1))
            incoming_options = []
            for incoming in lessons.values():
                if int(incoming.get("group", -1)) != group_id:
                    continue
                if incoming.get("plan_active", True) or int(incoming.get("total_slots", 0)) <= 0:
                    continue
                if incoming.get("is_block", False) or incoming.get("is_pp", False):
                    continue
                if bool(incoming.get("is_lab", False)):
                    continue
                if int(incoming.get("subgroup", -1)) != subgroup:
                    continue
                if str(incoming.get("name", "")) == name:
                    continue
                teacher_id = int(incoming.get("teacher", -1))
                capacity = teacher_capacity(teacher_id)
                if capacity <= teacher_load[teacher_id]:
                    continue
                teacher = teachers.get(teacher_id, {})
                # Prefer a broadly available, non-school teacher with spare
                # capacity.  All candidates are still validated by CP-SAT.
                score = (
                    0 if teacher.get("school_schedule_2026_2027") else 1,
                    capacity - teacher_load[teacher_id],
                    len(teacher.get("allowed_campuses", [])) != 1,
                    int(incoming.get("total_slots", 0)),
                )
                incoming_options.append((score, incoming, capacity))
            if not incoming_options:
                continue
            _, incoming, capacity = max(incoming_options, key=lambda item: item[0])
            swap = (outgoing, incoming, excess, capacity)
            break
        if swap:
            break
    if not swap:
        skipped.append({"group_id": group_id, "group": groups.get(group_id, {}).get("name", str(group_id))})
        continue
    outgoing, incoming, excess, capacity = swap
    outgoing_before = int(outgoing.get("total_slots", 0))
    incoming_semester = int(incoming.get("total_slots", 0))
    outgoing["total_slots"] = outgoing_before - 1
    if outgoing_before - 1 <= 0:
        outgoing["plan_active"] = False
    incoming["semester_total_slots_before_week_swap"] = incoming_semester
    incoming["total_slots"] = 1
    incoming["plan_active"] = True
    incoming["week_parity"] = "all"
    outgoing_teacher = int(outgoing.get("teacher", -1))
    incoming_teacher = int(incoming.get("teacher", -1))
    teacher_load[outgoing_teacher] -= 1
    teacher_load[incoming_teacher] += 1
    changes.append({
        "group_id": group_id,
        "group": groups.get(group_id, {}).get("name", str(group_id)),
        "removed_lesson": int(outgoing["id"]),
        "removed_subject": outgoing.get("name", ""),
        "removed_before": outgoing_before,
        "removed_after": outgoing_before - 1,
        "added_lesson": int(incoming["id"]),
        "added_subject": incoming.get("name", ""),
        "added_teacher": teachers.get(incoming_teacher, {}).get("name", ""),
        "added_teacher_load": teacher_load[incoming_teacher],
        "added_teacher_capacity": capacity,
        "observed_excess": excess,
    })

HISTORY.mkdir(parents=True, exist_ok=True)
backup = HISTORY / f"before_same_subject_quota_swap_{datetime.now():%Y%m%d-%H%M%S}.json"
shutil.copy2(DATA, backup)
write(DATA, data)
active_starts = sum(
    int(x.get("total_slots", 0)) for x in data.get("lessons", []) if x.get("plan_active", True)
)
report = {
    "backup": str(backup),
    "violating_groups": len(violating),
    "changes": changes,
    "skipped": skipped,
    "active_starts": active_starts,
}
write(REPORT, report)
print(json.dumps(report, ensure_ascii=False, indent=2))
