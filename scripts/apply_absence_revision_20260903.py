#!/usr/bin/env python3
"""Apply the final absence constraints for the 2026-09-04/05 regeneration."""

from __future__ import annotations

import json
import shutil
from collections import Counter
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "timetable_data.json"
HISTORY = ROOT / "data" / "history"
WEEK_FROM = "2026-08-31"
WEEK_TO = "2026-09-05"
TARGET_DATES = ("2026-09-04", "2026-09-05")

# Six removed occurrences are replaced in the same groups, so the two-day
# workload remains maximal at 349 rendered lesson occurrences.
REPLACEMENTS = {
    282: 1,  # МЦМ-Пф-202, иностранный язык, 1 п/г
    283: 1,  # МЦМ-Пф-202, иностранный язык, 2 п/г
    337: 1,  # МЦМ-408, МДК.01.01
    855: 1,  # ИСП-2308, география
    857: 1,  # ИСП-2308, история
    875: 1,  # ИСП-2309п, география
}


def write_json(path: Path, value: object) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def set_override(teacher: dict, date_iso: str, slots: list[int]) -> None:
    overrides = [
        item for item in teacher.get("date_slot_overrides", [])
        if str(item.get("date", "")) != date_iso
    ]
    overrides.append({"date": date_iso, "slots": slots})
    overrides.sort(key=lambda item: str(item.get("date", "")))
    teacher["date_slot_overrides"] = overrides


def main() -> None:
    data = json.loads(DATA.read_text(encoding="utf-8-sig"))
    HISTORY.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup = HISTORY / f"before_absence_revision_{stamp}.json"
    shutil.copy2(DATA, backup)

    teachers = {int(item["id"]): item for item in data.get("teachers", [])}
    lessons = {int(item["id"]): item for item in data.get("lessons", [])}
    for teacher_id in (44, 66):
        for date_iso in TARGET_DATES:
            set_override(teachers[teacher_id], date_iso, [])
    set_override(teachers[10], "2026-09-05", [1, 2, 3, 4])

    absences = [
        item for item in data.get("teacher_unavailable", [])
        if int(item.get("teacher", -1)) not in (44, 66)
    ]
    next_id = max((int(item.get("id", -1)) for item in absences), default=-1) + 1
    for teacher_id, slug in ((44, "pismak"), (66, "koltyshev")):
        absences.append({
            "from": WEEK_FROM,
            "id": next_id,
            "teacher": teacher_id,
            "text": "Отсутствует всю текущую учебную неделю",
            "to": WEEK_TO,
            "uid": f"teacher_unavailable-{slug}-2026w36",
        })
        next_id += 1
    data["teacher_unavailable"] = absences

    removed = Counter()
    for lesson in lessons.values():
        if int(lesson.get("teacher", -1)) in (44, 66):
            quota = int(lesson.get("total_slots", 0))
            if quota:
                removed[int(lesson["teacher"])] += quota
            lesson["total_slots"] = 0
            lesson["plan_active"] = False

    for lesson_id, increment in REPLACEMENTS.items():
        lesson = lessons[lesson_id]
        lesson["total_slots"] = int(lesson.get("total_slots", 0)) + increment
        lesson["plan_active"] = True

    if sum(removed.values()) != sum(REPLACEMENTS.values()) or removed != Counter({44: 3, 66: 3}):
        raise SystemExit(f"Unexpected removed quotas: {dict(removed)}")

    teacher_load: Counter[int] = Counter()
    for lesson in lessons.values():
        quota = int(lesson.get("total_slots", 0))
        if quota > 0:
            teacher_load[int(lesson.get("teacher", -1))] += quota
    previous_targets = {
        int(item.get("teacher", -1)): item
        for item in data.get("settings", {}).get("teacher_period_targets", [])
    }
    targets = []
    for teacher_id, selected in sorted(teacher_load.items()):
        if teacher_id < 0 or selected <= 0:
            continue
        previous = previous_targets.get(teacher_id, {})
        targets.append({
            "teacher": teacher_id,
            "name": teachers.get(teacher_id, {}).get("name", f"#{teacher_id}"),
            "minimum_pairs": selected,
            "desired_pairs": selected,
            "selected_pairs": selected,
            "minimum_safe_for_deadline": min(
                int(previous.get("minimum_safe_for_deadline", 0)), selected
            ),
            "remaining_semester_pairs": int(previous.get("remaining_semester_pairs", selected)),
        })
    data.setdefault("settings", {})["teacher_period_targets"] = targets

    notes = data.setdefault("meta", {}).setdefault("operational_constraints", [])
    marker = "absence-revision-2026-09-03-v1"
    notes = [item for item in notes if not isinstance(item, dict) or item.get("id") != marker]
    notes.append({
        "id": marker,
        "scope": [WEEK_FROM, WEEK_TO],
        "rules": [
            "Колтышев: занятий на текущей неделе нет",
            "Письмак: занятий на текущей неделе нет",
            "Семенова Лилиана Ивановна: в субботу только пары 1-4",
            "Удалённые шесть занятий заменены занятиями тех же групп; общая нагрузка сохранена",
        ],
    })
    data["meta"]["operational_constraints"] = notes

    write_json(DATA, data)
    print(json.dumps({
        "backup": str(backup),
        "removed": dict(removed),
        "replacements": REPLACEMENTS,
        "planned_occurrences": sum(int(item.get("total_slots", 0)) for item in lessons.values()),
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
