"""Reduce only weekly quotas that force a third same-subject pair in a day.

The input schedule must already satisfy the more important shape constraints
(four study days, 2-4 consecutive pairs).  One occurrence per affected group
is deferred to a later semester week, never taking a real subgroup below the
hard eight-pair minimum for this four-day week.
"""

from __future__ import annotations

import json
import shutil
from collections import Counter, defaultdict
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "timetable_data.json"
SCHEDULE = ROOT / "output" / "latest" / "schedule_all.json"
HISTORY = ROOT / "data" / "history"
REPORT = ROOT / "build" / "reports" / "same_subject_quota_repair.json"
MIN_WEEKLY_PAIRS = 8


def read(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temp.replace(path)


data = read(DATA)
schedule = read(SCHEDULE)
lessons = {int(item["id"]): item for item in data.get("lessons", [])}
groups = {int(item["id"]): item for item in data.get("groups", [])}

family_daily: Counter[tuple[int, int, bool, str, str]] = Counter()
seen: set[tuple[int, str, int]] = set()
for group_schedule in schedule.get("groups", []):
    group_id = int(group_schedule.get("group_index", -1))
    real_parts = max(1, min(2, int(groups.get(group_id, {}).get("parts", 2))))
    for day in group_schedule.get("days", []):
        date_iso = day.get("date_iso", "")
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

violations_by_group: dict[int, Counter[tuple[bool, str]]] = defaultdict(Counter)
for (group_id, _part, is_lab, name, _date_iso), count in family_daily.items():
    if count > 2:
        violations_by_group[group_id][(is_lab, name)] += count - 2

part_load: Counter[tuple[int, int]] = Counter()
for lesson in lessons.values():
    if not lesson.get("plan_active", True) or int(lesson.get("total_slots", 0)) <= 0:
        continue
    group_id = int(lesson.get("group", -1))
    subgroup = int(lesson.get("subgroup", -1))
    real_parts = max(1, min(2, int(groups.get(group_id, {}).get("parts", 2))))
    for part in range(real_parts):
        if subgroup in (-1, part):
            part_load[(group_id, part)] += int(lesson.get("total_slots", 0))

changes = []
skipped = []
for group_id, families in sorted(violations_by_group.items()):
    ranked_families = sorted(families.items(), key=lambda item: (item[1], item[0][0]), reverse=True)
    selected = None
    for (is_lab, name), excess in ranked_families:
        candidates = [
            lesson for lesson in lessons.values()
            if int(lesson.get("group", -1)) == group_id
            and bool(lesson.get("is_lab", False)) == is_lab
            and str(lesson.get("name", "")) == name
            and lesson.get("plan_active", True)
            and int(lesson.get("total_slots", 0)) > 0
        ]
        candidates.sort(key=lambda lesson: int(lesson.get("total_slots", 0)), reverse=True)
        for lesson in candidates:
            subgroup = int(lesson.get("subgroup", -1))
            real_parts = max(1, min(2, int(groups.get(group_id, {}).get("parts", 2))))
            affected = [part for part in range(real_parts) if subgroup in (-1, part)]
            if all(part_load[(group_id, part)] - 1 >= MIN_WEEKLY_PAIRS for part in affected):
                selected = (lesson, affected, excess)
                break
        if selected:
            break
    if not selected:
        skipped.append({"group_id": group_id, "group": groups.get(group_id, {}).get("name", str(group_id))})
        continue
    lesson, affected, excess = selected
    before = int(lesson.get("total_slots", 0))
    lesson["total_slots"] = before - 1
    if before - 1 <= 0:
        lesson["plan_active"] = False
    for part in affected:
        part_load[(group_id, part)] -= 1
    changes.append({
        "group_id": group_id,
        "group": groups.get(group_id, {}).get("name", str(group_id)),
        "lesson_id": int(lesson["id"]),
        "subject": lesson.get("name", ""),
        "is_lab": bool(lesson.get("is_lab", False)),
        "before": before,
        "after": before - 1,
        "observed_excess": excess,
        "part_load_after": {str(part + 1): part_load[(group_id, part)] for part in affected},
    })

HISTORY.mkdir(parents=True, exist_ok=True)
backup = HISTORY / f"before_same_subject_quota_repair_{datetime.now():%Y%m%d-%H%M%S}.json"
shutil.copy2(DATA, backup)
write(DATA, data)
report = {
    "backup": str(backup),
    "violating_groups": len(violations_by_group),
    "changes": changes,
    "skipped": skipped,
    "remaining_starts": sum(
        int(item.get("total_slots", 0)) for item in data.get("lessons", []) if item.get("plan_active", True)
    ),
}
write(REPORT, report)
print(json.dumps(report, ensure_ascii=False, indent=2))
