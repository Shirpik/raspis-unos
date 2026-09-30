#!/usr/bin/env python3
"""
Уменьшает total_slots для всех уроков на 30%, чтобы сделать расписание feasible.
Запустите: python fix_overload.py
"""
import json
import shutil
from datetime import datetime

# Создаем резервную копию
backup_path = f'data/timetable_data.backup.{datetime.now().strftime("%Y%m%d_%H%M%S")}.json'
shutil.copy('data/timetable_data.json', backup_path)
print(f"Резервная копия создана: {backup_path}")

# Загружаем данные
data = json.load(open('data/timetable_data.json', encoding='utf-8'))

# Уменьшаем total_slots
reduced_count = 0
for lesson in data['lessons']:
    old_slots = lesson.get('total_slots', 0)
    if old_slots > 0:
        lesson['total_slots'] = max(1, int(old_slots * 0.7))
        reduced_count += 1

print(f"Уменьшено total_slots для {reduced_count} уроков на 30%")

# Сохраняем
with open('data/timetable_data.json', 'w', encoding='utf-8') as f:
    json.dump(data, f, ensure_ascii=False, indent=2)

print("✓ Готово! Теперь попробуйте генерацию.")
