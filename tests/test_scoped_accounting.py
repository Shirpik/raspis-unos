"""API/CP-SAT regression: journal and schedule reservations share one balance.

Run: python tests/test_scoped_accounting.py build/Release/timetable_solver.exe
Uses an isolated temporary database and a future date; never changes real schedules.
"""
import hashlib
import http.cookiejar
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
import urllib.error
from datetime import date, timedelta
from pathlib import Path


def run(solver, imported=False, span=1):
    target = date.today() + timedelta(days=7 - date.today().weekday())
    prior = target - timedelta(days=2)
    iso = lambda day: day.isoformat()
    settings = dict(start_date=iso(target), end_date=iso(target),
                    semester_start_date=iso(target - timedelta(days=7)),
                    semester_end_date=iso(target + timedelta(days=35)),
                    automatic_period_quotas=False, enforce_semester_readout=False,
                    solver_config=dict(week_time_limit_seconds=5,
                                       quality_improvement_seconds=0, solver_workers=2))
    lessons = [dict(id=n, group=0, teacher=0, subgroup=-1, name=f'Subject {n}',
                    subject_id=n, total_hours=2*(span+1), total_slots=1, allowed_campuses=[0])
               for n in range(3)]
    ledger = [dict(id=2*n+i, lesson_id=n, date=iso(day), slot=n+1,
                   hours=2, status='planned')
              for n in range(3) for i, day in enumerate((prior, target))]
    data = dict(settings=settings, groups=[dict(id=0, name='TEST-201', parts=1,
                home_campus=0, class_hour_enabled=False)],
                teachers=[dict(id=0, name='Teacher')],
                rooms=[dict(id=0, name='101', campus=0, active=True)],
                lessons=lessons, teaching_ledger=ledger)
    previous_day = dict(date_iso=iso(prior), slots=[dict(slot=n+1, lessons=[dict(id=n)])
                                                 for n in range(3)])
    if imported:
        for entry in ledger:
            if entry['date'] == iso(prior):
                entry.update(status='confirmed', source_file='completed-week.xlsx', group_id=0)
        # A stale generated snapshot has the same group/day but different
        # placements. The imported completed day must supersede it as a whole.
        for slot in previous_day['slots']:
            slot['lessons'][0]['id'] = (slot['lessons'][0]['id'] + 1) % 3
    with tempfile.TemporaryDirectory(prefix='scoped-accounting-') as tmp:
        folder = Path(tmp)
        (folder/'data').mkdir()
        (folder/'output/latest').mkdir(parents=True)
        (folder/'data/timetable_data.json').write_text(json.dumps(data), encoding='utf-8')
        salt = '00112233445566778899aabbccddeeff'
        password = 'isolated-accounting-test'
        auth = dict(username='test', password_salt=salt, iterations=150000,
                    password_hash=hashlib.pbkdf2_hmac('sha256', password.encode(),
                                  bytes.fromhex(salt), 150000).hex())
        (folder/'data/auth_config.json').write_text(json.dumps(auth), encoding='utf-8')
        (folder/'output/latest/schedule_all.json').write_text(json.dumps(dict(groups=[
            dict(group_index=0, days=[previous_day])])), encoding='utf-8')
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}),
                                            urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))

        def api(path, body=None):
            request = urllib.request.Request(f'http://127.0.0.1:{port}/api/{path}',
                      data=json.dumps(body).encode() if body is not None else None,
                      headers={'Content-Type': 'application/json'})
            with opener.open(request, timeout=10) as response:
                return json.load(response)

        with (folder/'server.log').open('w', encoding='utf-8') as log:
            environment = dict(os.environ)
            environment.pop('TIMETABLE_DATABASE_URL', None)
            environment['TIMETABLE_HOST'] = '127.0.0.1'
            process = subprocess.Popen([str(solver), str(port)], cwd=folder, stdout=log, stderr=log, env=environment)
            try:
                for attempt in range(80):
                    try:
                        api('auth/status')
                        break
                    except OSError:
                        if process.poll() is not None:
                            raise AssertionError('Test API exited during startup')
                        time.sleep(0.1)
                api('auth/login', dict(username='test', password=password))
                normalized = json.loads((folder/'data/timetable_data.json').read_text(encoding='utf-8-sig'))
                for invalid in (dict(scope_to=iso(target)), dict(mode='typo'),
                                dict(scope_from='', scope_to=''),
                                dict(scope_from=iso(target), scope_to=iso(prior))):
                    try:
                        api('schedule/regenerate', invalid)
                        raise AssertionError(f'Invalid scope accepted: {invalid}')
                    except urllib.error.HTTPError as error:
                        assert error.code in (400, 422), error
                last = target + timedelta(days=span-1)
                assert api('schedule/regenerate', dict(mode='weekly', scope_from=iso(target),
                           scope_to=iso(last), lock_existing='none'))['async']
                for attempt in range(200):
                    progress = api('schedule/progress')
                    if progress['state'] in ('done', 'failed', 'cancelled'):
                        break
                    time.sleep(0.1)
                assert progress['state'] == 'done', progress
                current = json.loads((folder/'output/latest/schedule_all.json').read_text(encoding='utf-8'))
                days = current['groups'][0]['days']
                assert {day['date_iso'] for day in days} == {iso(target+timedelta(days=i)) for i in range(span)}, days
                assert current['scope_from'] == iso(target) and current['scope_to'] == iso(last)
                assert json.loads((folder/'output/latest/strict_audit.json').read_text())['ok']
                events = [lesson for day in days
                          for slot in day['slots'] for lesson in slot['lessons']
                          if not lesson.get('is_class_hour')]
                if span == 1:
                    assert sorted(lesson['id'] for lesson in events) == [0, 1, 2], events
                else:
                    assert len(events) >= 2*span and all(e['room_id'] is not None for e in events), events
                saved = json.loads((folder/'data/timetable_data.json').read_text(encoding='utf-8-sig'))
                assert saved['teaching_ledger'] == normalized['teaching_ledger'], 'Generation modified the teaching ledger'
                report = api('hours')
                for row in report['lessons'] if span == 1 else []:
                    assert row['scheduled_hours'] == (2 if imported else 0), row
                    assert row['reserved_hours'] == 2*(span if imported else span+1) and row['available_hours'] == 0, row
                print('Scoped accounting: reservations deduplicate, selected plans release, '
                      'CP-SAT assigns 3 pairs, exact output dates and journal preserved')
            except Exception:
                log.flush()
                print((folder/'server.log').read_text(encoding='utf-8')[-5000:])
                raise
            finally:
                process.terminate()
                process.wait(timeout=10)


if __name__ == '__main__':
    run(Path(sys.argv[1]).resolve())
    run(Path(sys.argv[1]).resolve(), imported=True)
    run(Path(sys.argv[1]).resolve(), span=2)
