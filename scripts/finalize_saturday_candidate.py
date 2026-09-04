"""Preserve empty Friday for SP-4612p and keep its quota on Saturday."""
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
candidate=root/'.tmp/saturday-20260904/candidate4/schedule_all.json'
s=json.loads(candidate.read_text(encoding='utf-8-sig'))
moved=None
for g in s['groups']:
    for day in g['days']:
        if day.get('date_iso')!='2026-09-04': continue
        for slot in day['slots']:
            for e in list(slot.get('lessons',[])):
                if e.get('id')==605:
                    slot['lessons'].remove(e); moved=e
if moved is None: raise RuntimeError('lesson 605 not found on Friday')
target=next(g for g in s['groups'] if g['group_index']==29)
sat=next(day for day in target['days'] if day.get('date_iso')=='2026-09-05')
slot7=next(x for x in sat['slots'] if x['slot']==7)
if slot7.get('lessons'): raise RuntimeError('SP-4612p slot 7 is occupied')
moved['room_id']=31; moved['room_name']='205'; moved['campus']=0
slot7['lessons']=[moved]; slot7['text']=moved.get('name','')
candidate.write_text(json.dumps(s,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

# The CPDE rule is date-operational from Saturday. Its generated Saturday
# placements remain in CPDE, while removing the global fixed-room flag avoids
# retroactively changing the approved Friday.
dp=root/'data/timetable_data.json'; d=json.loads(dp.read_text(encoding='utf-8-sig'))
pod=next(t['id'] for t in d['teachers'] if 'Подчинен' in t['name'])
for lesson in d['lessons']:
    if lesson.get('teacher')==pod and 'лпз' in lesson.get('name','').casefold():
        lesson['fixed_room']=-1; lesson['preferred_room']=68
dp.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'moved_lesson':605,'to':'2026-09-05 slot 7','room_id':31}))
