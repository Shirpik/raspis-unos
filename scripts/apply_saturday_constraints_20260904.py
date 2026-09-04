"""Apply the dispatcher constraints for Saturday 2026-09-05 by stable names."""
import json, shutil, datetime as dt, re
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]; DATA=ROOT/'data/timetable_data.json'; HISTORY=ROOT/'data/history'
def n(v): return re.sub(r'\s+',' ',str(v or '')).strip().casefold().replace('ё','е')

d=json.loads(DATA.read_text(encoding='utf-8-sig'))
tby={n(x['name']):x for x in d['teachers']}; rby={n(x['name']):x for x in d['rooms']}
def teacher(fragment):
    found=[x for key,x in tby.items() if n(fragment) in key]
    if len(found)!=1: raise RuntimeError(f'{fragment}: найдено {len(found)}')
    return found[0]
def set_override(t,date,slots):
    values=[x for x in t.get('date_slot_overrides',[]) if x.get('date')!=date]
    values.append({'date':date,'slots':slots}); t['date_slot_overrides']=sorted(values,key=lambda x:x['date'])
def add_unavailable(t,from_date,to_date,text):
    d['teacher_unavailable']=[x for x in d.get('teacher_unavailable',[]) if not(x.get('teacher')==t['id'] and x.get('from_date')==from_date and x.get('to_date')==to_date)]
    d['teacher_unavailable'].append({'id':max([x.get('id',-1) for x in d.get('teacher_unavailable',[])],default=-1)+1,
       'teacher':t['id'],'from_date':from_date,'to_date':to_date,'text':text})

pod=teacher('Подчинен'); khan=teacher('Ханьжина'); sem=teacher('Семенова Лилиана'); kol=teacher('Колтышев'); pis=teacher('Письмак'); kor=teacher('Коробкова'); mih=teacher('Михайлова')
cpde=rby[n('ЦПДЭ')]
cpde['responsible_teacher_ids']=sorted(set(cpde.get('responsible_teacher_ids',[])+[pod['id']]))
changed=0
for lesson in d['lessons']:
    name=n(lesson.get('name'))
    if lesson.get('teacher')==pod['id'] and 'лпз' in name:
        lesson.update({'is_lab':True,'allowed_campuses':[0],'fixed_room':cpde['id'],'preferred_room':cpde['id'],'allow_room_substitution':False})
        changed+=1
# New workload may transfer an existing CPDE practical row to another teacher.
# Access follows the fixed lesson, so those teachers must remain eligible too.
cpde['responsible_teacher_ids']=sorted(set(cpde.get('responsible_teacher_ids',[])+[
    x.get('teacher',-1) for x in d['lessons'] if x.get('fixed_room')==cpde['id'] and x.get('teacher',-1)>=0
]))
set_override(sem,'2026-09-05',[1,2,3,4]); set_override(kor,'2026-09-04',[1,2,3,4]); set_override(kor,'2026-09-05',[])
set_override(kol,'2026-09-05',[]); set_override(pis,'2026-09-05',[]); set_override(mih,'2026-09-05',[])
set_override(khan,'2026-09-07',[5,6,7])
# Dispatcher confirmed that Saturday is a make-up workday: allow the full
# seven-pair grid so the teacher can read out the maximum possible load.
for day in khan.get('work_days',[]):
    if day.get('day')==6: day.update({'enabled':True,'start_slot':1,'end_slot':7,'slots':[1,2,3,4,5,6,7]})
add_unavailable(khan,'2026-09-08','2026-09-18','Командировка')
d.setdefault('settings',{})['hard_no_student_windows']=True
HISTORY.mkdir(parents=True,exist_ok=True); backup=HISTORY/f'before_saturday_constraints_{dt.datetime.now():%Y%m%d_%H%M%S}.json'; shutil.copy2(DATA,backup)
DATA.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'ok':True,'backup':str(backup),'podchinenov_lpz_cpde':changed,'khanzhina_sep7_slots':[5,6,7],'khanzhina_trip':'2026-09-08..2026-09-18'},ensure_ascii=False,indent=2))
