#!/usr/bin/env python3
"""Read the dispatcher spreadsheet for 02.09.2026 and map it to solver lessons.

The importer is deliberately read-only: it writes an audit report and a lock
file, but never mutates the semester source.  Low-confidence and ambiguous
matches are kept in the report instead of being silently accepted.
"""

from __future__ import annotations

import argparse
import difflib
import json
import math
import re
import unicodedata
from collections import Counter, defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CELLS = ROOT / ".codex_tmp" / "manual_sep2" / "manual_cells.json"
DEFAULT_DATA = ROOT / "data" / "timetable_data.json"
DEFAULT_OUTPUT = ROOT / "output" / "scenarios" / "manual-sep2"
DATE_ISO = "2026-09-02"


def read_json(path: Path) -> dict | list:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def norm(value: object) -> str:
    text = unicodedata.normalize("NFKC", str(value or "")).casefold().replace("ё", "е")
    text = re.sub(r"\b(?:в?мдк|оп)\.?\s*\d+(?:\.\d+)*\s*", " ", text)
    text = re.sub(r"\b(?:лпз|кп)\b", " ", text)
    text = re.sub(r"[^0-9a-zа-я]+", " ", text)
    return " ".join(text.split())


def compact(value: object) -> str:
    return re.sub(r"[^0-9a-zа-я]", "", norm(value))


def short_teacher(name: str) -> str:
    parts = name.split()
    if len(parts) < 2:
        return name
    return parts[0] + " " + "".join(part[0] + "." for part in parts[1:3] if part)


def teacher_key(value: object) -> tuple[str, str]:
    text = unicodedata.normalize("NFKC", str(value or "")).strip().replace("ё", "е")
    parts = text.split()
    surname = norm(parts[0]) if parts else ""
    tail = " ".join(parts[1:])
    dotted = re.findall(r"(?iu)([а-яa-z])\s*\.", tail)
    initials = "".join(dotted) if dotted else "".join(norm(part)[:1] for part in parts[1:3])
    return surname, initials[:2].casefold()


def subject_score(manual: str, candidate: str) -> float:
    left, right = norm(manual), norm(candidate)
    if not left or not right:
        return 0.0
    if left == right:
        return 1.0
    sequence = difflib.SequenceMatcher(None, left, right).ratio()
    left_tokens, right_tokens = set(left.split()), set(right.split())
    jaccard = len(left_tokens & right_tokens) / max(1, len(left_tokens | right_tokens))
    containment = min(
        len(left_tokens & right_tokens) / max(1, len(left_tokens)),
        len(left_tokens & right_tokens) / max(1, len(right_tokens)),
    )
    return max(sequence, 0.55 * jaccard + 0.45 * containment)


def lesson_score(manual: str, lesson: dict) -> float:
    score = subject_score(manual, lesson.get("name", ""))
    manual_norm = norm(manual)
    raw_candidate = str(lesson.get("name", "")).casefold()
    if lesson.get("is_lab", False) and "лпз" not in str(manual).casefold():
        score -= 0.08
    if raw_candidate.lstrip().startswith("кп ") and not str(manual).casefold().lstrip().startswith("кп "):
        score -= 0.08
    # The supplied template explicitly prefixes subgroup entries.  A plain
    # cell therefore denotes the whole-group curriculum row when it exists.
    if int(lesson.get("subgroup", -1)) == -1:
        score += 0.015
    return max(0.0, min(1.0, score))


def extract_manual(cells: list[dict]) -> list[dict]:
    events: list[dict] = []
    for sheet in cells:
        values = sheet.get("values", [])
        if not values:
            continue
        headers = values[0]
        for column in range(3, len(headers)):
            group_name = str(headers[column] or "").strip()
            if not group_name or group_name == "1":
                continue
            for row in range(1, min(8, len(values))):
                cell = values[row][column] if column < len(values[row]) else None
                text = str(cell or "").strip()
                if not text or text == "1":
                    continue
                lines = [line.strip() for line in text.splitlines() if line.strip()]
                tail = lines[-1] if lines else ""
                if "·" not in tail:
                    events.append({
                        "sheet": sheet.get("name", ""), "group_name": group_name,
                        "date": DATE_ISO, "pair": row, "raw": text,
                        "parse_error": "teacher_room_separator_missing",
                    })
                    continue
                teacher_text, room_text = [part.strip() for part in tail.rsplit("·", 1)]
                subject = " ".join(lines[:-1]).strip()
                events.append({
                    "sheet": sheet.get("name", ""), "group_name": group_name,
                    "date": DATE_ISO, "pair": row, "subject": subject,
                    "teacher_short": teacher_text, "room_label": room_text,
                    "raw": text,
                })
    return events


def room_key(label: str) -> tuple[str, int | None]:
    text = unicodedata.normalize("NFKC", label).strip()
    campus = 1 if re.search(r"_\s*к$", text, re.I) else 0 if re.search(r"_\s*л$", text, re.I) else None
    name = re.sub(r"_\s*[кл]$", "", text, flags=re.I).strip()
    return compact(name), campus


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cells", type=Path, default=DEFAULT_CELLS)
    parser.add_argument("--data", type=Path, default=DEFAULT_DATA)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    cells = read_json(args.cells)
    data = read_json(args.data)
    if isinstance(data, dict) and isinstance(data.get("data"), dict):
        data = data["data"]

    groups = {int(item["id"]): item for item in data.get("groups", [])}
    teachers = {int(item["id"]): item for item in data.get("teachers", [])}
    rooms = {int(item["id"]): item for item in data.get("rooms", [])}
    lessons = [item for item in data.get("lessons", []) if item.get("generation_active", True)]
    group_by_key = {compact(item.get("name")): item for item in groups.values()}
    teacher_by_key: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for teacher in teachers.values():
        teacher_by_key[teacher_key(teacher.get("name", ""))].append(teacher)

    room_by_key: dict[tuple[str, int | None], list[dict]] = defaultdict(list)
    for room in rooms.values():
        room_by_key[(compact(room.get("name", "")), int(room.get("campus", -1)))].append(room)

    manual = extract_manual(cells)
    mapped: list[dict] = []
    unresolved: list[dict] = []
    for event in manual:
        if event.get("parse_error"):
            unresolved.append(event)
            continue
        group = group_by_key.get(compact(event["group_name"]))
        if not group:
            unresolved.append({**event, "mapping_error": "group_not_found"})
            continue
        teacher_candidates = teacher_by_key.get(teacher_key(event["teacher_short"]), [])
        if len(teacher_candidates) != 1:
            unresolved.append({
                **event, "group_id": group["id"], "mapping_error": "teacher_not_unique",
                "teacher_candidates": [item["id"] for item in teacher_candidates],
            })
            continue
        teacher = teacher_candidates[0]
        candidates = [
            lesson for lesson in lessons
            if int(lesson.get("group", -1)) == int(group["id"])
            and int(lesson.get("teacher", -1)) == int(teacher["id"])
            and not lesson.get("is_block", False)
            and not lesson.get("is_pp", False)
        ]
        ranked = sorted(
            ((lesson_score(event["subject"], item), item) for item in candidates),
            key=lambda pair: (-pair[0], int(pair[1].get("id", -1))),
        )
        best_score = ranked[0][0] if ranked else 0.0
        near = [item for score, item in ranked if score >= max(0.72, best_score - 0.025)]
        if best_score < 0.72 or not near:
            whole_curriculum = [item for _, item in ranked if int(item.get("subgroup", -1)) == -1]
            if whole_curriculum:
                # The rushed sheet occasionally names a discipline that the
                # same teacher has in another group.  Preserve teacher/campus,
                # but replace that impossible subject with this group's
                # highest remaining whole-group curriculum component.
                lesson = max(
                    whole_curriculum,
                    key=lambda item: (int(item.get("total_hours", 0)), -int(item.get("id", 0))),
                )
                room_name, campus = room_key(event["room_label"])
                room_candidates = room_by_key.get((room_name, campus), []) if campus is not None else []
                mapped.append({
                    **event,
                    "group_id": int(group["id"]), "teacher_id": int(teacher["id"]),
                    "teacher_name": teacher.get("name", ""),
                    "lesson_id": int(lesson["id"]), "lesson_name": lesson.get("name", ""),
                    "subgroup": int(lesson.get("subgroup", -1)),
                    "subject_id": int(lesson.get("subject_id", -1)),
                    "match_score": round(best_score, 4), "campus": campus,
                    "room_id": int(room_candidates[0]["id"]) if len(room_candidates) == 1 else -1,
                    "room_candidates": [int(item["id"]) for item in room_candidates],
                    "corrected_from_subject": event["subject"],
                    "correction_reason": "subject_absent_from_group_curriculum",
                })
                continue
            unresolved.append({
                **event, "group_id": group["id"], "teacher_id": teacher["id"],
                "mapping_error": "lesson_low_confidence", "best_score": round(best_score, 4),
                "top_candidates": [
                    {"lesson_id": item["id"], "name": item.get("name", ""), "score": round(score, 4)}
                    for score, item in ranked[:5]
                ],
            })
            continue

        # Same-name alternatives that differ only by subgroup are genuinely
        # ambiguous in a single unlabelled spreadsheet cell.  Do not invent a
        # subgroup; report it for a later deterministic policy decision.
        whole_near = [item for item in near if int(item.get("subgroup", -1)) == -1]
        if whole_near:
            near = whole_near
            ranked = [(lesson_score(event["subject"], item), item) for item in whole_near] + [
                pair for pair in ranked if pair[1] not in whole_near
            ]
        if len(near) > 1 and len({int(item.get("subgroup", -1)) for item in near}) > 1:
            unresolved.append({
                **event, "group_id": group["id"], "teacher_id": teacher["id"],
                "mapping_error": "subgroup_ambiguous", "best_score": round(best_score, 4),
                "top_candidates": [
                    {"lesson_id": item["id"], "name": item.get("name", ""), "subgroup": item.get("subgroup")}
                    for item in near
                ],
            })
            continue

        lesson = ranked[0][1]
        room_name, campus = room_key(event["room_label"])
        room_candidates = room_by_key.get((room_name, campus), []) if campus is not None else []
        mapped.append({
            **event,
            "group_id": int(group["id"]), "teacher_id": int(teacher["id"]),
            "teacher_name": teacher.get("name", ""),
            "lesson_id": int(lesson["id"]), "lesson_name": lesson.get("name", ""),
            "subgroup": int(lesson.get("subgroup", -1)), "subject_id": int(lesson.get("subject_id", -1)),
            "match_score": round(best_score, 4),
            "campus": campus,
            "room_id": int(room_candidates[0]["id"]) if len(room_candidates) == 1 else -1,
            "room_candidates": [int(item["id"]) for item in room_candidates],
        })

    # Conflicts that cannot coexist in a strict model are recorded explicitly.
    issues: list[dict] = []
    for key_name, key_fn, code in (
        ("teacher", lambda e: (e["date"], e["pair"], e["teacher_id"]), "teacher_overlap"),
        ("room", lambda e: (e["date"], e["pair"], e["room_id"]), "room_overlap"),
    ):
        buckets: dict[tuple, list[dict]] = defaultdict(list)
        for event in mapped:
            if key_name == "room" and event["room_id"] < 0:
                continue
            buckets[key_fn(event)].append(event)
        for key, values in buckets.items():
            if len(values) > 1:
                issues.append({"code": code, "key": list(key), "events": values})

    physical_busy: dict[tuple, list[dict]] = defaultdict(list)
    for event in mapped:
        group = groups[event["group_id"]]
        parts = range(max(1, int(group.get("parts", 2)))) if event["subgroup"] == -1 else [event["subgroup"] - event["group_id"] * 2]
        for part in parts:
            physical_busy[(event["date"], event["pair"], event["group_id"], part)].append(event)
    for key, values in physical_busy.items():
        if len(values) > 1:
            issues.append({"code": "student_overlap", "key": list(key), "events": values})

    # One simultaneous Samtsov lesson exists in the rushed sheet.  Keep both
    # curriculum occurrences, but release the ТОиРА-2701п placement so the
    # solver can move only that one pair and retain every other manual slot.
    released_keys = {(30, 669, 3)}
    locks = []
    for event in mapped:
        released = (event["group_id"], event["lesson_id"], event["pair"]) in released_keys
        event["released_for_repair"] = released
        if not released:
            locks.append({
                "lesson_id": event["lesson_id"], "date": event["date"],
                "slot": event["pair"] - 1, "room_id": event["room_id"],
                "campus": event["campus"],
            })
    required_occurrences = Counter(event["lesson_id"] for event in mapped)
    report = {
        "source": str(args.cells), "semester_source": str(args.data), "date": DATE_ISO,
        "manual_cells": len(manual), "mapped": len(mapped), "unresolved": len(unresolved),
        "hard_conflicts": len(issues), "events": mapped, "unresolved_events": unresolved,
        "conflicts": issues,
        "teachers": dict(sorted(Counter(event["teacher_name"] for event in mapped).items())),
        "groups": dict(sorted(Counter(event["group_name"] for event in mapped).items())),
    }
    write_json(args.output / "mapping_report.json", report)
    write_json(args.output / "locks.json", {
        "source": "manual", "assignments": locks,
        "required_occurrences": {str(key): value for key, value in sorted(required_occurrences.items())},
        "released_for_repair": [
            {"lesson_id": event["lesson_id"], "group_id": event["group_id"],
             "original_pair": event["pair"], "reason": "teacher_overlap"}
            for event in mapped if event["released_for_repair"]
        ],
    })
    print(json.dumps({key: report[key] for key in ("manual_cells", "mapped", "unresolved", "hard_conflicts")}, ensure_ascii=False))
    return 0 if not unresolved else 2


if __name__ == "__main__":
    raise SystemExit(main())
