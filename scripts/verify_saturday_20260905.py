import json
from collections import defaultdict, Counter
from pathlib import Path
root=Path(__file__).resolve().parents[1]
d=json.loads((root/'data/timetable_data.json').read_text(encoding='utf-8-sig'))
s=json.loads((root/'.tmp/saturday-20260904/final.json').read_text(encoding='utf-8-sig'))
old=json.loads((root/'.tmp/saturday-20260904/approved-before-regeneration.json').read_text(encoding='utf-8-sig'))
L={x['id']:x for x in d['lessons']}; T={x['id']:x['name'] for x in d['teachers']}; R={x['id']:x for x in d['rooms']}
def occ(schedule,date):
 return [(g['group_index'],sl['slot'],e) for g in schedule['groups'] for day in g['days'] if day.get('date_iso')==date for sl in day['slots'] for e in sl.get('lessons',[])]
errors=[]
sig=lambda x:sorted((g,slot,e['id'],e.get('subgroup',-1),e.get('teacher_id',-1),e.get('room_id',-1)) for g,slot,e in x)
if sig(occ(s,'2026-09-04'))!=sig(occ(old,'2026-09-04')): errors.append('Пятница изменилась')
sat=occ(s,'2026-09-05')
by_part=defaultdict(set)
for gid,slot,e in sat:
 lesson=L[e['id']]; sg=lesson.get('subgroup',-1)
 parts=(0,1) if sg<0 else (sg%2,)
 for part in parts: by_part[(gid,part)].add(slot)
 name=T.get(lesson.get('teacher'),'')
 low=lesson.get('name','').casefold(); room=R.get(e.get('room_id'),{})
 if any(x in name for x in ['Колтышев','Письмак','Михайлова']): errors.append(f'Запрещённый преподаватель: {name}')
 if 'Семенова Лилиана' in name and slot not in [1,2,3,4]: errors.append(f'Семенова на паре {slot}')
 if 'Подчинен' in name and 'лпз' in low and room.get('name')!='ЦПДЭ': errors.append('Подчиненнов ЛПЗ не в ЦПДЭ')
 if 'Кальчевская' in name and 'лпз' in low and room.get('name')!='ЦПДЭ': errors.append('Кальчевская ЛПЗ не в ЦПДЭ')
 if any(x in name for x in ['Лимонова','Самцов']) and 'лпз' not in low:
  if room.get('campus')!=0 or room.get('name')=='210' or room.get('access_mode')!='general': errors.append(f'Теория {name} в неверной аудитории {room.get("name")}')
 if room.get('name')=='210' and 'Азарян' not in name: errors.append(f'210 занята: {name}')
for key,slots in by_part.items():
 if slots and max(slots)-min(slots)+1!=len(slots): errors.append(f'Окно у {key}: {sorted(slots)}')
for lid,x in L.items():
 if x.get('consecutive_pairs')==2:
  positions=sorted(slot for gid,slot,e in sat if e['id']==lid)
  if len(positions)%2 or any(positions[i+1]!=positions[i]+1 or (positions[i]==2 and x.get('avoid_lunch_split')) for i in range(0,len(positions),2)): errors.append(f'Разорван двойной блок {lid}: {positions}')
kh=sum(1 for _,_,e in sat if 'Ханьжина' in T.get(L[e['id']].get('teacher'),''))
print(json.dumps({'ok':not errors,'events':len(occ(s,'2026-09-04'))+len(sat),'friday_events':len(occ(s,'2026-09-04')),'saturday_events':len(sat),'student_windows':0 if not [x for x in errors if x.startswith('Окно')] else None,'khanzhina_saturday_pairs':kh,'errors':errors},ensure_ascii=False,indent=2))
if errors: raise SystemExit(1)
