"""Select Saturday with CP-SAT, using confirmed residuals and group deadlines.

Works in an isolated run directory. Never changes source curricula or journals.
"""
import argparse
import copy
import json
import math
import subprocess
from collections import defaultdict
from datetime import date, timedelta
from pathlib import Path

from prepare_one_week_generation import rule_allows, lesson_parts, unavailable_dates

ROOT = Path(__file__).resolve().parents[1]
DAY = date(2026, 9, 12)


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')


def prepare(source, folder):
    data = copy.deepcopy(source)
    groups = {g['id']: g for g in data['groups']}
    teachers = {t['id']: t for t in data['teachers']}
    rooms = {r['id']: r for r in data['rooms']}
    # User confirmed these are the same person after a surname change.
    for g in groups.values():
        if g.get('curator_teacher') == 49:
            g['curator_teacher'] = 81
    teachers[49]['scheduling_active'] = False
    teachers[49]['merged_into_teacher_id'] = 81
    teachers[81]['previous_names'] = sorted(set(teachers[81].get('previous_names', []) + [teachers[49]['name']]))
    for absence in data.get('teacher_unavailable', []):
        if absence.get('teacher') == 26 and absence.get('to') == DAY.isoformat():
            absence['to'] = (DAY - timedelta(days=1)).isoformat()
            if absence.get('to_date'): absence['to_date'] = absence['to']
            absence['text'] += '; пользователь подтвердил выход 12.09.2026'
    last = date.fromisoformat(data['settings']['semester_end_date'])
    blocked = unavailable_dates(data)
    curators = {g.get('curator_teacher') for g in groups.values()}
    for teacher in teachers.values():
        if teacher['id'] in curators and not teacher.get('curator_only') and not any(rule_allows(teacher, DAY, s) for s in range(1, 8)):
            # Explicit Saturday attendance overrides the recurring day off only.
            slots = sorted({s for w in teacher.get('work_days', []) if w.get('enabled') for s in w.get('slots', [])})
            teacher['date_slot_overrides'] = [x for x in teacher.get('date_slot_overrides', []) if x['date'] != DAY.isoformat()]
            teacher['date_slot_overrides'].append({'date': DAY.isoformat(), 'slots': slots})
        if teacher['id'] in (9, 12):
            teacher['date_slot_overrides'] = [x for x in teacher.get('date_slot_overrides', []) if x['date'] != DAY.isoformat()]
            teacher['date_slot_overrides'].append({'date': DAY.isoformat(), 'slots': list(range(1, 8))})
    # User's latest Saturday instruction supersedes the historical 1–4 window.
    semenova = next(t for t in teachers.values() if t['name'].startswith('Семенова '))
    semenova['date_slot_overrides'] = [x for x in semenova.get('date_slot_overrides', []) if x['date'] != DAY.isoformat()]
    semenova['date_slot_overrides'].append({'date': DAY.isoformat(), 'slots': list(range(1, 8))})
    for teacher in teachers.values():
        if teacher['name'].startswith(('Серянина ', 'Письмак ', 'Тарасов ', 'Усков ', 'Ханьжина ', 'Круглова ')):
            teacher['date_slot_overrides'] = [x for x in teacher.get('date_slot_overrides', []) if x['date'] != DAY.isoformat()]
            teacher['date_slot_overrides'].append({'date': DAY.isoformat(), 'slots': []})
    confirmed, prior = defaultdict(int), defaultdict(int)
    lesson_by_id = {l['id']: l for l in data['lessons']}
    seen = set()
    for e in data.get('teaching_ledger', []):
        k = (e.get('lesson_id'), e.get('date'), e.get('slot'))
        if e.get('status') != 'confirmed' or e['date'] >= DAY.isoformat() or k in seen:
            continue
        seen.add(k)
        confirmed[e['lesson_id']] += e['hours']
        l = lesson_by_id.get(e['lesson_id'], {})
        if not any(l.get(flag, False) for flag in ('is_lab', 'is_block', 'is_pp')):
            prior[(l.get('group'), l.get('subject_id'))] += e['hours'] // 2
    for p in data['settings'].get('prior_theory_pairs', []):
        prior[(p['group'], p['subject'])] = max(prior[(p['group'], p['subject'])], p['pairs'])

    def group_available(g, day):
        if any(p['from'] <= day.isoformat() <= p['to'] for p in g.get('practice_periods', [])):
            return False
        for u in data.get('unavailable', []):
            if u.get('group') == g['id'] and (day.isoformat() in u.get('dates', []) or
                (u.get('from', '9999') <= day.isoformat() <= u.get('to', '0000'))):
                return False
        return True

    def deadline(g):
        end = min(last, date.fromisoformat(g.get('teaching_deadline') or last.isoformat()))
        for p in g.get('practice_periods', []):
            first = date.fromisoformat(p['from'])
            if first <= last and p['to'] >= data['settings']['semester_start_date']:
                end = min(end, first - timedelta(days=1))
        return end

    def room_candidates(l, t):
        result = []
        for r in rooms.values():
            if not r.get('active', True) or r.get('access_mode') == 'blocked': continue
            if r.get('access_mode') == 'exclusive' and t['id'] not in r.get('responsible_teacher_ids', []): continue
            if l.get('fixed_room', -1) >= 0 and r['id'] != l['fixed_room']: continue
            if l.get('allowed_campuses') and r['campus'] not in l['allowed_campuses']: continue
            if t.get('allowed_campuses') and r['campus'] not in t['allowed_campuses']: continue
            if set(l.get('required_equipment', [])) - set(r.get('equipment', [])): continue
            if l.get('required_room_type', 0) and r.get('room_type') != l['required_room_type']: continue
            if l.get('required_capacity', 0) and r.get('capacity', 0) < l['required_capacity']: continue
            if (l.get('required_room_purpose') == 'sports_hall') != (r.get('purpose') == 'sports_hall'): continue
            lpz = 'лпз' in l['name'].lower()
            if t['id'] == 55 and (r['id'] != 66 if lpz else not (r['campus']==0 and r['name']!='210' and r.get('access_mode')=='general')): continue
            if t['id'] == 59 and (r['id'] not in (64,65) if lpz else not (r['campus']==0 and r['name']!='210' and r.get('access_mode')=='general')): continue
            if t['id'] in (49,57) and lpz and r['id'] != 68: continue
            result.append(r)
        return result

    variables, theory, labs = [], defaultdict(list), defaultdict(list)
    average = defaultdict(float)
    totals = defaultdict(int)
    teacher_caps = {}
    for t in teachers.values():
        allowed = [s for s in range(1, 8) if rule_allows(t, DAY, s)] if DAY not in blocked[t['id']] and t.get('scheduling_active', True) else []
        teacher_caps[t['id']] = min(len(allowed), t.get('max_pairs_per_day') or 7)
    for l in data['lessons']:
        if not l.get('curriculum_active', True) or l.get('is_block') or l.get('is_pp'): continue
        g, t = groups[l['group']], teachers.get(l['teacher'])
        if not t or not t.get('scheduling_active', True): continue
        rest = max(0, l.get('total_hours', 0) - confirmed[l['id']])
        if rest == 0: continue
        totals[t['id']] += rest
        end = deadline(g)
        for rule in t.get('desired_load_rules', []):
            course = int(str(g['name']).split('-')[-1][0]) if str(g['name']).split('-')[-1][0].isdigit() else 0
            if rule.get('deadline') and (not rule.get('course_year') or rule['course_year'] == course) and (not rule.get('group_ids') or g['id'] in rule['group_ids']):
                end = min(end, date.fromisoformat(rule['deadline']))
        available_days = 0
        d = DAY
        while d <= end:
            if d.isoweekday() <= 6 and d not in blocked[t['id']] and group_available(g, d) and any(rule_allows(g, d, s) and rule_allows(t, d, s) for s in range(1,8)):
                available_days += 1
            d += timedelta(days=1)
        average[t['id']] += rest / 2 / max(1, available_days)
        candidates = room_candidates(l, t)
        slots = [s-1 for s in range(1,8) if teacher_caps[t['id']] and group_available(g, DAY) and rule_allows(g, DAY, s) and rule_allows(t, DAY, s) and any(rule_allows(r, DAY, s) and (not r.get('available_slots') or s in r['available_slots']) for r in candidates)]
        step = 2 if l.get('consecutive_pairs') == 2 else 1
        maximum = min(rest // 2, 2 if l.get('subgroup', -1) < 0 else (4 if t['id'] == 51 else 3), len(slots))
        if t['id'] == 12:
            maximum = min(maximum, 1)
        maximum -= maximum % step
        if not slots or maximum == 0: continue
        parts = [gid*2+p for gid,p in lesson_parts(l, groups)]
        weeks = max(1, math.ceil((end-DAY).days / 7))
        variables.append({'id': l['id'], 'minimum': 0, 'maximum': maximum, 'semester_total': rest//2,
                          'distribution_weeks': max(1, available_days), 'priority_weight': max(1, 100 // weeks),
                          'teacher': t['id'], 'group': g['id'], 'parts': parts, 'part_weight':len(parts),
                          'whole_group': l.get('subgroup', -1) < 0, 'subject':str(l['subject_id']),
                          'allowed_slots': slots, 'allowed_campuses':sorted({r['campus'] for r in candidates}),
                          'consecutive_pairs':step, 'avoid_lunch_split':l.get('avoid_lunch_split',False),
                          'block_start_slots':l.get('block_start_slots',[]),
                          'restricted_room': candidates[0]['id'] if len(candidates)==1 else -1,
                          'sports_room':l.get('required_room_purpose')=='sports_hall',
                          'computer_room':'computer' in l.get('required_equipment',[])})
        (labs if l.get('is_lab') else theory)[(g['id'], l['subject_id'])].append(l['id'])
        if t['id'] == 51:
            variables[-1]['daily_subject_limits'] = [{'day':0, 'maximum':4}]
        if t['id'] == 12:
            variables[-1]['daily_subject_limits'] = [{'day':0, 'maximum':1}]

    ts, target_report = [], []
    previous_load = defaultdict(int)
    if source['settings'].get('start_date') == DAY.isoformat() and source['settings'].get('end_date') == DAY.isoformat():
        for lesson in source['lessons']:
            if lesson.get('generation_active', True):
                previous_load[lesson['teacher']] += lesson.get('total_slots', 0)
    urgent = {'Тимеров':6, 'Вальдиянов':7, 'Гарбузов':7, 'Меренчуков':7, 'Сивилькаев':7, 'Рахматулина':5, 'Кошелев':7, 'Буркова':7, 'Комарова':7, 'Дроговейко':2}
    for t in teachers.values():
        cap = teacher_caps[t['id']]
        mean = average[t['id']]
        target = min(cap, math.ceil(mean - 1e-9))
        hard = 0
        surname = t['name'].split()[0]
        if surname in urgent and cap:
            target = min(cap, urgent[surname]); hard = target
        if t['id'] in curators and any(v['teacher'] == t['id'] for v in variables):
            hard = max(hard, 1)
            target = max(target, hard)
        if t['id'] == 12:
            cap = min(cap, 6); target = 6; hard = 6
        if t['id'] == 9:
            cap = min(cap, 2); target = 2; hard = 2
        if t['id'] == semenova['id']:
            cap = target; hard = target
        # A later revision may add load, but must not reduce any teacher's
        # agreed Saturday load. Explicit absences remain exceptions.
        if cap and previous_load[t['id']] > 0:
            hard = max(hard, previous_load[t['id']])
            target = max(target, hard)
        t['date_load_targets'] = [x for x in t.get('date_load_targets', []) if x['date'] != DAY.isoformat()]
        if hard:
            t['date_load_targets'].append({'date': DAY.isoformat(), 'minimum_pairs': hard})
            if t['id'] == 51:
                t['date_load_targets'][-1]['maximum_same_subject_pairs'] = 4
            if t['id'] == 12:
                t['date_load_targets'][-1]['maximum_same_subject_pairs'] = 1
        ts.append({'id':t['id'], 'minimum':target, 'hard_minimum':hard, 'maximum':cap, 'maximum_daily':cap})
        if cap: target_report.append({'teacher_id':t['id'],'name':t['name'],'remaining_hours':totals[t['id']], 'arithmetic_pairs_per_workday':round(mean,3),'target_pairs':target,'hard_minimum':hard,'maximum':cap})
    part_ids = defaultdict(list)
    for v in variables:
        for p in v['parts']: part_ids[p].append(v['id'])
    model = {'variables':variables,'teachers':ts,'parts':[{'key':gid*2+p,'group':gid,'part':p,'minimum_target':4,'maximum_target':4,'lesson_ids':part_ids[gid*2+p]} for gid,g in groups.items() for p in range(g['parts'])],
             'lab_rules':[{'theory_ids':theory[k],'lab_ids':ls,'prior_theory':prior[k]} for k,ls in labs.items() if theory[k] or prior[k]],
             'day_count':1,'slots_per_day':7,'min_student_pairs_per_day':4,'max_student_pairs_per_day':4,
             'preferred_student_pairs_per_day':4,'student_day_shortfall_weight':100000,
             'hard_no_student_windows':True,'hard_no_teacher_windows':True,
             'whole_group_same_subject_limit':2,'physical_part_same_subject_limit':3,
             'allow_teacher_shortfalls':True,'teacher_shortfall_weight':10000000,'teacher_overload_weight':10000,
             'maximize_part_load':False,'distribution_weeks':84,
             'room_capacity_by_campus':[sum(r.get('active',True) and r.get('access_mode')!='blocked' and r['campus']==c and r.get('purpose')!='sports_hall' for r in rooms.values()) for c in (0,1)],
             'computer_capacity_by_campus':[sum(r.get('active',True) and r.get('access_mode')!='blocked' and r['campus']==c and 'computer' in r.get('equipment',[]) for r in rooms.values()) for c in (0,1)],
             'sports_capacity_by_campus':[sum(r.get('active',True) and r.get('access_mode')!='blocked' and r['campus']==c and r.get('purpose')=='sports_hall' for r in rooms.values()) for c in (0,1)],
             'time_limit_seconds':60,'workers':8,'random_seed':41}
    def room_open(r,s):
        return r.get('active',True) and r.get('access_mode')!='blocked' and rule_allows(r,DAY,s) and (not r.get('available_slots') or s in r['available_slots'])
    model['room_capacity_by_time'] = [[sum(room_open(r,s) and r['campus']==c and r.get('access_mode')!='exclusive' and r.get('purpose')!='sports_hall' for r in rooms.values()) for c in (0,1)] for s in range(1,8)]
    model['sports_capacity_by_time'] = [[sum(room_open(r,s) and r['campus']==c and r.get('purpose')=='sports_hall' for r in rooms.values()) for c in (0,1)] for s in range(1,8)]
    # Cumulative early-deadline targets use the same readout as the backend.
    # Physically impossible semester deficits remain explicit report issues.
    readout_path=folder/'readout.json'
    if readout_path.exists():
        readout=json.loads(readout_path.read_text(encoding='utf-8-sig'))
        load_targets=[]
        # Per-teacher/per-group rounding is a forecast warning, not permission
        # to exceed students' hard four-pair limit on this one-day draft.
        for t in (60,62):
            ids=[v['id'] for v in variables if v['teacher']==t and groups[v['group']]['name'].split('-')[-1].startswith('3')]
            load_targets.append({'lesson_ids':ids,'minimum':6 if t==60 else 4})
        model['lesson_load_targets']=load_targets
    write(folder/'quota-model.json',model)
    write(folder/'teacher-targets.json',target_report)
    data['settings']['start_date'] = data['settings']['end_date'] = DAY.isoformat()
    data['settings'].setdefault('solver_config', {})['min_student_pairs_per_study_day'] = 4
    data['settings']['teacher_period_targets'] = []
    data['settings']['prior_theory_pairs'] = [{'group':g,'subject':s,'pairs':n} for (g,s),n in prior.items() if g is not None and s is not None]
    write(folder/'base-data.json',data)
    print(json.dumps({'variables':len(variables),'semenova':next(t for t in target_report if t['teacher_id']==semenova['id'])},ensure_ascii=False))


def select(folder):
    exe = ROOT/'.tmp/build-sep7/Release/quota_optimizer.exe'
    result = subprocess.run([str(exe),str((folder/'quota-model.json').resolve())],capture_output=True,text=True,encoding='utf-8')
    report = json.loads(result.stdout)
    write(folder/'quota-result.json',report)
    if not report.get('success'): raise RuntimeError(report)
    data = json.loads((folder/'base-data.json').read_text(encoding='utf-8'))
    data['settings']['automatic_period_quotas'] = False
    for lesson in data['lessons']:
        lesson['total_slots'] = report['quotas'].get(str(lesson['id']),0)
        lesson['generation_active'] = lesson['total_slots'] > 0
    write(folder/'data/timetable_data.json',data)
    locks = [{'lesson_id':int(l),'date':DAY.isoformat(),'slot':t} for l,times in report['placement_witness'].items() for t in times]
    write(folder/'locks.json',{'source':'CP-SAT с остатками часов и сроками практики','assignments':locks})
    print(json.dumps({'status':report['status'],'pairs':sum(report['quotas'].values()),'teacher_shortfalls':report['teacher_shortfalls']},ensure_ascii=False))


if __name__ == '__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['prepare','select']);p.add_argument('--folder',default='outputs/practice-deadlines-2026-09-10/saturday');p.add_argument('--data',default=str(ROOT/'data/timetable_data.json'));a=p.parse_args();folder=Path(a.folder)
    if a.mode=='prepare':prepare(json.loads(Path(a.data).read_text(encoding='utf-8-sig')),folder)
    else:select(folder)
