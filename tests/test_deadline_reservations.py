"""Cross-language regression for dates, remaining hours, and planned reservations."""
import json
import subprocess
import sys
import tempfile
from datetime import date, timedelta
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from teaching_balance import balances, deadline


def run(solver):
    settings = dict(start_date='2026-09-18',end_date='2026-09-19',semester_start_date='2026-09-01',
                    semester_end_date='2026-12-19',first_course_semester_end_date='2026-12-26',automatic_period_quotas=True)
    group = dict(id=0,name='TEST-101',parts=1,class_hour_enabled=False)
    data=dict(settings=settings,groups=[group],teachers=[dict(id=0,name='Teacher')],
              rooms=[dict(id=0,name='101',campus=0,active=True)],
              lessons=[dict(id=0,teacher=0,group=0,subgroup=-1,name='Theory',subject_id=0,total_hours=10,total_slots=4,
                            curriculum_active=True,plan_active=True,generation_active=True,is_lab=False,is_block=False,is_pp=False)],
              teaching_ledger=[dict(lesson_id=0,date='2026-09-14',slot=1,hours=2,status='confirmed'),
                               dict(lesson_id=0,date='2026-09-17',slot=1,hours=2,status='planned'),
                               dict(lesson_id=0,date='2026-09-17',slot=2,hours=2,status='planned'),
                               dict(lesson_id=0,date='2026-09-17',slot=2,hours=2,status='planned'),
                               dict(lesson_id=0,date='2026-09-18',slot=1,hours=2,status='planned')])
    with tempfile.TemporaryDirectory() as folder:
        file=Path(folder)/'data.json'
        for asof,expected_reserve in [('2026-09-18',4),('2026-09-17',0)]:
            settings['start_date']=asof
            file.write_text(json.dumps(data),encoding='utf-8')
            proc=subprocess.run([str(solver),'--semester-readout','--data',str(file)],capture_output=True,encoding='utf-8',timeout=30)
            report=json.loads(proc.stdout)
            row=next(l for l in report['lessons'] if l['lesson_id']==0)
            assert row['confirmed_hours']==2 and row['reserved_hours']==expected_reserve, row
            assert row['remaining_hours']==8-expected_reserve, row
            assert row['deadline']=='2026-12-26',row
            fact,reserved,_=balances(data,date.fromisoformat(asof))
            assert fact[0]==row['confirmed_hours'] and reserved[0]==row['reserved_hours']
            assert deadline(group,settings).isoformat()==row['deadline']
        group['name']='TEST-301'
        group['academic_calendar']=[{'from':(date(2026,8,31)+timedelta(weeks=i)).isoformat(),
                                    'to':(date(2026,9,6)+timedelta(weeks=i)).isoformat(),
                                    'theory_hours':30,'up_hours':6} for i in range(9)] + [
                                   {'from':'2026-11-02','to':'2026-11-08','theory_hours':0,'up_hours':36},
                                   {'from':'2026-11-09','to':'2026-11-15','theory_hours':0,'pp_hours':36}]
        file.write_text(json.dumps(data),encoding='utf-8')
        proc=subprocess.run([str(solver),'--semester-readout','--data',str(file)],capture_output=True,encoding='utf-8',timeout=30)
        assert proc.stdout, proc.stderr
        report=json.loads(proc.stdout)
        assert report['groups'][0]['deadline']=='2026-11-01'
        assert deadline(group,settings).isoformat()=='2026-11-01'
    print('Reservation/cutoff/deduplication/course deadlines/UP-before-PP regressions passed')


if __name__=='__main__':
    run(Path(sys.argv[1]).resolve())
