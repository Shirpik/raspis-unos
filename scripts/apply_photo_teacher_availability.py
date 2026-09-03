#!/usr/bin/env python3
"""Apply teacher availability transcribed from the two photographed sheets."""

from __future__ import annotations

import hashlib
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "timetable_data.json"
HISTORY = ROOT / "data" / "history"

PHOTO_NAMES = [
    "Абрамчук Раида Филимазовна", "Азарян Карине Айказовна", "Альшаева Алина Павловна",
    "Аринкус Татьяна Юрьевна", "Ахметов Артур Фанависович",
    "Безбородов Игорь Алексеевич", "Билалова Рауиза Галлинуровна", "Бобылева Екатерина Дмитриевна",
    "Бородай Светлана Александровна", "Бурдин Алексей Александрович", "Буркова Елена Владимировна",
    "Вагайская Татьяна Александровна", "Вальдиянов Ян Мансурович", "Галузин Антон Илюсович",
    "Гарбузов Андрей Евгеньевич", "Гусакова Наталья Михайловна", "Давыдова Валентина Алексеевна",
    "Демина Екатерина Алексеевна", "Домарева Ирина Вячеславовна", "Дорохова Евгения Владимировна",
    "Дроговейко Дарья Юрьевна", "Елагина Ольга Александровна", "Ермолина Ирина Павловна",
    "Жилина Наталья Владимировна", "Журавский Константин Сергеевич", "Кальчевская Наталья Владимировна",
    "Кибардина Татьяна Леонидовна", "Кириллова Татьяна Олеговна", "Клабуков Василий Витальевич",
    "Кобылянская Татьяна Александровна", "Колтышев Евгений Валерьевич", "Комарова Яна Николаевна",
    "Конева Кристина Сергеевна", "Коробкова Эльвира Федоровна", "Костарева Наталья Викторовна",
    "Кошелев Дмитрий Александрович", "Кропотова Анастасия Андреевна", "Круглова Юлия Петровна",
    "Кузнецова Елизавета Олеговна", "Ланитина Елена Сергеевна", "Лимонова Евгения Николаевна",
    "Логинова Ксения Ивановна", "Лучинин Александр Васильевич", "Меренчуков Иван Александрович",
    "Михайлова Татьяна Алексеевна", "Никонова Антонина Олеговна", "Нифонтова Ирина Геннадьевна",
    "Новосёлова Светлана Юрьевна", "Нуриахметова Инесса Сергеевна", "Нуров Мирзо Нуралиевич",
    "Осипчук Александр Александрович", "Письмак Владимир Николаевич", "Подчиненнов Александр Юрьевич",
    "Попова Юлия Александровна", "Попова Татьяна Вильевна", "Потапова Регина Александровна",
    "Рабенок Мария Александровна", "Садриева Татьяна Геннадьевна", "Саламатина Вера Викторовна",
    "Самцов Андрей Евгеньевич", "Семенова Лилиана Ивановна", "Серянина Светлана Федоровна",
    "Сивилькаев Вадим Михайлович", "Силенок Марина Юрьевна", "Синельникова Елена Владимировна",
    "Соболева Любовь Анатольевна", "Сутягин Артём Борисович", "Тарасов Игорь Викторович",
    "Тарасюк Татьяна Ивановна", "Тимеров Валерий Вагизович", "Третяк Надежда Александровна",
    "Ханьжина Ксения Владимировна", "Хасанова Гузель Хамитовна", "Цимфер Татьяна Ивановна",
    "Чадова Марина Александровна", "Черепанова Татьяна Михайловна", "Шадрина Елена Федоровна",
    "Шалых Борис Сергеевич", "Щеткова", "Ярославцева Елена Анатольевна",
]

# Confirmed by the dispatcher on 2026-08-31. Days are ISO weekdays: Mon=1.
RULES = {
    "Абрамчук Раида Филимазовна": {d: (1, 5) for d in range(1, 6)},
    "Азарян Карине Айказовна": {d: (1, 5) for d in (1, 2, 3, 5)},
    "Альшаева Алина Павловна": {6: (1, 7)},
    "Бобылева Екатерина Дмитриевна": {2: (6, 7), 4: (4, 7)},
    "Билалова Рауиза Галлинуровна": {6: (1, 7)},
    "Бородай Светлана Александровна": {d: (1, 7) for d in range(1, 6)},
    "Буркова Елена Владимировна": {5: (1, 7), 6: (1, 7)},
    "Вагайская Татьяна Александровна": {d: (1, 7) for d in range(1, 6)},
    "Галузин Антон Илюсович": {6: (1, 7)},
    "Гарбузов Андрей Евгеньевич": {d: (1, 7) for d in range(1, 7)},
    "Демина Екатерина Алексеевна": {d: (1, 4) for d in range(1, 6)},
    "Домарева Ирина Вячеславовна": {d: (2, 4) for d in range(1, 7)},
    "Дорохова Евгения Владимировна": {
        1: (1, 7), 2: (1, 7), 3: (1, 7), 4: (1, 3), 5: (1, 7), 6: (1, 7)
    },
    "Клабуков Василий Витальевич": {d: (1, 7) for d in (2, 3, 4)},
    "Кобылянская Татьяна Александровна": {d: (1, 7) for d in range(1, 6)},
    "Колтышев Евгений Валерьевич": {d: (2, 7) for d in range(1, 7)},
    "Коробкова Эльвира Федоровна": {},
    "Кошелев Дмитрий Александрович": {6: (1, 7)},
    "Кропотова Анастасия Андреевна": {d: (1, 7) for d in range(1, 7)},
    "Михайлова Татьяна Алексеевна": {1: (1, 4), 2: (1, 4), 3: (1, 4), 4: (1, 4), 5: (1, 4)},
    "Никонова Антонина Олеговна": {d: (1, 3) for d in range(1, 7)},
    "Новосёлова Светлана Юрьевна": {2: (1, 4), 3: (1, 4), 5: (1, 4)},
    "Осипчук Александр Александрович": {6: (1, 7)},
    "Письмак Владимир Николаевич": {1: (4, 6), 2: (4, 6), 3: (4, 6), 4: (4, 6), 5: (4, 6), 6: (4, 6)},
    "Подчиненнов Александр Юрьевич": {1: (1, 7), 2: (1, 7), 3: (1, 7), 4: (1, 7), 5: (1, 7), 6: (1, 7)},
    "Рабенок Мария Александровна": {3: [2, 4], 4: [2, 4], 5: [2, 4]},
    "Сутягин Артём Борисович": {5: (1, 7)},
    "Тарасов Игорь Викторович": {1: (1, 2), 2: (1, 2), 3: (1, 2), 4: (1, 2), 5: (1, 2), 6: (1, 2)},
    "Ханьжина Ксения Владимировна": {1: (6, 7), 2: (6, 7), 3: (6, 7), 4: (6, 7), 5: (6, 7), 6: (1, 4)},
    "Цимфер Татьяна Ивановна": {1: (1, 5), 2: (1, 5), 3: (1, 5), 4: (1, 5), 5: (1, 5)},
    "Черепанова Татьяна Михайловна": {1: (1, 5), 2: (1, 5), 3: (1, 5), 4: (1, 5), 5: (1, 5)},
    "Ярославцева Елена Анатольевна": {1: (1, 5), 2: (1, 5), 3: (1, 5), 4: (1, 5), 5: (1, 5), 6: (1, 5)},
}

DISABLED_TEACHERS = {
    "Безбородов Игорь Алексеевич": "Без часов, занятия пока не ставить",
    "Коробкова Эльвира Федоровна": "Занятия пока не ставить",
    "Серянина Светлана Федоровна": "Нет с 01.09, дата возвращения не указана",
    "Третяк Надежда Александровна": "Пока нет назначенных пар",
}

UNAVAILABILITY = {
    "Вальдиянов Ян Мансурович": [{"dates": ["2026-09-10"]}],
    "Дроговейко Дарья Юрьевна": [{"from": "2026-09-01", "to": "2026-09-12"}],
    "Лучинин Александр Васильевич": [{"dates": ["2026-09-03"]}],
    "Потапова Регина Александровна": [{"dates": ["2026-09-23"]}],
}

DATE_SLOT_OVERRIDES = {
    "Рабенок Мария Александровна": {
        "2026-09-02": [4],
        "2026-09-03": [2, 4],
        "2026-09-04": [2, 4],
        "2026-09-05": [],
        "2026-09-07": [],
        "2026-09-08": [],
        "2026-09-09": [2, 4],
        "2026-09-10": [],
        "2026-09-11": [],
        "2026-09-12": [],
    },
}

NOTES = {
    "Вагайская Татьяна Александровна": "ПН–ПТ, не более 2 пар в день",
    "Колтышев Евгений Валерьевич": "Один рабочий день в неделю, 2–7 пары",
    "Подчиненнов Александр Юрьевич": "ПН–СБ, 1–7 пары; на фото также была пометка 14:00–19:00",
    "Рабенок Мария Александровна": "1-я неделя: СР-4, ЧТ-2/4, ПТ-2/4; 2-я неделя: СР-2/4",
    "Серянина Светлана Федоровна": "«нет с 1.09 — отдыхает», дата окончания отсутствует.",
    "Третяк Надежда Александровна": "Пока нет назначенных пар",
}


def work_days(rule: dict[int, tuple[int, int] | list[int]]) -> list[dict]:
    result = []
    for day in range(1, 8):
        raw = rule.get(day)
        if raw is None:
            selected = []
        elif isinstance(raw, tuple):
            selected = list(range(raw[0], raw[1] + 1))
        else:
            selected = sorted({int(slot) for slot in raw if 1 <= int(slot) <= 7})
        result.append({
            "day": day,
            "enabled": bool(selected),
            "start_slot": selected[0] if selected else 1,
            "end_slot": selected[-1] if selected else 7,
            "slots": selected,
        })
    return result


def main() -> None:
    HISTORY.mkdir(parents=True, exist_ok=True)
    backup = HISTORY / f"before_teacher_availability_{datetime.now():%Y%m%d-%H%M%S}.json"
    shutil.copy2(DATA, backup)
    data = json.loads(DATA.read_text(encoding="utf-8"))
    teachers = data.setdefault("teachers", [])
    by_name = {teacher.get("name"): teacher for teacher in teachers}
    next_id = max((int(t.get("id", -1)) for t in teachers), default=-1) + 1
    added = []
    for name in PHOTO_NAMES:
        if name in by_name:
            continue
        teacher = {
            "id": next_id,
            "uid": "teacher-" + hashlib.sha1(name.encode("utf-8")).hexdigest()[:16],
            "name": name,
            "work_period": {"from": "", "to": ""},
            "work_days": work_days({day: (1, 7) for day in range(1, 7)}),
            "default_room": -1,
            "campus_priority": [],
            "max_work_days_per_week": 0,
            "max_pairs_per_day": 0,
            "date_slot_overrides": [],
        }
        next_id += 1
        teachers.append(teacher)
        by_name[name] = teacher
        added.append(name)

    for name in PHOTO_NAMES:
        teacher = by_name[name]
        teacher.setdefault("work_period", {"from": "", "to": ""})
        teacher["work_days"] = work_days(RULES.get(name, {day: (1, 7) for day in range(1, 7)}))
        teacher["max_work_days_per_week"] = 0
        teacher["max_pairs_per_day"] = 0
        teacher["date_slot_overrides"] = []
        teacher["availability_note"] = NOTES.get(name, DISABLED_TEACHERS.get(name, ""))
    for name in DISABLED_TEACHERS:
        by_name[name]["work_days"] = work_days({})
    by_name["Колтышев Евгений Валерьевич"]["max_work_days_per_week"] = 1
    by_name["Вагайская Татьяна Александровна"]["max_pairs_per_day"] = 2
    for name, overrides in DATE_SLOT_OVERRIDES.items():
        by_name[name]["date_slot_overrides"] = [
            {"date": date, "slots": slots} for date, slots in sorted(overrides.items())
        ]

    unavailable = data.setdefault("teacher_unavailable", [])
    # These names were managed by the previous photo import as well. Replacing
    # their rows removes obsolete and duplicate entries for Елагина/Галузин.
    managed_names = set(UNAVAILABILITY) | {
        "Елагина Ольга Александровна", "Галузин Антон Илюсович"
    }
    managed_ids = {int(by_name[name]["id"]) for name in managed_names}
    unavailable[:] = [
        item for item in unavailable if int(item.get("teacher", -1)) not in managed_ids
    ]
    for name, entries in UNAVAILABILITY.items():
        for entry in entries:
            item = {
                "id": max((int(x.get("id", -1)) for x in unavailable), default=-1) + 1,
                "teacher": int(by_name[name]["id"]),
                "text": "Подтверждено диспетчером 31.08.2026",
                **entry,
            }
            unavailable.append(item)

    data.setdefault("meta", {})["teacher_availability_photo"] = {
        "applied_at": datetime.now(timezone.utc).isoformat(),
        "source_pages": 2,
        "teachers_on_photo": len(PHOTO_NAMES),
        "explicit_rules_applied": sorted(RULES),
        "unavailability_applied": UNAVAILABILITY,
        "date_slot_overrides_applied": DATE_SLOT_OVERRIDES,
        "disabled_teachers": DISABLED_TEACHERS,
        "added_without_current_workload": added,
        "name_aliases_from_confirmation": {
            "Аринкина Татьяна Юрьевна": "Аринкус Татьяна Юрьевна",
            "Кузнецова Татьяна Олеговна": "Кириллова Татьяна Олеговна",
            "Полякова Юлия Александровна": "Попова Юлия Александровна",
        },
        "interpretation": "Подтверждено пользователем; неуказанные ограничения = ПН–СБ, 1–7 пары.",
        "backup": str(backup),
    }
    DATA.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"teachers": len(teachers), "added": added, "rules": len(RULES)}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
