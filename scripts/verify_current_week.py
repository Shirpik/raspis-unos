#!/usr/bin/env python3
"""Verify the generated presentation week against operational constraints."""

import json
from collections import Counter, defaultdict
from datetime import date
from pathlib import Path

from prepare_one_week_generation import rule_allows, unavailable_dates


root = Path(__file__).resolve().parents[1]
data = json.loads((root / "data" / "timetable_data.json").read_text(encoding="utf-8"))
schedule = json.loads(
    (root / "output" / "latest" / "schedule_all.json").read_text(encoding="utf-8")
)

teachers = {int(x["id"]): x for x in data["teachers"]}
rooms = {int(x["id"]): x for x in data["rooms"]}
lessons = {int(x["id"]): x for x in data["lessons"]}
unavailable = unavailable_dates(data)

daily_group_counts = Counter()
daily_part_counts = Counter()
availability_violations = []
wrong_strict_campus = []
wrong_default_campus = []
room_access_violations = []
blocked_room_events = []
room_52_events = []
rabenok = []
nurov = []
silenok = []
tsimfer = []
akhmetov = []
up_pp = []
occurrences = defaultdict(list)
same_subject_violations = []
popova_actual = []
student_window_violations = []

seen = set()
for group in schedule.get("groups", []):
    real_parts = max(1, min(2, int(next(
        item.get("parts", 2) for item in data["groups"]
        if int(item["id"]) == int(group["group_index"])
    ))))
    for day in group.get("days", []):
        current = date.fromisoformat(day["date_iso"])
        part_slots = [set() for _ in range(real_parts)]
        part_subjects = [Counter() for _ in range(real_parts)]
        group_slots = set()
        for slot in day.get("slots", []):
            pair = int(slot["slot"])
            if slot.get("lessons"):
                group_slots.add(pair)
            for event in slot.get("lessons", []):
                subgroup = int(event.get("subgroup", -1))
                for part in range(real_parts):
                    if subgroup in (-1, part):
                        part_slots[part].add(pair)
                        part_subjects[part][
                            (bool(event.get("is_lab", False)), event.get("name", ""))
                        ] += 1
                key = (int(event["id"]), day["date_iso"], pair)
                if key in seen:
                    continue
                seen.add(key)
                lesson = lessons[int(event["id"])]
                teacher_id = int(event["teacher_id"])
                teacher = teachers[teacher_id]
                name = teacher["name"]
                if current in unavailable[teacher_id] or not rule_allows(teacher, current, pair):
                    availability_violations.append({"teacher": name, "date": day["date_iso"], "pair": pair})
                room = rooms.get(int(event.get("room_id", -1)))
                campus = int(room.get("campus", -1)) if room else -1
                allowed_campuses = [int(value) for value in teacher.get("allowed_campuses", [])]
                if allowed_campuses and campus not in allowed_campuses:
                    wrong_strict_campus.append({"teacher": name, "date": day["date_iso"], "pair": pair, "campus": campus, "allowed": allowed_campuses})
                if room:
                    access_mode = room.get("access_mode", "general")
                    owner_ids = {int(value) for value in room.get("responsible_teacher_ids", [])}
                    if access_mode == "blocked" or room.get("active") is False:
                        blocked_room_events.append({"teacher": name, "date": day["date_iso"], "pair": pair, "room": room.get("name")})
                    if access_mode == "exclusive" and teacher_id not in owner_ids:
                        room_access_violations.append({"teacher": name, "date": day["date_iso"], "pair": pair, "room": room.get("name"), "owner_ids": sorted(owner_ids)})
                default = rooms.get(int(teacher.get("default_room", -1)))
                if default and campus != int(default.get("campus", -1)):
                    wrong_default_campus.append({"teacher": name, "date": day["date_iso"], "pair": pair, "campus": campus, "room": event.get("room_name")})
                record = {"date": day["date_iso"], "pair": pair, "room": event.get("room_name"), "campus": campus}
                if name == "Рабенок Мария Александровна": rabenok.append(record)
                if name == "Попова Татьяна Вильевна":
                    popova_actual.append((day["date_iso"], pair, int(event["id"])))
                if name == "Нуров Мирзо Нуралиевич": nurov.append(record)
                if name == "Силенок Марина Юрьевна": silenok.append(record)
                if name == "Цимфер Татьяна Ивановна": tsimfer.append(record)
                if name == "Ахметов Артур Фанависович": akhmetov.append(record)
                if str(event.get("room_name")) == "52": room_52_events.append(record | {"teacher": name})
                if lesson.get("is_block") or lesson.get("is_pp"): up_pp.append(int(event["id"]))
                occurrences[(int(lesson.get("group", -1)), int(lesson.get("subject_id", -1)))].append(
                    (day["date_iso"], pair, bool(lesson.get("is_lab", False)), int(event["id"]))
                )
        daily_group_counts[len(group_slots)] += 1
        for values in part_slots:
            daily_part_counts[len(values)] += 1
        for part, values in enumerate(part_slots, start=1):
            if values and max(values) - min(values) + 1 != len(values):
                student_window_violations.append({
                    "group": group["group_name"], "date": day["date_iso"],
                    "part": part, "slots": sorted(values),
                })
        for part, counts in enumerate(part_subjects, start=1):
            for (is_lab, subject), count in counts.items():
                if count > 2:
                    same_subject_violations.append({
                        "group": group["group_name"], "date": day["date_iso"],
                        "part": part, "subject": subject, "is_lab": is_lab,
                        "count": count,
                    })

early_labs = []
for key, values in occurrences.items():
    theory_seen = 0
    for date_iso, pair, is_lab, lesson_id in sorted(values):
        if is_lab and theory_seen < 2:
            early_labs.append({"group": key[0], "subject": key[1], "lesson": lesson_id, "prior_theory": theory_seen})
        elif not is_lab:
            theory_seen += 1

print(json.dumps({
    "events": len(seen),
    "group_day_pair_distribution": dict(sorted(daily_group_counts.items())),
    "subgroup_day_pair_distribution": dict(sorted(daily_part_counts.items())),
    "availability_violations": availability_violations,
    "strict_campus_violations": wrong_strict_campus,
    "room_access_violations": room_access_violations,
    "blocked_room_events": blocked_room_events,
    "default_campus_mismatches": len(wrong_default_campus),
    "default_campus_mismatch_by_teacher": dict(Counter(item["teacher"] for item in wrong_default_campus)),
    "room_52_events": room_52_events,
    "up_pp_events": up_pp,
    "early_labs": early_labs,
    "same_subject_violations": same_subject_violations,
    "student_window_violations": student_window_violations,
    "popova": sorted(popova_actual),
    "rabenok": sorted(rabenok, key=lambda x: (x["date"], x["pair"])),
    "nurov_campuses": dict(Counter(x["campus"] for x in nurov)),
    "silenok_campuses": dict(Counter(x["campus"] for x in silenok)),
    "tsimfer_campuses": dict(Counter(x["campus"] for x in tsimfer)),
    "akhmetov_rooms": dict(Counter(str(x["room"]) for x in akhmetov)),
}, ensure_ascii=False, indent=2))
