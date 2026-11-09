"""Dated substitutions and room calendars participate in real CP-SAT generation."""
import copy
import hashlib
import http.cookiejar
import json
import os
import subprocess
import sys
import tempfile
import socket
import time
import urllib.request
import urllib.error
from pathlib import Path

def days(monday, tuesday):
    return [dict(day=i, enabled=bool(slots), slots=slots, start_slot=1, end_slot=7)
            for i, slots in [(1,monday),(2,tuesday),*[(d,[]) for d in range(3,8)]]]

def run(solver):
    base=dict(settings=dict(start_date='2026-11-09',end_date='2026-11-10',semester_start_date='2026-11-09',semester_end_date='2026-11-10',automatic_period_quotas=False,enforce_semester_readout=True,
              solver_config=dict(week_time_limit_seconds=5,quality_improvement_seconds=0,solver_workers=2,max_student_pairs_per_day=4,max_whole_group_same_subject_pairs_per_day=1)),
        groups=[dict(id=0,name='TEST-301',parts=1,class_hour_enabled=False,work_days=days([1,2,3,4],[1,2,3,4]))],
        teachers=[dict(id=0,name='Original',work_days=days([1,2],[1,2,3,4])),dict(id=1,name='Substitute',work_days=days([3,4],[]))],
        rooms=[dict(id=0,name='101',campus=0,active=True,work_days=days([1,2],[1,2,3,4])),dict(id=1,name='102',campus=0,active=True,work_days=days([3,4],[]))],
        lessons=[dict(id=n,group=0,subgroup=-1,teacher=0,name=f'Subject {n}',subject_id=n,total_slots=2,total_hours=4,allowed_campuses=[0],teacher_replacements=[]) for n in range(4)],teaching_ledger=[])
    for lesson in base['lessons']:
        lesson['teacher_replacements']=[{'from':'2026-11-09','to':'2026-11-09','teacher':1}] if lesson['id']<2 else []
    env=dict(os.environ);env.pop('TIMETABLE_DATABASE_URL',None)
    with tempfile.TemporaryDirectory(prefix='replacement-generation-') as tmp:
        folder=Path(tmp);(folder/'data').mkdir()
        def generate(data, suffix):
            (folder/'data/timetable_data.json').write_text(json.dumps(data),encoding='utf-8')
            result=subprocess.run([str(solver),'--generate','--output',suffix],cwd=folder,env=env,capture_output=True,timeout=40)
            return result
        result=generate(base,'valid')
        assert result.returncode==0,(result.stdout[-3500:],result.stderr[-2000:])
        schedule=json.loads((folder/'valid/schedule_all.json').read_text(encoding='utf-8'))
        events=[(d['date_iso'],s['slot'],l) for g in schedule['groups'] for d in g['days'] for s in d['slots'] for l in s['lessons'] if s['slot']>0]
        assert len(events)==8,len(events)
        for date,slot,lesson in events:
            replacement=date=='2026-11-09' and lesson['id']<2
            assert lesson['teacher_id']==(1 if replacement else 0),(date,slot,lesson)
            assert lesson.get('is_substitution')==replacement
            assert lesson.get('workload_teacher_id')==0
            if date=='2026-11-09':
                assert slot in ([3,4] if replacement else [1,2]),(date,slot,lesson)
                assert lesson['room_id']==(1 if replacement else 0)
            else:
                assert lesson['room_id']==0,'Closed room used on Tuesday'
        assert json.loads((folder/'valid/strict_audit.json').read_text(encoding='utf-8'))['ok']
        print('Period boundaries, actual teacher availability, room weekdays and strict audit: passed')
        closed=copy.deepcopy(base)
        closed['rooms'][1]['work_days']=days([],[])
        assert generate(closed,'room-closed').returncode!=0,'Generated without an available room'
        override=copy.deepcopy(base)
        override['rooms'][1]['date_slot_overrides']=[{'date':'2026-11-09','slots':[]}]
        assert generate(override,'room-date-closed').returncode!=0,'Ignored exact-date room closure'
        unavailable=copy.deepcopy(base)
        unavailable['teacher_unavailable']=[dict(id=0,teacher=1,**{'from':'2026-11-09','to':'2026-11-09'})]
        assert generate(unavailable,'substitute-absent').returncode!=0,'Ignored substitute unavailability'
        conflict=copy.deepcopy(base)
        conflict['groups'].append(dict(id=1,name='OTHER-301',parts=1,class_hour_enabled=False,work_days=days([3,4],[])))
        conflict['rooms'].append(dict(id=2,name='103',campus=0,active=True))
        conflict['lessons'].extend(dict(id=10+n,group=1,subgroup=-1,teacher=1,name=f'Own subject {n}',subject_id=10+n,total_slots=1,total_hours=2,allowed_campuses=[0]) for n in range(2))
        assert generate(conflict,'substitute-conflict').returncode!=0,'Double-booked substitute in two groups'
        invalid=copy.deepcopy(base)
        invalid['lessons'][0]['teacher_replacements']*=2
        assert generate(invalid,'overlap').returncode!=0,'Accepted overlapping period replacements'
        up=copy.deepcopy(base)
        up['settings']['end_date']=up['settings']['semester_end_date']='2026-11-09'
        up['teachers'][0]['work_days']=days([],[])
        up['teachers'][1]['work_days']=days([1,2,3,4,5,6,7],[])
        up['groups'][0]['work_days']=days([1,2,3,4,5,6,7],[])
        up['rooms'][0]['work_days']=days([1,2,3,4,5,6,7],[])
        up['lessons']=[dict(id=0,group=0,subgroup=-1,teacher=0,name='УП.01',subject_id=0,is_block=True,total_slots=1,total_hours=6,allowed_campuses=[0],teacher_replacements=[{'from':'2026-11-09','to':'2026-11-09','teacher':1}])]
        result=generate(up,'up-replacement')
        assert result.returncode==0,(result.stdout[-2500:],result.stderr[-1000:])
        up_schedule=json.loads((folder/'up-replacement/schedule_all.json').read_text(encoding='utf-8'))
        up_events=[l for g in up_schedule['groups'] for d in g['days'] for s in d['slots'] for l in s['lessons'] if s['slot']>0]
        assert up_events and all(l['teacher_id']==1 and l['is_block'] for l in up_events)
        assert json.loads((folder/'up-replacement/strict_audit.json').read_text(encoding='utf-8'))['ok']
        print('Six-hour UP uses replacement teacher and passes strict audit')
        print('Closed room, unavailable substitute and conflicting periods rejected: passed')
        for mode in ['weekly','day','monolithic']:
            api_check(solver,base,folder/mode,mode)

def api_check(solver,base,folder,mode):
    folder.mkdir();(folder/'data').mkdir()
    source=copy.deepcopy(base)
    for lesson in source['lessons']:lesson['teacher_replacements']=[]
    (folder/'data/timetable_data.json').write_text(json.dumps(source),encoding='utf-8')
    salt=bytes.fromhex('00112233445566778899aabbccddeeff');password='isolated-period-test'
    (folder/'data/auth_config.json').write_text(json.dumps(dict(username='test',password_salt=salt.hex(),password_hash=hashlib.pbkdf2_hmac('sha256',password.encode(),salt,10000).hex(),iterations=10000)),encoding='utf-8')
    with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}),urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
    def api(path,body=None,method=None):
        req=urllib.request.Request(f'http://127.0.0.1:{port}/api/'+path,data=None if body is None else json.dumps(body).encode(),headers={'Content-Type':'application/json'},method=method)
        with opener.open(req,timeout=15) as response:return json.load(response)
    env=dict(os.environ,TIMETABLE_HOST='127.0.0.1');env.pop('TIMETABLE_DATABASE_URL',None)
    with (folder/'server.log').open('w') as log:
        process=subprocess.Popen([str(solver),str(port)],cwd=folder,env=env,stdout=log,stderr=log)
        try:
            for i in range(100):
                try:api('auth/status');break
                except OSError:time.sleep(.05)
            api('auth/login',dict(username='test',password=password))
            for id in [0,1]:
                api(f'lessons/{id}',{'teacher_replacements':base['lessons'][id]['teacher_replacements']},'PATCH')
            try:
                api('lessons/0',{'teacher_replacements':base['lessons'][0]['teacher_replacements']*2},'PATCH')
                raise AssertionError('API accepted overlapping substitutions')
            except urllib.error.HTTPError as error:assert error.code==400,error.code
            persisted=json.loads((folder/'data/timetable_data.json').read_text(encoding='utf-8-sig'))
            assert persisted['lessons'][0]['teacher']==0
            assert len(persisted['lessons'][0]['teacher_replacements'])==1
            body=dict(mode='weekly' if mode=='day' else mode,lock_existing='none')
            if mode in ['weekly','day']:body.update(scope_from='2026-11-09',scope_to='2026-11-09' if mode=='day' else '2026-11-10')
            result=api('schedule/regenerate',body)
            if mode in ['weekly','day']:
                for i in range(300):
                    progress=api('schedule/progress')
                    if progress['state'] in ['done','failed','cancelled']:break
                    time.sleep(.05)
                assert progress['state']=='done',progress
            else:assert result.get('success'),result
            schedule=api('schedule')
            events=[(d['date_iso'],l) for g in schedule['groups'] for d in g['days'] for s in d['slots'] for l in s['lessons'] if s['slot']>0]
            assert events
            for date,lesson in events:
                assert lesson['teacher_id']==(1 if date=='2026-11-09' and lesson['id']<2 else 0),(date,lesson)
            assert json.loads((folder/'output/latest/strict_audit.json').read_text(encoding='utf-8'))['ok']
            occupancy=api('accounting/teacher-occupancy')
            assert any(e['teacher_id']==1 and e['is_substitution'] for e in occupancy['entries']),occupancy
            hours=api('hours')
            assert all(t.get('credited_hours',0)==0 for t in hours['teachers']),'Generation must not credit future lessons'
            assert not json.loads((folder/'data/timetable_data.json').read_text(encoding='utf-8-sig')).get('teaching_ledger')
            api('schedule/manual/copy-from-auto',{})
            api('schedule/manual/complete-day',{'date':'2026-11-09','completed':True},'PUT')
            ledger=json.loads((folder/'data/timetable_data.json').read_text(encoding='utf-8-sig'))['teaching_ledger']
            assert any(e.get('actual_teacher')==1 and e['lesson_id']<2 for e in ledger)
            credited=api('hours')['teachers']
            assert next(t for t in credited if t['teacher_id']==0)['credited_hours']==8,credited
            assert next(t for t in credited if t['teacher_id']==1)['credited_hours']==0,credited
            print(f'{mode}: API persistence, generation, strict audit and original teacher credit passed')
        finally:
            process.terminate();process.wait(timeout=5)

if __name__=='__main__':run(Path(sys.argv[1]).resolve())
