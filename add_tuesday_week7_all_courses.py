#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import json
from datetime import datetime

# Read the JSON file
with open('data/timetable_data.json', 'r', encoding='utf-8') as f:
    data = json.load(f)

# Map of teacher names to IDs
teacher_ids = {
    'Соболева': 20, 'Семенова': 10, 'Цимфер': 11, 'Силенок': 19,
    'Журавский': 23, 'Кибардина': 22, 'Хасанова': 0, 'Демина': 2,
    'Потапова': 8, 'Кобылянская': 7, 'Бобылева': 6, 'Никонова': 3,
    'Третяк': 18, 'Логинова': 32, 'Домарева': 24
}

# Map of group names to IDs
group_ids = {
    'ТАКХС-Пф-1203': 0, 'ТМ-1419': 1, 'ТМ-1420': 2, 'МЦМ-Пф-103': 3,
    'СП-Пф-1605': 4, 'СП-Пф-1606п': 5, 'ЭОЭ-Пф-151': 6, 'РУП-1301п': 7,
    'ПКД-1809': 8, 'ТОиРА-1702': 9, 'ТОиРА-1703п': 10
}

# Create lessons index by group and normalized subject name
def normalize_subject(name):
    name = name.lower().strip()
    mapping = {
        'физическая культура': ['физическая культура', 'физкультура', 'physical education'],
        'обществознание': ['обществознание', 'social studies'],
        'основы безопасности и защита родины': ['основы безопасности и защита родины', 'основы безопасности и защиты родины', 'обж', 'основы безопасности', 'safety basics'],
        'химия': ['химия', 'chemistry'],
        'биология': ['биология', 'biology'],
        'индивидуальный проект': ['индивидуальный проект', 'individual project'],
        'математика': ['математика', 'mathematics'],
        'русский язык': ['русский язык', 'russian language'],
        'физика': ['физика', 'physics'],
        'литература': ['литература', 'literature'],
        'информатика': ['информатика', 'computer science'],
        'введение в специальность': ['введение в специальность']
    }
    for key, variants in mapping.items():
        for variant in variants:
            if variant in name:
                return key
    return name

lessons_index = {}
for lesson in data.get('lessons', []):
    group_id = lesson.get('group')
    subject = normalize_subject(lesson.get('name', ''))
    key = (group_id, subject)
    if key not in lessons_index:
        lessons_index[key] = []
    lessons_index[key].append(lesson)

# Helper function to find lesson ID
def find_lesson(group_name, subject_name, teacher_name=None, subgroup=None):
    group_id = group_ids.get(group_name)
    if group_id is None:
        return None

    subject_norm = normalize_subject(subject_name)
    key = (group_id, subject_norm)
    candidates = lessons_index.get(key, [])

    if not candidates:
        return None

    # Filter by subgroup if specified
    if subgroup is not None:
        candidates = [l for l in candidates if l.get('subgroup') == subgroup]

    # Filter by teacher if specified
    if teacher_name and teacher_name in teacher_ids:
        teacher_id = teacher_ids[teacher_name]
        candidates = [l for l in candidates if l.get('teacher') == teacher_id]

    return candidates[0] if candidates else lessons_index.get(key, [None])[0]

# Get current max ID and remove existing entries for 2026-10-13
teaching_ledger = data.get('teaching_ledger', [])
# Remove any existing entries for this date to avoid duplicates
teaching_ledger = [entry for entry in teaching_ledger if entry.get('date') != '2026-10-13']
data['teaching_ledger'] = teaching_ledger

next_id = max([entry.get('id', 0) for entry in teaching_ledger], default=0) + 1

# Tuesday week 7 date
date = "2026-10-13"
import_time = datetime.now().isoformat() + 'Z'

# Complete schedule data for Tuesday week 7 (all courses combined)
schedule = [
    # Slot 1 (8:30-9:10)
    {'group': 'ЭОЭ-Пф-151', 'slot': 1, 'subject': 'Физическая культура', 'teacher': 'Кобылянская', 'room': 'Спортзал', 'hours': 2},
    {'group': 'РУП-1301п', 'slot': 1, 'subject': 'Обществознание', 'teacher': 'Демина', 'room': '28_К', 'hours': 2},
    {'group': 'ТОиРА-1702', 'slot': 1, 'subject': 'Обществознание', 'teacher': 'Демина', 'room': '28_К', 'hours': 2},
    {'group': 'ТОиРА-1703п', 'slot': 1, 'subject': 'Основы безопасности и защита родины', 'teacher': 'Потапова', 'room': '47_К', 'hours': 2},
    {'group': 'ТМ-1419', 'slot': 1, 'subject': 'Физическая культура', 'teacher': 'Кобылянская', 'room': 'Спортзал', 'hours': 2},
    {'group': 'ТМ-1420', 'slot': 1, 'subject': 'Обществознание', 'teacher': 'Демина', 'room': '28_К', 'hours': 2},
    {'group': 'МЦМ-Пф-103', 'slot': 1, 'subject': 'Основы безопасности и защита родины', 'teacher': 'Потапова', 'room': '47_К', 'hours': 2},
    {'group': 'ПКД-1809', 'slot': 1, 'subject': 'Физическая культура', 'teacher': 'Кобылянская', 'room': 'Спортзал', 'hours': 2},

    # Slot 2 (10:05-10:45)
    {'group': 'ТАКХС-Пф-1203', 'slot': 2, 'subject': 'Химия', 'teacher': 'Соболева', 'room': '15_К', 'hours': 2},
    {'group': 'ТМ-1419', 'slot': 2, 'subject': 'Физическая культура', 'teacher': 'Кобылянская', 'room': 'Спортзал', 'hours': 2},
    {'group': 'ТМ-1420', 'slot': 2, 'subject': 'Биология', 'teacher': 'Никонова', 'room': '41_К', 'hours': 2},
    {'group': 'МЦМ-Пф-103', 'slot': 2, 'subject': 'Индивидуальный проект', 'teacher': 'Цимфер', 'room': '31_К', 'hours': 2},
    {'group': 'СП-Пф-1605', 'slot': 2, 'subject': 'Математика', 'teacher': 'Журавский', 'room': '63_К', 'hours': 2},
    {'group': 'СП-Пф-1606п', 'slot': 2, 'subject': 'Математика', 'teacher': 'Журавский', 'room': '63_К', 'hours': 2},
    {'group': 'ЭОЭ-Пф-151', 'slot': 2, 'subject': 'Русский язык', 'teacher': 'Кибардина', 'room': '33_К', 'hours': 2},
    {'group': 'РУП-1301п', 'slot': 2, 'subject': 'Обществознание', 'teacher': 'Демина', 'room': '28_К', 'hours': 2},
    {'group': 'ПКД-1809', 'slot': 2, 'subject': 'Биология', 'teacher': 'Никонова', 'room': '41_К', 'hours': 2},
    {'group': 'ТОиРА-1702', 'slot': 2, 'subject': 'Индивидуальный проект', 'teacher': 'Цимфер', 'room': '31_К', 'hours': 2},
    {'group': 'ТОиРА-1703п', 'slot': 2, 'subject': 'Математика', 'teacher': 'Журавский', 'room': '63_К', 'hours': 2},

    # Slot 3 (12:25-13:05)
    {'group': 'ТАКХС-Пф-1203', 'slot': 3, 'subject': 'Химия', 'teacher': 'Семенова', 'room': '54_К', 'hours': 2},
    {'group': 'ТМ-1419', 'slot': 3, 'subject': 'Обществознание', 'teacher': 'Цимфер', 'room': '31_К', 'hours': 2},
    {'group': 'ТМ-1420', 'slot': 3, 'subject': 'Физика', 'teacher': 'Силенок', 'room': '22_К', 'hours': 2},
    {'group': 'МЦМ-Пф-103', 'slot': 3, 'subject': 'Обществознание', 'teacher': 'Демина', 'room': '28_К', 'hours': 2},
    {'group': 'СП-Пф-1605', 'slot': 3, 'subject': 'Математика', 'teacher': 'Журавский', 'room': '63_К', 'hours': 2},
    {'group': 'СП-Пф-1606п', 'slot': 3, 'subject': 'Физическая культура', 'teacher': 'Кобылянская', 'room': 'Спортзал', 'hours': 2},
    {'group': 'ЭОЭ-Пф-151', 'slot': 3, 'subject': 'Русский язык', 'teacher': 'Кибардина', 'room': '33_К', 'hours': 2},
    {'group': 'РУП-1301п', 'slot': 3, 'subject': 'Основы безопасности и защита родины', 'teacher': 'Потапова', 'room': '47_К', 'hours': 2},
    {'group': 'ПКД-1809', 'slot': 3, 'subject': 'Химия', 'teacher': 'Соболева', 'room': '15_К', 'hours': 2},
    {'group': 'ТОиРА-1702', 'slot': 3, 'subject': 'Математика', 'teacher': 'Журавский', 'room': '63_К', 'hours': 2},
    {'group': 'ТОиРА-1703п', 'slot': 3, 'subject': 'Русский язык', 'teacher': 'Кибардина', 'room': '33_К', 'hours': 2},

    # Slot 4 (14:00-14:40)
    {'group': 'ТАКХС-Пф-1203', 'slot': 4, 'subject': 'Химия', 'teacher': 'Семенова', 'room': '54_К', 'hours': 2},
    {'group': 'ТМ-1419', 'slot': 4, 'subject': 'Обществознание', 'teacher': 'Цимфер', 'room': '31_К', 'hours': 2},
    {'group': 'ТМ-1420', 'slot': 4, 'subject': 'Литература', 'teacher': 'Хасанова', 'room': '23_К', 'hours': 2},
    {'group': 'МЦМ-Пф-103', 'slot': 4, 'subject': 'Литература', 'teacher': 'Кибардина', 'room': '33_К', 'hours': 2},
    {'group': 'СП-Пф-1605', 'slot': 4, 'subject': 'Физика', 'teacher': 'Силенок', 'room': '22_К', 'hours': 2},
    {'group': 'СП-Пф-1606п', 'slot': 4, 'subject': 'Основы безопасности и защита родины', 'teacher': 'Потапова', 'room': '47_К', 'hours': 2},
    {'group': 'ЭОЭ-Пф-151', 'slot': 4, 'subject': 'Обществознание', 'teacher': 'Демина', 'room': '28_К', 'hours': 2},
    {'group': 'РУП-1301п', 'slot': 4, 'subject': 'Физическая культура', 'teacher': 'Кобылянская', 'room': 'Спортзал', 'hours': 2},
    {'group': 'ПКД-1809', 'slot': 4, 'subject': 'Основы безопасности и защита родины', 'teacher': 'Потапова', 'room': '47_К', 'hours': 2},
    {'group': 'ТОиРА-1702', 'slot': 4, 'subject': 'Физика', 'teacher': 'Силенок', 'room': '22_К', 'hours': 2},
    {'group': 'ТОиРА-1703п', 'slot': 4, 'subject': 'Обществознание', 'teacher': 'Демина', 'room': '28_К', 'hours': 2},

    # Slot 5 (15:35-16:55)
    {'group': 'ТАКХС-Пф-1203', 'slot': 5, 'subject': 'Информатика', 'teacher': 'Бобылева', 'room': '16_К', 'hours': 2, 'subgroup': 1},
    {'group': 'ТМ-1419', 'slot': 5, 'subject': 'Физика', 'teacher': 'Силенок', 'room': '22_К', 'hours': 2},
    {'group': 'ТМ-1419', 'slot': 5, 'subject': 'Информатика', 'teacher': 'Третяк', 'room': '60_К', 'hours': 2, 'subgroup': 2},
    {'group': 'ТМ-1420', 'slot': 5, 'subject': 'Химия', 'teacher': 'Соболева', 'room': '15_К', 'hours': 2},
    {'group': 'МЦМ-Пф-103', 'slot': 5, 'subject': 'Основы безопасности и защита родины', 'teacher': 'Потапова', 'room': '47_К', 'hours': 2},
    {'group': 'СП-Пф-1605', 'slot': 5, 'subject': 'Литература', 'teacher': 'Кибардина', 'room': '33_К', 'hours': 2},
    {'group': 'СП-Пф-1606п', 'slot': 5, 'subject': 'Обществознание', 'teacher': 'Цимфер', 'room': '31_К', 'hours': 2},
    {'group': 'ЭОЭ-Пф-151', 'slot': 5, 'subject': 'Обществознание', 'teacher': 'Цимфер', 'room': '31_К', 'hours': 2},
    {'group': 'ПКД-1809', 'slot': 5, 'subject': 'Основы безопасности и защита родины', 'teacher': 'Потапова', 'room': '47_К', 'hours': 2},
    {'group': 'ТОиРА-1702', 'slot': 5, 'subject': 'Химия', 'teacher': 'Соболева', 'room': '15_К', 'hours': 2},
    {'group': 'ТОиРА-1703п', 'slot': 5, 'subject': 'Литература', 'teacher': 'Кибардина', 'room': '33_К', 'hours': 2},

    # Slot 6 (17:05-18:25)
    {'group': 'ТАКХС-Пф-1203', 'slot': 6, 'subject': 'Информатика', 'teacher': 'Бобылева', 'room': '16_К', 'hours': 2, 'subgroup': 1},
    {'group': 'ТМ-1420', 'slot': 6, 'subject': 'Химия', 'teacher': 'Соболева', 'room': '15_К', 'hours': 2},
    {'group': 'СП-Пф-1605', 'slot': 6, 'subject': 'Русский язык', 'teacher': 'Кибардина', 'room': '33_К', 'hours': 2},
    {'group': 'СП-Пф-1606п', 'slot': 6, 'subject': 'Русский язык', 'teacher': 'Кибардина', 'room': '33_К', 'hours': 2},
    {'group': 'ЭОЭ-Пф-151', 'slot': 6, 'subject': 'Информатика', 'teacher': 'Логинова', 'room': '60_К', 'hours': 2, 'subgroup': 1},
    {'group': 'ПКД-1809', 'slot': 6, 'subject': 'Информатика', 'teacher': 'Логинова', 'room': '60_К', 'hours': 2, 'subgroup': 1},
    {'group': 'ТОиРА-1702', 'slot': 6, 'subject': 'Химия', 'teacher': 'Соболева', 'room': '15_К', 'hours': 2},
    {'group': 'ТОиРА-1703п', 'slot': 6, 'subject': 'Русский язык', 'teacher': 'Кибардина', 'room': '33_К', 'hours': 2},

    # Slot 7 (18:35-19:55)
    {'group': 'ЭОЭ-Пф-151', 'slot': 7, 'subject': 'Информатика', 'teacher': 'Логинова', 'room': '60_К', 'hours': 2, 'subgroup': 1},
    {'group': 'ПКД-1809', 'slot': 7, 'subject': 'Информатика', 'teacher': 'Логинова', 'room': '60_К', 'hours': 2, 'subgroup': 1},
    {'group': 'ТОиРА-1703п', 'slot': 7, 'subject': 'Информатика', 'teacher': 'Логинова', 'room': '60_К', 'hours': 2, 'subgroup': 1},
]

# Create new teaching_ledger entries
new_entries = []
skipped = []
for item in schedule:
    lesson = find_lesson(item['group'], item['subject'], item.get('teacher'), item.get('subgroup'))
    if not lesson:
        skipped.append(f"{item['group']} - {item['subject']} (slot {item['slot']})")
        continue

    entry = {
        "actual_teacher": teacher_ids.get(item['teacher']),
        "date": date,
        "group_id": group_ids[item['group']],
        "hours": item['hours'],
        "id": next_id,
        "imported_at": import_time,
        "lesson_id": lesson['id'],
        "room": item['room'],
        "slot": item['slot'],
        "source_file": "manual_import_week7_tuesday_all_courses.py",
        "source_sheet": "Week 7 Tuesday - All Courses",
        "source_subject": item['subject'],
        "source_teacher": f"{item['teacher']} · {item['room']}",
        "status": "confirmed"
    }
    new_entries.append(entry)
    next_id += 1

print(f"Created {len(new_entries)} new teaching_ledger entries")
if skipped:
    print(f"\nWarning: Could not find lessons for:")
    for s in skipped:
        print(f"  - {s}")

# Add new entries to teaching_ledger
data['teaching_ledger'].extend(new_entries)

# Write back to file
with open('data/timetable_data.json', 'w', encoding='utf-8') as f:
    json.dump(data, f, ensure_ascii=False, indent=2)

print(f"\nSuccessfully added {len(new_entries)} entries to teaching_ledger")
print(f"Date: {date}")
print("Status: All marked as 'confirmed' (completed and deducted)")
