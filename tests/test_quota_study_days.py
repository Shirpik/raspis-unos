"""The quota witness must respect the timetable's subgroup study days."""
import copy
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def model():
    rows = [dict(id=i, teacher=i, group=0, minimum=1, maximum=1,
                 semester_total=1, parts=[i], part_weight=1,
                 whole_group=False, allowed_slots=[i * 2],
                 allowed_campuses=[0], subject=str(i)) for i in range(2)]
    return dict(variables=rows, parts=[dict(key=i, lesson_ids=[i], target=1)
                                     for i in range(2)],
                teachers=[dict(id=i, minimum=1, maximum=1) for i in range(2)],
                lab_rules=[], day_count=2, slots_per_day=2,
                min_student_pairs_per_day=1, max_student_pairs_per_day=2,
                workers=1, time_limit_seconds=5)


def run(executable, payload):
    with tempfile.TemporaryDirectory(prefix='quota-study-days-') as folder:
        file = Path(folder) / 'model.json'
        file.write_text(json.dumps(payload), encoding='utf-8')
        env = os.environ.copy()
        if os.name == 'nt':
            env['PATH'] = 'C:/or-tools/bin;' + env.get('PATH', '')
        result = subprocess.run([str(executable), str(file)], env=env,
                                capture_output=True, timeout=15)
        return json.loads(result.stdout.decode('utf-8-sig'))


if __name__ == '__main__':
    exe = Path(sys.argv[1]).resolve()
    disjoint = model()
    assert run(exe, disjoint)['status'] == 'INFEASIBLE'
    synced = copy.deepcopy(disjoint)
    synced['variables'][1]['allowed_slots'] = [1]
    assert run(exe, synced)['success']
    synced['require_all_student_days'] = True
    assert run(exe, synced)['status'] == 'INFEASIBLE'
    both_days = copy.deepcopy(synced)
    for row in both_days['variables']:
        row['allowed_slots'] = [row['id'], row['id'] + 2]
        row['minimum'] = row['maximum'] = 2
    for part in both_days['parts']:
        part['target'] = 2
    for teacher in both_days['teachers']:
        teacher['minimum'] = teacher['maximum'] = 2
    assert run(exe, both_days)['success']
    print('4 quota study-day regression checks passed')
