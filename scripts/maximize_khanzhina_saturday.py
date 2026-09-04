"""Raise Khanzhina to the maximum seven Saturday pairs without changing totals."""
import json
from collections import Counter
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'data/timetable_data.json'
d=json.loads(p.read_text(encoding='utf-8-sig')); by={x['id']:x for x in d['lessons']}
# Remove five non-Friday quotas from the same four groups.
for lid in [895,900,926,952,976]:
    if by[lid].get('total_slots',0)<1: raise RuntimeError(f'Нет квоты для {lid}')
    by[lid]['total_slots']-=1; by[lid]['plan_active']=by[lid]['total_slots']>0
# Add five current-workload pairs to Khanzhina: 2+1+1+1. Together
# with her two existing Saturday pairs this yields the daily maximum of 7.
for lid,amount in {913:2,934:1,940:1,963:1}.items():
    by[lid]['total_slots']=by[lid].get('total_slots',0)+amount
    by[lid]['plan_active']=True; by[lid]['generation_active']=True
loads=Counter()
for x in d['lessons']:
    if x.get('teacher',-1)>=0: loads[x['teacher']]+=x.get('total_slots',0)
teachers={x['id']:x['name'] for x in d['teachers']}; old={x.get('teacher'):x for x in d['settings'].get('teacher_period_targets',[])}
d['settings']['teacher_period_targets']=[{'teacher':tid,'name':teachers[tid],'minimum_pairs':q,'desired_pairs':q,'selected_pairs':q,
 'minimum_safe_for_deadline':min(q,old.get(tid,{}).get('minimum_safe_for_deadline',0)),'remaining_semester_pairs':old.get(tid,{}).get('remaining_semester_pairs',q)} for tid,q in sorted(loads.items()) if q>0]
p.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'khanzhina_period_quota':loads[68],'expected_saturday':7,'total_pairs':sum(loads.values())}))
