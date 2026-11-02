"""Date-action API regression, isolated files/auth and no external database."""
import copy
import hashlib
import http.cookiejar
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request


def run(solver):
    first, second, third, empty = '2026-11-04', '2026-11-05', '2026-11-06', '2026-11-07'
    data = dict(settings=dict(semester_start_date='2026-09-01', semester_end_date='2026-12-19',
                start_date='2026-11-02', end_date='2026-11-07', automatic_period_quotas=False),
                groups=[dict(id=0, name='TEST-201', parts=1, class_hour_enabled=False)],
                teachers=[dict(id=0, name='Teacher')], rooms=[dict(id=0, name='101', campus=0, active=True)],
                lessons=[dict(id=i, name=f'Subject {i}', group=0, teacher=0, subgroup=-1,
                    total_hours=36, total_slots=1, is_block=i == 1, allowed_campuses=[0]) for i in range(3)],
                teaching_ledger=[dict(id=1, lesson_id=0, group_id=0, date=first, slot=1, hours=2,
                    status='confirmed', source_file='completed.xlsx', room='101'),
                    dict(id=2, lesson_id=2, group_id=0, date=second, slot=1, hours=2,
                    status='planned', source_file='plan.xlsx', source_cell='D18', room='101')])
    def event(i):
        return dict(id=i, name=f'Subject {i}', teacher_id=0, room_id=0, room_name='101', is_block=i == 1)
    class_slot = dict(slot=0, text='Class hour', lessons=[dict(id=-1, name='Class hour', is_class_hour=True)])
    snapshot = dict(groups=[dict(group_index=0, group_name='TEST-201', days=[
        dict(date_iso=first, date='04.11.2026', weekday='СР', slots=[copy.deepcopy(class_slot),
             dict(slot=1, text='Imported fact | Proposed partner', lessons=[event(0),event(2)]), dict(slot=2, text='UP proposal', lessons=[event(1)])]),
        dict(date_iso=second, date='05.11.2026', weekday='ЧТ', slots=[copy.deepcopy(class_slot), dict(slot=1, text='Proposal', lessons=[event(2)])]),
        dict(date_iso=third, date='06.11.2026', weekday='ПТ', slots=[dict(slot=1, text='Outside', lessons=[event(0)])]),
    ])])
    with tempfile.TemporaryDirectory(prefix='schedule-day-actions-') as directory:
        root = Path(directory)
        (root/'data').mkdir()
        (root/'data/timetable_data.json').write_text(json.dumps(data), encoding='utf-8')
        password, salt = 'isolated-day-actions', '00112233445566778899aabbccddeeff'
        (root/'data/auth_config.json').write_text(json.dumps(dict(username='test',password_salt=salt,
            iterations=150000,password_hash=hashlib.pbkdf2_hmac('sha256', password.encode(),bytes.fromhex(salt),150000).hex())), encoding='utf-8')
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
        def api(route, body=None, method=None):
            req=urllib.request.Request(f'http://127.0.0.1:{port}/api/{route}',data=json.dumps(body).encode() if body is not None else None,
                headers={'Content-Type':'application/json'}, method=method)
            with opener.open(req,timeout=15) as response:return json.load(response)
        def rejected(body, status):
            try: api('schedule/days',body)
            except urllib.error.HTTPError as error: assert error.code==status, error
            else: raise AssertionError('Action unexpectedly succeeded')
        def restore(schedule):
            current=api('data')
            return api('transfer/import', dict(bundle=dict(format='raspis-transfer-bundle',schema_version=1,data=current,
                schedules=dict(auto=schedule,manual=copy.deepcopy(snapshot),published=copy.deepcopy(schedule))),primary_schedule='auto',publish=False))
        def day(schedule,date): return next(d for d in schedule['groups'][0]['days'] if d['date_iso']==date)
        environment=dict(os.environ);environment.pop('TIMETABLE_DATABASE_URL',None);environment['TIMETABLE_HOST']='127.0.0.1'
        with (root/'server.log').open('w',encoding='utf-8') as log:
            process=subprocess.Popen([str(solver),str(port)],cwd=root,env=environment,stdout=log,stderr=log)
            try:
                for _ in range(80):
                    try: api('auth/status');break
                    except OSError: time.sleep(.1)
                rejected(dict(action='clear',dates=[first]),401)
                api('auth/login',dict(username='test',password=password))
                restore(snapshot)
                before=api('data');manual=api('schedule/manual');original=api('schedule');outside=copy.deepcopy(day(original,third))
                for body in [dict(action='clear',dates=[]),dict(action='oops',dates=[first]),dict(action='clear',dates=['bad-date']),dict(action='clear',dates=[first]*8)]:rejected(body,400)
                rejected(dict(action='confirm',dates=[second,empty]),422)
                assert api('data')==before, 'Batch validation partially credited hours'
                assert api('schedule')==original
                result=api('schedule/days',dict(action='clear',dates=[first]))
                assert result['changed']==3,result  # UP, proposed partner and class hour, not the imported fact.
                for route in ['schedule','schedule/published']:
                    current=api(route)
                    assert [l['id'] for s in day(current,first)['slots'] for l in s['lessons']]==[0]
                    assert day(current,third)==outside
                    assert all(not s['text'] for s in day(current,first)['slots'] if not s['lessons']), 'Stale legacy text survived clear'
                    assert next(s for s in day(current,first)['slots'] if s['slot']==1)['text']=='Subject 0 — Teacher, каб. 101'
                assert api('data')==before and api('schedule/manual')==manual
                assert api('schedule/days',dict(action='clear',dates=[first]))['changed']==0
                result=api('schedule/days',dict(action='confirm',dates=[first,second,second]))
                assert result['changed']==1,result
                ledger=api('data')['teaching_ledger']
                assert len(ledger)==2 and all(e['status']=='confirmed' for e in ledger)
                promoted=next(e for e in ledger if e['date']==second)
                assert promoted['source_file']=='plan.xlsx' and promoted['source_cell']=='D18' and promoted['hours']==2
                assert ledger[0]==before['teaching_ledger'][0], 'Imported fact was overwritten'
                assert api('schedule/days',dict(action='confirm',dates=[second]))['changed']==0
                assert api('schedule/days',dict(action='clear',dates=[second]))['changed']==0
                assert all(l['confirmed'] for s in day(api('schedule/published'),second)['slots'] for l in s['lessons'])
                assert api('schedule/manual')==manual
                already=copy.deepcopy(snapshot);already['groups'][0]['days']=[copy.deepcopy(snapshot['groups'][0]['days'][0])]
                already['groups'][0]['days'][0]['slots']=[copy.deepcopy(class_slot),dict(slot=1,text='Imported fact',lessons=[event(0)])]
                restore(already)
                assert api('schedule/days',dict(action='confirm',dates=[first]))['changed']==0
                assert day(api('schedule'),first)['slots'][0]['lessons'][0]['confirmed']
                assert api('schedule/days',dict(action='clear',dates=[first]))['changed']==0
                assert api('data')['teaching_ledger']==ledger
                # Six-hour UP, followed by clear-week protection and a fresh publication date.
                up=copy.deepcopy(snapshot);up['groups'][0]['days']=[dict(date_iso=third,date='06.11.2026',weekday='ПТ',slots=[copy.deepcopy(class_slot),dict(slot=1,lessons=[event(1)])])]
                restore(up)
                assert api('schedule/days',dict(action='confirm',dates=[third]))['changed']==1
                up_fact=next(e for e in api('data')['teaching_ledger'] if e['date']==third)
                assert up_fact['hours']==6
                assert api('schedule/days',dict(action='clear',dates=[first,second,third,empty]))['changed']==0
                assert sum(r['scheduled_hours'] for r in api('hours')['lessons'])==10
                # Confirmation must also add a day absent from the publication.
                with (root/'output/published/schedule_all.json').open('w') as file:json.dump(dict(groups=[]),file)
                new=copy.deepcopy(up);new['groups'][0]['days'][0]['date_iso']=empty
                restore(new)
                with (root/'output/published/schedule_all.json').open('w') as file:json.dump(dict(groups=[]),file)
                api('schedule/days',dict(action='confirm',dates=[empty]))
                assert day(api('schedule/published'),empty)['slots']
                assert list((root/'output/day_action_backups').glob('*.raspis.json'))
                restore(snapshot)
                changed_data=api('data');changed_data['settings']['site_title']='Changed input'
                api('data',changed_data,method='PUT')
                rejected(dict(action='confirm',dates=[first]),409)
                api('schedule/days',dict(action='clear',dates=[first]))
                try:api('schedule')
                except urllib.error.HTTPError as error:assert error.code==409
                else:raise AssertionError('Clear incorrectly validated unrelated stale generation')
                print('Date actions: scoped clear, protected facts, batch validation, idempotent confirmation, planned promotion, UP=6h, class hours excluded, publication and Constructor preserved')
            except Exception:
                log.flush();print((root/'server.log').read_text(encoding='utf-8')[-4000:]);raise
            finally:process.terminate();process.wait(timeout=10)


if __name__=='__main__': run(Path(sys.argv[1]).resolve())
