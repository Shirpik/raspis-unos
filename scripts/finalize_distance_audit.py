"""Refresh forecasts and install only verified calendar/settings into the live data."""
import copy
import json
import subprocess
from pathlib import Path
from prepare_friday_saturday_20260918 import prepare, write

ROOT = Path(__file__).resolve().parents[1]
FOLDER = ROOT/'outputs/solver-audit-20260918'
solver = ROOT/'.tmp/build-sep7/Release/timetable_solver.exe'
source = json.loads((FOLDER/'updated-source.json').read_text(encoding='utf-8-sig'))
# Rebuild metadata after adding front-end date targets, keeping the proven witness.
prepare(source, FOLDER)
base = json.loads((FOLDER/'base-data.json').read_text(encoding='utf-8'))
selected = json.loads((FOLDER/'data/timetable_data.json').read_text(encoding='utf-8'))
selected['teachers'] = copy.deepcopy(base['teachers'])
write(FOLDER/'data/timetable_data.json', selected)
validation = json.loads((FOLDER/'final-validation.json').read_text(encoding='utf-8'))
assert validation['ok']

def forecast(data, filename):
    path = FOLDER/(filename+'-data.json')
    write(path,data)
    proc = subprocess.run([str(solver),'--semester-readout','--data',str(path)], capture_output=True,encoding='utf-8',timeout=90)
    if proc.returncode not in (0,1) or not proc.stdout: raise RuntimeError(proc.stderr)
    report = json.loads(proc.stdout)
    assert report['rules_version'] >= 3
    write(FOLDER/(filename+'.json'),report)
    return report

before = forecast(selected,'forecast')
after_data = copy.deepcopy(base)
schedule = json.loads((FOLDER/'candidate-cp/schedule_all.json').read_text(encoding='utf-8'))
after_data['teaching_ledger'] = [r for r in after_data['teaching_ledger'] if not (r.get('status')=='planned' and '2026-09-18'<=r.get('date','')<='2026-09-19')]
next_id = max((r.get('id',0) for r in after_data['teaching_ledger']),default=0)+1
for group in schedule['groups']:
    for day in group['days']:
        for slot in day['slots']:
            for lesson in slot['lessons']:
                after_data['teaching_ledger'].append({'id':next_id,'lesson_id':lesson['id'],'date':day['date_iso'],'slot':slot['slot'],'hours':2,'status':'planned','source_file':'distance-draft-18-19.09.2026','group_id':group['group_index']})
                next_id += 1
after_data['settings'].update(start_date='2026-09-21',end_date='2026-09-26',automatic_period_quotas=True)
after = forecast(after_data,'forecast-after-draft')

# Install source calendar and course deadlines without replacing facts, future
# reservations, teacher constraints, or the user's currently selected period.
live_path = ROOT/'data/timetable_data.json'
live = json.loads(live_path.read_text(encoding='utf-8-sig'))
write(FOLDER/'before-calendar-install.json',live)
calendar = {g['id']:g for g in source['groups']}
for group in live['groups']:
    for key in ('academic_calendar','calendar_theory_semester_hours','practice_calendar_source','practice_periods'):
        group[key] = copy.deepcopy(calendar[group['id']][key])
for key in ('semester_start_date','semester_end_date','first_course_semester_end_date'):
    live['settings'][key] = source['settings'][key]
assert live['teaching_ledger'] == json.loads((FOLDER/'before-calendar-install.json').read_text(encoding='utf-8'))['teaching_ledger']
write(live_path,live)

names={t['id']:t['name'] for t in base['teachers']}
risk={}
for row in after['rows']:
    shortfall=max(0,row['remaining_regular_hours']-2*row['capacity_pairs_until_deadline'])
    if shortfall and shortfall>risk.get(row['teacher'],{}).get('shortfall',0):
        risk[row['teacher']]={'name':names[row['teacher']],'deadline':row['deadline'],'remaining':row['remaining_regular_hours'],'capacity':2*row['capacity_pairs_until_deadline'],'shortfall':shortfall}
write(FOLDER/'capacity-risks.json',list(risk.values()))
lines=['# Аудит решателя и расписание 18–19 сентября 2026','',
       'Расписание получено CP-SAT. Найден допустимый вариант; глобальная оптимальность не доказана. Отдельный валидатор проверил исходные часы и размещение занятий.', '',
       '## Что исправлено', '',
       '- Остатки уменьшаются на подтверждённые часы и отдельный резерв запланированных занятий до начала периода. Резерв не превращается в проведённые часы. Классные часы исключены.',
       '- Для первого курса конец семестра — 26 декабря, для 2–4 курсов — 19 декабря. Срок сокращается до последнего теоретического периода перед ПП или завершающей учебной практикой.',
       '- Темп вычитки определяется оставшимися часами и доступным временем до индивидуального срока. Нельзя добавлять уже исчерпанную теорию.',
       '- В настройках сайта добавлен конец семестра первого курса. В прогнозе видны предметы, факт, резерв, оставшиеся учебные недели и требуемые часы в неделю.',
       '- Общая пара записывается только в верхнюю ячейку. Занятия подгрупп занимают соответствующие отдельные ячейки.', '',
       '## Источник календаря', '',
       '[Рабочее время групп](https://docs.google.com/spreadsheets/d/1YQ4TsERPrgtNNrC1weyDPox8hwIjU8XDfaM9XDO2X4U/edit?gid=517842105#gid=517842105). Скачанная копия проанализирована локально; исходная Google-таблица не изменялась. Сопоставлены 46 групп. СП-4611 и СП-4612п сохранены разными группами.', '',
       '| Группы | Закончить обычные занятия до |','|---|---|']
grouped={}
for g in after['groups']: grouped.setdefault(g['deadline'],[]).append(g['group_name'])
for end,gs in sorted(grouped.items()): lines.append(f"| {', '.join(gs)} | {end} |")
lines += ['', '## Нагрузка 18–19 сентября', '', f"Всего {validation['total_pairs']} преподавательских пар: 235 в пятницу и 228 в субботу. Для всех 90 физических подгрупп — ровно 4 пары ежедневно. Общие занятия считаются одним событием; занятия двух подгрупп — двумя.", '', '| Преподаватель | Пятница | Суббота | Всего |','|---|---:|---:|---:|']
for t in validation['teacher_loads']: lines.append(f"| {t['name']} | {t['friday']} | {t['saturday']} | {t['total']} |")
lines += ['', 'Рабенок: по 3 пары, шесть разных ранее знакомых групп; ЭОЭ-Пф-151 исключена. У Третяк после проекта остаётся 4 часа общей теории. Подгрупповые часы информатики показаны отдельно и этим требованием не закрываются.', '',
          'По ранее обсуждавшимся ограничениям сохранены исключения Тарасова и Письмака на оба дня, Ханьжиной на субботу. Субботний предел Семеновой 1–4 снят. Это принятые при генерации допущения, пока не получено другое уточнение.', '',
          '## Риски после выполнения проекта', '',
          'Расчёт на 21 сентября условный: занятия проекта считаются резервом, а не фактом. Оценка доступных часов — верхняя граница. Даже её достаточность не доказывает возможность полного расписания до конца семестра.', '',
          '| Преподаватель | Срок | Остаток, ч | Доступно не более, ч | Не хватает, ч |','|---|---|---:|---:|---:|']
for r in risk.values(): lines.append(f"| {r['name']} | {r['deadline']} | {r['remaining']} | {r['capacity']} | {r['shortfall']} |")
lines += ['', 'Причины требуют организационных решений: изменить доступные дни, передать часть часов или исправить нагрузку при подтверждённой ошибке исходника. У Третяк будущие регулярные рабочие дни не заданы; у Осипчука после этих дат в базе остаётся суббота. Для Михайловой сохранены понедельник–пятница, пары 1–4. Прогноз не скрывает эти ограничения.', '',
          'Дополнительно отчёт содержит предметы без действующего преподавателя, расхождения календарного объёма и вклеек, нераспределённое руководство практикой и остатки, не делящиеся на размер учебного блока. Эти исходные проблемы нельзя устранить перестановкой пар двух дней.', '',
          '## Проверки и границы результата', '',
          '- Независимый аудит текущего расписания: пересечения, доступность, окна преподавателей и студентов, четыре пары на подгруппу, блоки ЛПЗ, остатки часов, дедлайны и обязательная нагрузка — пройдены.',
          '- Регрессионные проверки: подтверждённые часы/резерв/дубликаты/даты среза; дедлайны первого курса и УП перед ПП; дневные квоты и общий предел выбранных предметов; C++ validator — пройдены.',
          '- Рабочая база обновлена только в части календаря и дат семестра. Факт и ранее запланированные занятия сохранены. Новый вариант 18–19 сентября остаётся отдельным проектом, а не проведёнными занятиями.',
          '- Полное расписание семестра не построено. Общая генерация сайта и точная двухдневная CP-SAT-модель — разные пути; все специальные условия этого проекта сохранены в его входной модели и снимке данных.', '']
(FOLDER/'Аудит_решателя_и_вычитки.md').write_text('\n'.join(lines),encoding='utf-8')
print(json.dumps({'risks':list(risk.values()),'calendar_groups':len(calendar),'ledger_unchanged':True},ensure_ascii=False))
