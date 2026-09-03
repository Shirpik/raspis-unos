#!/usr/bin/env python3
"""Restrict an already validated short-week candidate to Friday and Saturday.

The source candidate is used only to select the remaining lesson quotas and to
create exact time locks.  Wednesday and Thursday are intentionally outside the
new planning horizon and therefore cannot be overwritten by regeneration.
"""

from __future__ import annotations

import argparse
import copy
import json
import shutil
from collections import Counter
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "timetable_data.json"
DEFAULT_SCHEDULE = ROOT / "output" / "latest" / "schedule_all.json"
START_DATE = "2026-09-04"
END_DATE = "2026-09-05"
TARGET_DATES = {START_DATE, END_DATE}


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def selected_occurrences(schedule: dict) -> tuple[Counter[int], list[dict]]:
    quotas: Counter[int] = Counter()
    locks: list[dict] = []
    seen: set[tuple[int, str, int]] = set()
    present_dates: set[str] = set()

    for group in schedule.get("groups", []):
        for day in group.get("days", []):
            date_iso = str(day.get("date_iso", ""))
            if date_iso not in TARGET_DATES:
                continue
            present_dates.add(date_iso)
            for slot in day.get("slots", []):
                pair = int(slot.get("slot", 0))
                for lesson in slot.get("lessons", []):
                    lesson_id = int(lesson["id"])
                    key = (lesson_id, date_iso, pair)
                    if key in seen:
                        raise SystemExit(f"Duplicate lesson placement in source: {key}")
                    seen.add(key)
                    quotas[lesson_id] += 1
                    locks.append({
                        "lesson_id": lesson_id,
                        "date": date_iso,
                        "slot": pair - 1,
                    })

    if present_dates != TARGET_DATES:
        raise SystemExit(
            f"Source schedule does not contain both target dates: {sorted(present_dates)}"
        )
    if not locks:
        raise SystemExit("Source schedule contains no Friday/Saturday lessons")
    locks.sort(key=lambda item: (item["date"], item["slot"], item["lesson_id"]))
    return quotas, locks


def filtered_schedule(schedule: dict) -> dict:
    result = copy.deepcopy(schedule)
    weekday = {START_DATE: "ПТ", END_DATE: "СБ"}
    display_date = {START_DATE: "04.09.2026", END_DATE: "05.09.2026"}
    slot_times = [
        "1 пара (08:30-09:55)", "2 пара (10:05-11:30)",
        "3 пара (12:25-13:50)", "4 пара (14:00-15:25)",
        "5 пара (15:35-16:55)", "6 пара (17:05-18:25)",
        "7 пара (18:35-19:55)",
    ]
    for group in result.get("groups", []):
        existing = {
            str(day.get("date_iso", "")): day for day in group.get("days", [])
            if str(day.get("date_iso", "")) in TARGET_DATES
        }
        days = []
        for index, date_iso in enumerate((START_DATE, END_DATE)):
            day = existing.get(date_iso)
            if day is None:
                day = {
                    "date": display_date[date_iso],
                    "date_iso": date_iso,
                    "weekday": weekday[date_iso],
                    "slots": [
                        {"slot": slot + 1, "time": time, "text": "-", "lessons": []}
                        for slot, time in enumerate(slot_times)
                    ],
                }
            day["day_index"] = index
            days.append(day)
        group["days"] = days
    return result


def theory_violation_keys(data: dict, report_path: Path | None) -> set[tuple[int, int]]:
    if report_path is None:
        return set()
    report = read_json(report_path)
    lessons = {int(item["id"]): item for item in data.get("lessons", [])}
    result = set()
    for issue in report.get("issues", []):
        if issue.get("code") != "theory_before_lab_violation":
            continue
        lesson = lessons.get(int(issue.get("context", {}).get("lesson", -1)))
        if lesson:
            result.add((int(lesson["group"]), int(lesson["subject_id"])))
    return result


def ensure_initial_theory(
    data: dict,
    quotas: Counter[int],
    required_keys: set[tuple[int, int]],
) -> list[dict]:
    """Add missing introductory theory instead of depending on Thursday."""
    config = data.get("settings", {}).get("solver_config", {})
    required = int(config.get("min_initial_theory_slots_before_labs", 0))
    if required <= 0:
        return []

    lessons = data.get("lessons", [])
    by_key: dict[tuple[int, int], list[dict]] = {}
    for lesson in lessons:
        key = (int(lesson.get("group", -1)), int(lesson.get("subject_id", -1)))
        by_key.setdefault(key, []).append(lesson)

    prior = Counter()
    for item in data.get("settings", {}).get("prior_theory_pairs", []):
        prior[(int(item.get("group", -1)), int(item.get("subject", -1)))] += int(
            item.get("pairs", 0)
        )

    additions: list[dict] = []
    lab_keys = {
        (int(lesson.get("group", -1)), int(lesson.get("subject_id", -1)))
        for lesson in lessons
        if bool(lesson.get("is_lab", False)) and quotas[int(lesson["id"])] > 0
    }
    lab_keys &= required_keys
    for key in sorted(lab_keys):
        siblings = by_key.get(key, [])
        theory = [
            lesson for lesson in siblings
            if not bool(lesson.get("is_lab", False))
            and not bool(lesson.get("is_block", False))
            and bool(lesson.get("generation_active", True))
            and int(lesson.get("teacher", -1)) >= 0
        ]
        planned = sum(quotas[int(lesson["id"])] for lesson in theory)
        missing = max(0, required - prior[key] - planned)
        if missing == 0:
            continue
        # Some imported disciplines consist solely of practical work.  The
        # solver does not create a theory-before-lab relation for those rows.
        if not theory:
            continue
        target = theory[0]
        target_id = int(target["id"])
        semester_limit = (int(target.get("total_hours", 0)) + 1) // 2
        if quotas[target_id] + missing > semester_limit:
            raise SystemExit(f"Not enough theory hours for {key}: need {missing} more")
        quotas[target_id] += missing
        additions.append({
            "group": key[0],
            "subject": key[1],
            "lesson_id": target_id,
            "added_pairs": missing,
        })
    return additions


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--schedule", type=Path, default=DEFAULT_SCHEDULE)
    parser.add_argument("--locks-output", type=Path, required=True)
    parser.add_argument("--filtered-output", type=Path)
    parser.add_argument("--theory-errors", type=Path)
    args = parser.parse_args()

    data = read_json(DATA)
    schedule = read_json(args.schedule)
    quotas, locks = selected_occurrences(schedule)
    violation_keys = theory_violation_keys(data, args.theory_errors)
    theory_additions = ensure_initial_theory(data, quotas, violation_keys)

    known_ids = {int(lesson["id"]) for lesson in data.get("lessons", [])}
    unknown_ids = sorted(set(quotas) - known_ids)
    if unknown_ids:
        raise SystemExit(f"Source schedule refers to unknown lessons: {unknown_ids}")

    history = ROOT / "data" / "history"
    history.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup = history / f"before_friday_saturday_generation_{stamp}.json"
    shutil.copy2(DATA, backup)

    teacher_load: Counter[int] = Counter()
    active_lessons = 0
    for lesson in data.get("lessons", []):
        lesson_id = int(lesson["id"])
        quota = int(quotas.get(lesson_id, 0))
        lesson["total_slots"] = quota
        lesson["plan_active"] = quota > 0
        if quota > 0:
            active_lessons += 1
            teacher_load[int(lesson.get("teacher", -1))] += quota

    settings = data.setdefault("settings", {})
    settings["start_date"] = START_DATE
    settings["end_date"] = END_DATE
    solver_config = settings.setdefault("solver_config", {})
    solver_config["min_student_study_days_per_week"] = 2

    teachers = {int(item["id"]): item for item in data.get("teachers", [])}
    previous_targets = {
        int(item.get("teacher", -1)): item
        for item in settings.get("teacher_period_targets", [])
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
            "remaining_semester_pairs": int(
                previous.get("remaining_semester_pairs", selected)
            ),
        })
    settings["teacher_period_targets"] = targets

    data.setdefault("meta", {})["generation_scenario"] = {
        "label": "friday-saturday-only",
        "date_from": START_DATE,
        "date_to": END_DATE,
        "source_schedule": str(args.schedule.resolve()),
        "selected_occurrences": sum(quotas.values()),
        "active_lessons": active_lessons,
        "initial_theory_additions": theory_additions,
        "excluded_existing_days": ["2026-09-02", "2026-09-03"],
        "backup": str(backup.resolve()),
    }

    write_json(DATA, data)
    write_json(args.locks_output, {
        "source": "validated-friday-saturday-candidate",
        "assignments": locks,
    })
    if args.filtered_output:
        write_json(args.filtered_output, filtered_schedule(schedule))
    print(json.dumps({
        "period": [START_DATE, END_DATE],
        "active_lessons": active_lessons,
        "selected_occurrences": sum(quotas.values()),
        "locks": len(locks),
        "initial_theory_additions": theory_additions,
        "backup": str(backup),
        "locks_output": str(args.locks_output),
        "filtered_output": str(args.filtered_output) if args.filtered_output else None,
    }, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
