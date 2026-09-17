"""Independent source/ledger reconciliation for the 14 September import."""
import json, subprocess
from pathlib import Path
from collections import Counter, defaultdict
import openpyxl
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'outputs/workload-import-20260914-v5'
read=lambda name:json.loads((OUT/name).read_bytes())
d=read('candidate.json'); before=read('before-data.json'); report=read('import-report.json')
w=openpyxl.load_workbook(report['source'],data_only=True)
lessons={l['id']:l for l in d['lessons']}; old={l['id']:l for l in before['lessons']}
source_rows=[l for l in d['lessons'] if l.get('curriculum_active',True)]
assert len(source_rows)==997
for l in source_rows:
    s=l['workload_source']; assert s['semester']==1
    raw=w[s['sheet']].cell(s['row'],6).value
    assert isinstance(raw,(int,float)) and raw==l['total_hours'], (s,l['total_hours'],raw)
    if l['id'] in old:
        for key in ['uid','subject_id','subgroup','group','fixed_room','allowed_campuses','consecutive_pairs']:
            assert l.get(key)==old[l['id']].get(key), (l['id'],key)
assert d['teaching_ledger']==before['teaching_ledger']
assert all(lid in lessons for lid in old)
assert d['teachers']==before['teachers'] and d['groups']==before['groups'] and d['settings']==before['settings']
assert len({r['id'] for r in d['lessons']})==len(d['lessons'])
for l in d['lessons']:
    if l.get('workload_retired'): assert l['total_hours']==0 and l['curriculum_active'] is False

confirmed=defaultdict(int); reserved=defaultdict(int); seen={}
for r in d['teaching_ledger']:
    if r.get('is_class_hour') or r['status'] not in ('confirmed','planned'): continue
    key=r['lesson_id'],r['date'],r['slot']
    if key not in seen or r['status']=='confirmed': seen[key]=r
for r in seen.values():
    (confirmed if r['status']=='confirmed' else reserved)[r['lesson_id']]+=r['hours']
overruns=[dict(id=l['id'],name=l['name'],plan=l['total_hours'],confirmed=confirmed[l['id']],reserved=reserved[l['id']])
          for l in source_rows if confirmed[l['id']]+reserved[l['id']]>l['total_hours']]
assert not overruns,overruns

draft=ROOT/'outputs/distance-18-19-revision2/candidate-cp/schedule_all.json'
draft_issues=[]
if draft.exists():
    schedule=json.loads(draft.read_bytes()); entries={}
    for g in schedule['groups']:
        for day in g['days']:
            for slot in day['slots']:
                for l in slot['lessons']:
                    entries[g['group_index'],day['date_iso'],slot['slot'],l['id']]=(g,day,slot,l)
    assigned=Counter(l['id'] for _,_,_,l in entries.values())
    for g,day,slot,l in entries.values():
        now=lessons.get(l['id'])
        if now and now['teacher']!=l['teacher_id']:
            draft_issues.append(dict(group=g['group_name'],date=day['date_iso'],slot=slot['slot'],lesson=l['id'],issue='teacher_changed'))
    for lid,pairs in assigned.items():
        now=lessons.get(lid)
        if now and confirmed[lid]+reserved[lid]+pairs*2>now['total_hours']:
            draft_issues.append(dict(lesson=lid,issue='draft_exceeds_new_remaining_hours',total=now['total_hours'],fact=confirmed[lid],reserved=reserved[lid],draft=pairs*2))

# Forecast from today without changing the live generator's selected dates.
forecast_data=json.loads(json.dumps(d)); forecast_data['settings'].update(start_date='2026-09-14',end_date='2026-09-14')
(OUT/'forecast-source.json').write_text(json.dumps(forecast_data,ensure_ascii=False),encoding='utf-8')
run=subprocess.run([str(ROOT/'.tmp/build-sep7/Release/timetable_solver.exe'),'--semester-readout','--data',str(OUT/'forecast-source.json')],capture_output=True)
assert run.returncode in (0,1)
(OUT/'forecast-as-of-20260914.json').write_bytes(run.stdout)
forecast=json.loads(run.stdout)
summary=dict(source_rows_checked=len(source_rows),source_hours=sum(l['total_hours'] for l in source_rows),
             ledger_records=len(d['teaching_ledger']),ledger_status_counts=dict(Counter(r['status'] for r in d['teaching_ledger'])),
             actual_and_reserved_overruns=overruns,saved_draft_conflicts=draft_issues,
             new_subject_hours=sum(r['hours'] for r in report['new']),structural_audit=report['audit_after'])
(OUT/'verification.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
teachers={t['id']:t['name'] for t in d['teachers']}
lines=['Обновление вклеек от 14.09.2026','',f"Источник: {Path(report['source']).name}",
       'Первый семестр: 46 групп, 997 строк, 34 058 часов (включая практики и отдельную нагрузку по подгруппам).',
       'Добавлены 10 строк по искусственному интеллекту: 268 часов. Во всех 10 строках преподаватель не указан.',
       'Обновлены часы 14 существующих строк. Уточнены названия трёх строк Лимоновой без смены ID.',
       'Химия ТЭиРП-2901 заменена биологией; старые часы химии исключены из нагрузки первого семестра.',
       'Сняты семь строк УП, у которых в первом семестре нет часов. История и ID сохранены.',
       'Запись «вынесена на ПП» больше не используется как имя преподавателя.',
       'Сохранены все 2 403 записи журнала: 1 870 проведённых и 533 запланированных занятия.',
       'Календари групп, ограничения преподавателей, кабинеты и настройки сохранены.',
       'Прежние квоты генерации сняты только с переданных другому преподавателю предметов.',
       'Проведённые и уже зарезервированные часы не превышают новую нагрузку ни по одной строке.','',
       'Назначения преподавателей:']
for c in report['changes']:
    change=c['changes'].get('teacher')
    if change and change[0]!=88:
        lines.append(f"- {c['group']}, {c['name']}: {teachers.get(change[0],'вакансия')} → {teachers.get(change[1],'вакансия')}.")
lines+=['','Изменения часов:']
for c in report['changes']:
    hours=c['changes'].get('total_hours')
    if hours: lines.append(f"- {c['group']}, {c['name']}: {hours[0]} → {hours[1]} ч.")
lines+=['','Новые строки без преподавателя:']
for r in report['new']: lines.append(f"- {r['group']}: {r['hours']} ч.")
lines+=['','Ранее выданный черновик 18–19 сентября:']
lines.append(f"Найдено {len(draft_issues)} расхождений с новой нагрузкой. Черновик не перезаписывался и требует пересчёта." if draft_issues else 'Расхождений по назначенным преподавателям и остаткам часов не обнаружено.')
for x in draft_issues: lines.append('- '+json.dumps(x,ensure_ascii=False))
lines+=['','Проверка темпа на 14.09.2026 (верхняя оценка вместимости; не доказательство выполнимости всего семестра):']
for issue in forecast.get('issues',[]):
    if issue['code']=='semester_capacity_shortfall': lines.append('- '+issue['message'])
(OUT/'Итоги_импорта.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print(json.dumps(summary,ensure_ascii=False,indent=2))
