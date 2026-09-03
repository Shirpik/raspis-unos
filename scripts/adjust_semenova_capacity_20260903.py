#!/usr/bin/env python3
"""Keep 349 occurrences while respecting Semenova's Saturday 1-4 limit."""

from __future__ import annotations

import json
import shutil
from collections import Counter
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "timetable_data.json"


def main() -> None:
    data = json.loads(DATA.read_text(encoding="utf-8-sig"))
    history = ROOT / "data" / "history"
    history.mkdir(parents=True, exist_ok=True)
    backup = history / f"before_semenova_capacity_{datetime.now():%Y%m%d-%H%M%S}.json"
    shutil.copy2(DATA, backup)

    lessons = {int(item["id"]): item for item in data.get("lessons", [])}
    removed = lessons[14]  # ТАКХС-Пф-1203, химия, Семенова
    replacement = lessons[7]  # та же группа, математика, Домарева
    if int(removed.get("total_slots", 0)) != 1:
        raise SystemExit(f"Unexpected lesson 14 quota: {removed.get('total_slots')}")
    removed["total_slots"] = 0
    removed["plan_active"] = False
    replacement["total_slots"] = int(replacement.get("total_slots", 0)) + 1
    replacement["plan_active"] = True

    teachers = {int(item["id"]): item for item in data.get("teachers", [])}
    previous_targets = {
        int(item.get("teacher", -1)): item
        for item in data.get("settings", {}).get("teacher_period_targets", [])
    }
    load: Counter[int] = Counter()
    for lesson in lessons.values():
        quota = int(lesson.get("total_slots", 0))
        if quota > 0:
            load[int(lesson.get("teacher", -1))] += quota
    targets = []
    for teacher_id, selected in sorted(load.items()):
        if teacher_id < 0:
            continue
        previous = previous_targets.get(teacher_id, {})
        targets.append({
            "teacher": teacher_id,
            "name": teachers[teacher_id]["name"],
            "minimum_pairs": selected,
            "desired_pairs": selected,
            "selected_pairs": selected,
            "minimum_safe_for_deadline": min(int(previous.get("minimum_safe_for_deadline", 0)), selected),
            "remaining_semester_pairs": int(previous.get("remaining_semester_pairs", selected)),
        })
    data["settings"]["teacher_period_targets"] = targets

    marker = "Семенова Л.И.: суббота 1-4; одно занятие химии группы ТАКХС-Пф-1203 заменено математикой этой же группы"
    data.setdefault("meta", {}).setdefault("operational_notes", [])
    if marker not in data["meta"]["operational_notes"]:
        data["meta"]["operational_notes"].append(marker)

    temporary = DATA.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temporary.replace(DATA)
    print(json.dumps({
        "backup": str(backup),
        "removed_lesson": 14,
        "replacement_lesson": 7,
        "semenova_pairs": load[10],
        "planned_occurrences": sum(int(item.get("total_slots", 0)) for item in lessons.values()),
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
