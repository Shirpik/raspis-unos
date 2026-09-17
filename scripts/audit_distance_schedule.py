"""Independent audit against source hours and dated availability, not quota totals."""
import argparse
import json
from collections import Counter, defaultdict
from datetime import date
from pathlib import Path
from prepare_one_week_generation import lesson_parts, rule_allows, unavailable_dates
from teaching_balance import balances, deadline, calendar_allows, course


def audit(folder):
    folder = Path(folder)
    data = json.loads((folder/'base-data.json').read_text(encoding='utf-8-sig'))
    schedule = json.loads((folder/'candidate-cp/schedule_all.json').read_text(encoding='utf-8-sig'))
    lessons = {l['id']: l for l in data['lessons']}
    groups = {g['id']: g for g in data['groups']}
    teachers = {t['id']: t for t in data['teachers']}
    first = date(2026,9,18)
    done, reserved, _ = balances(data, first)
    blocked = unavailable_dates(data)
    errors, events = [], []
    student_slots, teacher_slots, per_lesson = defaultdict(list), defaultdict(list), defaultdict(list)
    teacher_counts, day_counts = defaultdict(lambda:[0,0]), Counter()
    for group in schedule['groups']:
        for day in group['days']:
            current = date.fromisoformat(day['date_iso'])
            di = (current-first).days
            for slot in day['slots']:
                if slot['slot'] <= 0: continue
                for item in slot['lessons']:
                    l = lessons[item['id']]; t = teachers[l['teacher']]; g = groups[l['group']]
                    human = slot['slot']
                    if not l.get('curriculum_active',True) or not l.get('plan_active',True): errors.append(['disabled_lesson',l['id']])
                    if current > deadline(g,data['settings']) or not calendar_allows(g,current): errors.append(['calendar',l['id'],day['date_iso']])
                    if current in blocked[t['id']] or not rule_allows(t,current,human) or not rule_allows(g,current,human): errors.append(['availability',l['id'],day['date_iso'],human])
                    teacher_slots[t['id'],di].append(human)
                    for gid,part in lesson_parts(l,groups): student_slots[gid,part,di].append(human)
                    per_lesson[l['id']].append((di,human)); teacher_counts[t['id']][di]+=1; day_counts[day['date_iso']]+=1
                    events.append({'id':l['id'],'teacher':t['id'],'group':g['id'],'date':day['date_iso'],'slot':human,'subgroup':l['subgroup']})
    for gid,g in groups.items():
        for part in range(g['parts']):
            for di in range(2):
                slots=sorted(student_slots[gid,part,di])
                if len(slots)!=4 or len(set(slots))!=4 or slots[-1]-slots[0]!=3: errors.append(['student_day',g['name'],part+1,di,slots])
    for (tid,di),slots in teacher_slots.items():
        if len(slots)!=len(set(slots)): errors.append(['teacher_conflict',teachers[tid]['name'],di,slots])
        if slots and max(slots)-min(slots)+1!=len(slots): errors.append(['teacher_windows',teachers[tid]['name'],di,slots])
        if len(slots)>(teachers[tid].get('max_pairs_per_day') or 7): errors.append(['teacher_daily_cap',tid,di])
    for lid,times in per_lesson.items():
        l=lessons[lid]
        if done[lid]+reserved[lid]+2*len(times)>l['total_hours']: errors.append(['hours_exceeded',lid,done[lid],reserved[lid],2*len(times),l['total_hours']])
        if l.get('consecutive_pairs')==2:
            for di in range(2):
                slots=sorted(s for d,s in times if d==di)
                if len(slots)%2 or any(slots[i+1]!=slots[i]+1 for i in range(0,len(slots)-1,2)): errors.append(['unpaired_lab',lid,di,slots])
                if l.get('block_start_slots') and any(slots[i]-1 not in l['block_start_slots'] for i in range(0,len(slots),2)): errors.append(['lab_start',lid,di,slots])
    revision=data['settings'].get('distance_revision',1)
    targets={'Гарбузов':[7,7],'Меренчуков':[7,7],'Вальдиянов':[7,7],'Тимеров':[6,6],'Комарова':[7,7],'Саламатина':[7,7],'Дроговейко':[2,2]}
    if revision>=2: targets.update({'Письмак':[7,7],'Ахметов':[7,7],'Михайлова Татьяна':[5,5 if data['settings'].get('mikhailova_saturday') else 0],'Новосёлова':[4,0],'Коробкова':[4,4],'Тарасов':[0,0],'Садриева':[0,0]})
    else: targets['Рабенок']=[3,3]
    for surname,expected in targets.items():
        t=next(t for t in teachers.values() if t['name'].startswith(surname+' '))
        if teacher_counts[t['id']]!=expected: errors.append(['requested_load',t['name'],teacher_counts[t['id']],expected])
    rab=next(t for t in teachers.values() if t['name'].startswith('Рабенок Мария'))
    rg=[e['group'] for e in events if e['teacher']==rab['id']]
    if revision>=2:
        required={l['group'] for l in lessons.values() if l['teacher']==rab['id'] and l.get('curriculum_active',True) and l.get('plan_active',True) and not l.get('is_block') and not l.get('is_pp')}
        if len(rg)!=len(required) or set(rg)!=required or abs(teacher_counts[rab['id']][0]-teacher_counts[rab['id']][1])>1: errors.append(['rabenok_all_groups_once',rg,sorted(required)])
        for prefix in ('Круглова','Михайлова Татьяна'):
            tid=next(t['id'] for t in teachers.values() if t['name'].startswith(prefix+' '))
            if any(e['slot']>5 for e in events if e['teacher']==tid): errors.append(['slots_one_to_five',prefix])
        for prefix in ('Цимфер','Кропотова'):
            tid=next(t['id'] for t in teachers.values() if t['name'].startswith(prefix+' '))
            if any(n<2 or n>3 for n in teacher_counts[tid]): errors.append(['distance_light_load',prefix,teacher_counts[tid]])
    else:
        if len(rg)!=6 or len(set(rg))!=6 or any(groups[g]['name']=='ЭОЭ-Пф-151' for g in rg): errors.append(['rabenok_unique_groups',rg])
        familiar={lessons[r['lesson_id']]['group'] for r in data.get('teaching_ledger',[]) if r.get('status')=='confirmed' and r.get('date','9999')<first.isoformat() and r.get('lesson_id') in lessons and lessons[r['lesson_id']]['teacher']==rab['id']}
        if set(rg)-familiar: errors.append(['rabenok_unfamiliar',sorted(set(rg)-familiar)])
    for surname,minimum in {'Сивилькаев':[6,6],'Рахматулина':[5,5],'Семенова':[6,0],'Усков':[7,1],'Самцов':[4,7],'Осипчук':[1,1]}.items():
        tid=next(t['id'] for t in teachers.values() if t['name'].startswith(surname+' '))
        if any(n<want for n,want in zip(teacher_counts[tid],minimum)): errors.append(['minimum_load',surname,teacher_counts[tid],minimum])
    for surname in ('Тимеров','Усков'):
        tid=next(t['id'] for t in teachers.values() if t['name'].startswith(surname+' '))
        if any(course(groups[e['group']])!=3 for e in events if e['teacher']==tid): errors.append(['third_course_only',surname])
    tretyak=next(t for t in teachers.values() if t['name'].startswith('Третяк '))
    theory=[l for l in lessons.values() if l['teacher']==tretyak['id'] and l['subgroup']<0 and not l.get('is_lab') and not l.get('is_block') and not l.get('is_pp')]
    theory_remaining=sum(max(0,l['total_hours']-done[l['id']]-reserved[l['id']]-2*len(per_lesson[l['id']])) for l in theory)
    if theory_remaining!=4 or any(lessons[e['id']]['subgroup']>=0 or lessons[e['id']].get('is_lab') for e in events if e['teacher']==tretyak['id']): errors.append(['tretyak_theory_remaining',theory_remaining])
    kos=next(t['id'] for t in teachers.values() if t['name'].startswith('Кошелев '))
    if teacher_counts[kos][1]!=7: errors.append(['koshelev_saturday',teacher_counts[kos][1]])
    if revision>=2:
        bur=next(t['id'] for t in teachers.values() if t['name'].startswith('Буркова '))
        if teacher_counts[bur][1]!=7: errors.append(['burkova_saturday',teacher_counts[bur][1]])
        khan=next(t['id'] for t in teachers.values() if t['name'].startswith('Ханьжина '))
        if teacher_counts[khan][1]!=7: errors.append(['khanzhina_saturday',teacher_counts[khan][1]])
    for target in data['settings'].get('preserve_teacher_period_loads',[]):
        if sum(teacher_counts[target['teacher']])<target['pairs']: errors.append(['previous_load_reduced',teachers[target['teacher']]['name'],teacher_counts[target['teacher']],target['pairs']])
    report={'ok':not errors,'errors':errors,'total_pairs':len(events),'pairs_by_date':dict(day_counts),
            'teacher_loads':[{'teacher_id':tid,'name':teachers[tid]['name'],'friday':n[0],'saturday':n[1],'total':sum(n)} for tid,n in sorted(teacher_counts.items())],
            'rabenok_groups':[{'group':groups[e['group']]['name'],'date':e['date'],'slot':e['slot']} for e in events if e['teacher']==rab['id']],
            'tretyak_remaining_theory_hours':theory_remaining,'physical_subgroups':sum(g['parts'] for g in groups.values()),
            'checks':['source balances and reservations','teacher and student overlap','student and teacher windows','four pairs per physical subgroup','calendar and deadlines','paired LPZ','requested loads','Rabenok group coverage','Tretyak remaining theory']}
    (folder/'final-validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False))
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folder');args=parser.parse_args()
    raise SystemExit(0 if audit(args.folder)['ok'] else 1)
