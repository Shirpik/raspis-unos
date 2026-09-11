"""End-to-end CP-SAT guard against moving unfinished work after PP.

Run after building: python tests/test_practice_solver.py --solver <exe>
All data and schedules are isolated in a temporary directory.
"""
import argparse
import copy
import json
import subprocess
import tempfile
from pathlib import Path


def run(solver):
    base = {
        'settings': {'start_date': '2026-09-12', 'end_date': '2026-09-12',
                     'semester_start_date': '2026-09-01', 'semester_end_date': '2026-12-19',
                     'automatic_period_quotas': False, 'enforce_semester_readout': True,
                     'solver_config': {'week_time_limit_seconds': 5, 'solver_workers': 2,
                                       'max_student_pairs_per_day': 4}},
        'groups': [{'id': 0, 'name': 'TEST-301', 'parts': 1, 'home_campus': 0,
                    'class_hour_enabled': False,
                    'practice_periods': [{'from': '2026-09-14', 'to': '2026-09-20'}]}],
        'teachers': [{'id': 0, 'name': 'Teacher'}],
        'rooms': [{'id': 0, 'name': '101', 'campus': 0, 'active': True}],
        'lessons': [{'id': n, 'group': 0, 'subgroup': -1, 'teacher': 0,
                     'name': f'Subject {n}', 'subject_id': n, 'total_hours': 2,
                     'total_slots': 1, 'is_lab': False, 'is_block': False, 'is_pp': False,
                     'allowed_campuses': [0]} for n in range(3)],
        'teaching_ledger': [], 'unavailable': [], 'teacher_unavailable': [],
    }
    cases = [('before', '2026-09-12', True), ('during', '2026-09-14', False),
             ('after_return', '2026-09-21', False)]
    with tempfile.TemporaryDirectory(prefix='practice-solver-') as tmp:
        for name, day, expected in cases:
            folder = Path(tmp) / name
            (folder / 'data').mkdir(parents=True)
            data = copy.deepcopy(base)
            data['settings']['start_date'] = data['settings']['end_date'] = day
            (folder / 'data/timetable_data.json').write_text(json.dumps(data), encoding='utf-8')
            result = subprocess.run([str(solver), '--generate', '--draft-semester', '--output', 'result'],
                                    cwd=folder, capture_output=True, encoding='utf-8', timeout=30)
            assert (result.returncode == 0) == expected, (name, result.stdout[-4000:], result.stderr)
            if expected:
                schedule = json.loads((folder / 'result/schedule_all.json').read_text(encoding='utf-8'))
                events = [l for g in schedule['groups'] for d in g['days'] for s in d['slots'] for l in s['lessons'] if s['slot'] > 0]
                assert len(events) == 3
            print(f'{name}: {"scheduled" if expected else "blocked even in draft mode"}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--solver', required=True, type=Path)
    run(parser.parse_args().solver.resolve())
