#!/usr/bin/env python3
"""Compare the rushed 02 Sep sheet with scenario A and assess 07:30 publication risk."""

from __future__ import annotations

import argparse
import json
from collections import defaultdict
from functools import lru_cache
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MAPPING = ROOT / "output" / "scenarios" / "manual-sep2" / "mapping_report.json"
DEFAULT_DATA = ROOT / "output" / "scenarios" / "manual-sep2" / "input_sep2.json"
DEFAULT_SCHEDULE = ROOT / "output" / "scenarios" / "manual-sep2" / "schedule_sep2" / "schedule_all.json"
DEFAULT_REPORT = ROOT / "output" / "scenarios" / "manual-sep2" / "change_impact_sep2.json"
DEFAULT_SUMMARY = ROOT / "output" / "scenarios" / "manual-sep2" / "change_impact_sep2.md"


def read(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def parts_for(lesson: dict, groups: dict[int, dict]) -> list[int]:
    subgroup = int(lesson.get("subgroup", -1))
    count = max(1, int(groups[int(lesson["group"])].get("parts", 2)))
    return list(range(count)) if subgroup < 0 else [min(subgroup, count - 1)]


def windows(slots: set[int]) -> int:
    if not slots:
        return 0
    return sum(slot not in slots for slot in range(min(slots), max(slots) + 1))


def match_occurrences(old: list[dict], new: list[dict]) -> tuple[list[tuple[dict, dict]], list[dict]]:
    """Monotone minimum-distance matching; extra new occurrences remain additions."""
    old = sorted(old, key=lambda item: int(item["slot"]))
    new = sorted(new, key=lambda item: int(item["slot"]))

    # Occurrences of the same lesson are interchangeable.  Preserve every
    # exact manual cell first, even when the remaining minimum-distance pairs
    # would otherwise cross in time.
    exact_pairs: list[tuple[dict, dict]] = []
    used_old: set[int] = set()
    used_new: set[int] = set()
    for old_index, old_item in enumerate(old):
        for new_index, new_item in enumerate(new):
            if new_index not in used_new and int(old_item["slot"]) == int(new_item["slot"]):
                exact_pairs.append((old_item, new_item))
                used_old.add(old_index)
                used_new.add(new_index)
                break
    old = [item for index, item in enumerate(old) if index not in used_old]
    new = [item for index, item in enumerate(new) if index not in used_new]

    @lru_cache(None)
    def solve(i: int, j: int):
        if i == len(old):
            return (0, 0, ())
        if len(new) - j < len(old) - i:
            return None
        best = None
        if j < len(new):
            skipped = solve(i, j + 1)
            if skipped is not None:
                best = (skipped[0], skipped[1], skipped[2])
            used = solve(i + 1, j + 1)
            if used is not None:
                distance = abs(int(old[i]["slot"]) - int(new[j]["slot"]))
                candidate = (used[0] + distance, 0, ((i, j),) + used[2])
                if best is None or candidate[:2] < best[:2]:
                    best = candidate
        return best

    result = solve(0, 0)
    if result is None:
        return [], list(new)
    selected = {j for _, j in result[2]}
    return exact_pairs + [(old[i], new[j]) for i, j in result[2]], [item for j, item in enumerate(new) if j not in selected]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mapping", type=Path, default=DEFAULT_MAPPING)
    parser.add_argument("--data", type=Path, default=DEFAULT_DATA)
    parser.add_argument("--schedule", type=Path, default=DEFAULT_SCHEDULE)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--summary", type=Path, default=DEFAULT_SUMMARY)
    args = parser.parse_args()

    mapping, data, schedule = read(args.mapping), read(args.data), read(args.schedule)
    groups = {int(item["id"]): item for item in data["groups"]}
    teachers = {int(item["id"]): item for item in data["teachers"]}
    lessons = {int(item["id"]): item for item in data["lessons"]}

    old_by_lesson: dict[int, list[dict]] = defaultdict(list)
    old_part_slots: dict[tuple[int, int], set[int]] = defaultdict(set)
    old_part_at: dict[tuple[int, int, int], set[tuple[int, int]]] = defaultdict(set)
    old_teacher_slots: dict[int, set[int]] = defaultdict(set)
    old_teacher_at: dict[tuple[int, int], set[int]] = defaultdict(set)
    old_part_campuses: dict[tuple[int, int], set[int]] = defaultdict(set)
    for event in mapping["events"]:
        lesson_id = int(event["lesson_id"])
        lesson = lessons[lesson_id]
        slot = int(event["pair"])
        normalized = {
            "lesson_id": lesson_id, "slot": slot, "room_id": event.get("room_id"),
            "group": int(event["group_id"]), "teacher": int(event["teacher_id"]),
        }
        old_by_lesson[lesson_id].append(normalized)
        old_teacher_slots[int(event["teacher_id"])].add(slot)
        old_teacher_at[(int(event["teacher_id"]), slot)].add(lesson_id)
        for part in parts_for(lesson, groups):
            key = (int(event["group_id"]), part)
            old_part_slots[key].add(slot)
            old_part_at[(key[0], key[1], slot)].add((lesson_id, int(event["teacher_id"])))
            if event.get("campus") in (0, 1):
                old_part_campuses[key].add(int(event["campus"]))

    new_by_lesson: dict[int, list[dict]] = defaultdict(list)
    new_part_slots: dict[tuple[int, int], set[int]] = defaultdict(set)
    new_part_at: dict[tuple[int, int, int], set[tuple[int, int]]] = defaultdict(set)
    new_teacher_slots: dict[int, set[int]] = defaultdict(set)
    new_teacher_at: dict[tuple[int, int], set[int]] = defaultdict(set)
    new_part_campuses: dict[tuple[int, int], set[int]] = defaultdict(set)
    seen: set[tuple[int, str, int]] = set()
    for raw_group in schedule["groups"]:
        group_id = int(raw_group["group_index"])
        for day in raw_group.get("days", []):
            for raw_slot in day.get("slots", []):
                slot = int(raw_slot["slot"])
                for event in raw_slot.get("lessons", []):
                    lesson_id = int(event["id"])
                    occurrence = (lesson_id, day["date_iso"], slot)
                    if occurrence in seen:
                        continue
                    seen.add(occurrence)
                    lesson = lessons[lesson_id]
                    teacher_id = int(event["teacher_id"])
                    normalized = {
                        "lesson_id": lesson_id, "slot": slot, "room_id": event.get("room_id"),
                        "group": group_id, "teacher": teacher_id,
                    }
                    new_by_lesson[lesson_id].append(normalized)
                    new_teacher_slots[teacher_id].add(slot)
                    new_teacher_at[(teacher_id, slot)].add(lesson_id)
                    room = next((item for item in data["rooms"] if int(item["id"]) == int(event.get("room_id", -1))), {})
                    for part in parts_for(lesson, groups):
                        key = (group_id, part)
                        new_part_slots[key].add(slot)
                        new_part_at[(group_id, part, slot)].add((lesson_id, teacher_id))
                        if room.get("campus") in (0, 1):
                            new_part_campuses[key].add(int(room["campus"]))

    matched: list[tuple[dict, dict]] = []
    additions: list[dict] = []
    for lesson_id in sorted(set(old_by_lesson) | set(new_by_lesson)):
        pairs, extra = match_occurrences(old_by_lesson[lesson_id], new_by_lesson[lesson_id])
        matched.extend(pairs)
        additions.extend(extra)

    shifted = [(old, new) for old, new in matched if old["slot"] != new["slot"]]
    room_changed = [(old, new) for old, new in matched if old.get("room_id") != new.get("room_id")]
    unchanged = len(matched) - len(shifted)

    student_rows = []
    for group_id, group in sorted(groups.items()):
        for part in range(max(1, int(group.get("parts", 2)))):
            key = (group_id, part)
            old_slots, new_slots = old_part_slots[key], new_part_slots[key]
            old_first = min(old_slots) if old_slots else None
            new_first = min(new_slots) if new_slots else None
            pair1_changed = old_part_at[(group_id, part, 1)] != new_part_at[(group_id, part, 1)]
            campus_changed = bool(old_part_campuses[key] and new_part_campuses[key] and old_part_campuses[key] != new_part_campuses[key])
            student_rows.append({
                "group_id": group_id, "group": group["name"], "part": part + 1,
                "old_slots": sorted(old_slots), "new_slots": sorted(new_slots),
                "old_first": old_first, "new_first": new_first,
                "starts_earlier": new_first is not None and (old_first is None or new_first < old_first),
                "new_pair1": 1 in new_slots and 1 not in old_slots,
                "lost_pair1": 1 in old_slots and 1 not in new_slots,
                "pair1_content_changed": pair1_changed,
                "old_windows": windows(old_slots), "new_windows": windows(new_slots),
                "campus_changed": campus_changed,
            })

    teacher_rows = []
    for teacher_id in sorted(set(old_teacher_slots) | set(new_teacher_slots)):
        old_slots, new_slots = old_teacher_slots[teacher_id], new_teacher_slots[teacher_id]
        old_first = min(old_slots) if old_slots else None
        new_first = min(new_slots) if new_slots else None
        teacher_rows.append({
            "teacher_id": teacher_id, "teacher": teachers[teacher_id]["name"],
            "old_slots": sorted(old_slots), "new_slots": sorted(new_slots),
            "old_first": old_first, "new_first": new_first,
            "starts_earlier": new_first is not None and (old_first is None or new_first < old_first),
            "new_pair1": 1 in new_slots and 1 not in old_slots,
            "lost_pair1": 1 in old_slots and 1 not in new_slots,
            "pair1_content_changed": old_teacher_at[(teacher_id, 1)] != new_teacher_at[(teacher_id, 1)],
            "old_windows": windows(old_slots), "new_windows": windows(new_slots),
            "added_load": len(new_slots) - len(old_slots),
        })

    critical_students = [row for row in student_rows if row["new_pair1"] or row["pair1_content_changed"] or row["campus_changed"]]
    critical_teachers = [row for row in teacher_rows if row["new_pair1"] or row["pair1_content_changed"]]
    earlier_students = [row for row in student_rows if row["starts_earlier"]]
    earlier_teachers = [row for row in teacher_rows if row["starts_earlier"]]
    new_student_windows = [row for row in student_rows if row["new_windows"] > row["old_windows"]]
    new_teacher_windows = [row for row in teacher_rows if row["new_windows"] > row["old_windows"]]
    report = {
        "publication": {"publish_time": "07:30", "first_pair": "08:30", "notice_minutes": 60},
        "summary": {
            "risk": "critical" if critical_students or critical_teachers else "moderate",
            "manual_occurrences": sum(len(value) for value in old_by_lesson.values()),
            "new_occurrences": len(seen),
            "manual_positions_unchanged": unchanged,
            "manual_positions_shifted": len(shifted),
            "added_occurrences": len(additions),
            "room_changes_among_manual_occurrences": len(room_changed),
            "student_parts": len(student_rows),
            "student_parts_starting_earlier": len(earlier_students),
            "student_parts_with_new_or_changed_pair1": len(critical_students),
            "student_parts_with_new_pair1": sum(row["new_pair1"] for row in student_rows),
            "student_parts_losing_pair1": sum(row["lost_pair1"] for row in student_rows),
            "student_parts_with_new_windows": len(new_student_windows),
            "teachers_starting_earlier": len(earlier_teachers),
            "teachers_with_new_or_changed_pair1": len(critical_teachers),
            "teachers_with_new_pair1": sum(row["new_pair1"] for row in teacher_rows),
            "teachers_losing_pair1": sum(row["lost_pair1"] for row in teacher_rows),
            "student_windows_after": sum(row["new_windows"] for row in student_rows),
            "student_windows_before": sum(row["old_windows"] for row in student_rows),
            "teachers_with_more_windows": len(new_teacher_windows),
            "teacher_windows_before": sum(row["old_windows"] for row in teacher_rows),
            "teacher_windows_after": sum(row["new_windows"] for row in teacher_rows),
        },
        "critical_student_parts": critical_students,
        "critical_teachers": critical_teachers,
        "students_starting_earlier": earlier_students,
        "teachers_starting_earlier": earlier_teachers,
        "new_student_windows": new_student_windows,
        "teachers_with_more_windows": new_teacher_windows,
        "shift_distribution": dict(sorted(__import__("collections").Counter(new["slot"] - old["slot"] for old, new in shifted).items())),
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    summary = report["summary"]
    lines = [
        "# Оценка срочной публикации расписания A на 2 сентября",
        "",
        f"Публикация: **07:30**, первая пара: **08:30**, запас: **60 минут**.",
        "",
        f"Итоговый риск: **{summary['risk'].upper()}**.",
        "",
        f"- Сохранено на прежнем месте: {summary['manual_positions_unchanged']} из {summary['manual_occurrences']} исходных занятий.",
        f"- Перенесено по времени: {summary['manual_positions_shifted']}.",
        f"- Добавлено для вычитки: {summary['added_occurrences']}.",
        f"- Физических подгрупп с новой/изменённой первой парой: {summary['student_parts_with_new_or_changed_pair1']}.",
        f"- Из них впервые должны прийти к 08:30: {summary['student_parts_with_new_pair1']}; первая пара отменяется: {summary['student_parts_losing_pair1']}.",
        f"- Преподавателей с новой/изменённой первой парой: {summary['teachers_with_new_or_changed_pair1']}.",
        f"- Из них впервые должны прийти к 08:30: {summary['teachers_with_new_pair1']}; первая пара отменяется: {summary['teachers_losing_pair1']}.",
        f"- Новых окон у студентов: {summary['student_parts_with_new_windows']}; окон после оптимизации: {summary['student_windows_after']}.",
        f"- Окна преподавателей: было {summary['teacher_windows_before']}, стало {summary['teacher_windows_after']}; ухудшение у {summary['teachers_with_more_windows']} преподавателей.",
        "",
        "## Кого предупредить лично до публикации",
        "",
        "### Студенты",
    ]
    lines.extend(
        f"- {row['group']}, {row['part']} подгруппа: было {row['old_slots'] or 'нет'}, стало {row['new_slots']}."
        for row in critical_students
    )
    lines.extend(["", "### Преподаватели"])
    lines.extend(
        f"- {row['teacher']}: было {row['old_slots'] or 'нет'}, стало {row['new_slots']}."
        for row in critical_teachers
    )
    args.summary.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    print(args.summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
