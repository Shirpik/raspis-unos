"""Independent checks of the final assigned Saturday timetable."""
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path
from prepare_one_week_generation import lesson_parts

folder=Path(sys.argv[1])
candidate=folder/sys.argv[2]
data=json.loads((folder/'data/timetable_data.json').read_text(encoding='utf-8'))
schedule=json.loads((candidate/'schedule_all.json').read_text(encoding='utf-8'))
teachers={t['id']:t for t in data['teachers']}
groups={g['id']:g for g in data['groups']}
lessons={l['id']:l for l in data['lessons']}
rooms={r['id']:r for r in data['rooms']}
teacher_slots=defaultdict(list); part_slots=defaultdict(list); room_slots=set(); counts=Counter(); lesson_slots=defaultdict(list)
subject_counts=Counter(); subject_teachers=defaultdict(set)
for g in schedule['groups']:
    for day in g['days']:
        assert day['date_iso']=='2026-09-12'
        for slot in day['slots']:
            for event in slot['lessons']:
                l=lessons[event['id']]; s=slot['slot']; tid=l['teacher']
                teacher_slots[tid].append(s); counts[l['id']]+=1; lesson_slots[l['id']].append(s)
                for gid,part in lesson_parts(l,groups):
                    part_slots[(gid,part)].append(s)
                    subject_key=(gid,part,l['subject_id'])
                    subject_counts[subject_key]+=1;subject_teachers[subject_key].add(tid)
                rid=event['room_id']; assert rid in rooms
                assert (rid,s) not in room_slots, ('room collision',rid,s)
                room_slots.add((rid,s))
                if teachers[tid]['name'].startswith('Круглова '):
                    assert rooms[rid]['campus']==1 and 'computer' in rooms[rid].get('equipment',[])
for tid,slots in teacher_slots.items():
    assert len(slots)==len(set(slots)), ('teacher collision',tid)
    assert max(slots)-min(slots)+1==len(slots),('teacher window',tid,slots)
    assert len(slots)<=(teachers[tid].get('max_pairs_per_day') or 7)
for part,slots in part_slots.items():
    assert len(slots)==4,('student day',part,slots)
    assert len(slots)==len(set(slots))
    assert max(slots)-min(slots)+1==len(slots),('student window',part,slots)
for key,count in subject_counts.items():
    assert count <= (4 if subject_teachers[key]=={51} else 3),('subject limit exception leaked',key,count)
for lid,slots in lesson_slots.items():
    l=lessons[lid]
    if l.get('consecutive_pairs')==2:
        assert len(slots)%2==0
        ss=sorted(slots)
        for i in range(0,len(ss),2):
            assert ss[i+1]==ss[i]+1
            if l.get('block_start_slots'):assert ss[i]-1 in l['block_start_slots']
seen=set(); credited=Counter()
for e in data.get('teaching_ledger',[]):
    k=(e.get('lesson_id'),e.get('date'),e.get('slot'))
    if e.get('status')=='confirmed' and e['date']<'2026-09-12' and k not in seen:
        seen.add(k);credited[e['lesson_id']]+=e['hours']
for lid,count in counts.items():assert count*2<=lessons[lid]['total_hours']-credited[lid],('hours exceeded',lid)
for tid,t in teachers.items():
    if t['name'].startswith(('Серянина ','Письмак ','Тарасов ','Усков ','Ханьжина ','Круглова ')):assert tid not in teacher_slots
for surname,minimum in {'Рабенок Мария':4,'Тимеров':6,'Вальдиянов':7,'Гарбузов':7,'Меренчуков':7,'Сивилькаев':6,'Рахматулина':5}.items():
    tid=next(tid for tid,t in teachers.items() if t['name'].startswith(surname))
    assert len(teacher_slots[tid])>=minimum,(surname,teacher_slots[tid])
assert len(teacher_slots[10])==4
assert len(teacher_slots[9])==2
assert len(teacher_slots[51])==7
assert len(teacher_slots[40])==7
assert len(teacher_slots[16])==7
assert len(teacher_slots[21])==7
assert len(teacher_slots[12])==6
rabenok_groups=[lessons[lid]['group'] for lid,count in counts.items() if lessons[lid]['teacher']==12 for _ in range(count)]
assert len(rabenok_groups)==len(set(rabenok_groups))==6,('Rabenok repeated group',rabenok_groups)
assert all(groups[lessons[lid]['group']]['name'].split('-')[-1].startswith('3') for lid in counts if lessons[lid]['teacher']==60)
assert sum(count for lid,count in counts.items() if lessons[lid]['teacher']==62 and groups[lessons[lid]['group']]['name'].split('-')[-1].startswith('3'))>=4
curators={g.get('curator_teacher') for g in groups.values()}
assert len(part_slots)==sum(g['parts'] for g in groups.values()),'missing student subgroup'
exceptions={83,84,86,87}
assert all(tid in teacher_slots for tid in curators-exceptions), ('missing curator',curators-exceptions-set(teacher_slots))
audit=json.loads((candidate/'strict_audit.json').read_text(encoding='utf-8'))
assert audit['ok'] and audit['summary']['hard_errors']==0 and audit['summary']['unassigned_rooms']==0
report={'ok':True,'date':'2026-09-12','pairs':sum(counts.values()),'academic_hours':2*sum(counts.values()),'teachers':len(teacher_slots),'groups':len(schedule['groups']),
        'curators_with_lessons':len(curators-exceptions),'student_day_distribution':dict(Counter(len(s) for s in part_slots.values())),
        'teacher_load':[{'id':tid,'name':teachers[tid]['name'],'pairs':len(slots),'slots':sorted(slots)} for tid,slots in sorted(teacher_slots.items())],
        'semester_status':schedule['status'],'checks':['no teacher/student windows','student maximum four per subgroup','unique rooms','remaining hours','paired labs','priority load','curators present','explicit absences']}
(folder/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='teacher_load'},ensure_ascii=False))
