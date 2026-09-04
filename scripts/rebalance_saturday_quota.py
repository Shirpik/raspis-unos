"""Move one Saturday pair away from an over-capacity teacher."""
import json
from collections import Counter
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'data/timetable_data.json'
d=json.loads(p.read_text(encoding='utf-8-sig')); lessons={x['id']:x for x in d['lessons']}
# МЦМ-Пф-301: replace one Saturday physical-chemistry pair taught by
# Semenova with current-workload Geography. Friday remains untouched.
lessons[360]['total_slots']-=1
lessons[360]['plan_active']=lessons[360]['total_slots']>0
lessons[351]['total_slots']+=1
lessons[351]['plan_active']=True; lessons[351]['generation_active']=True
loads=Counter()
for x in d['lessons']:
    if x.get('teacher',-1)>=0: loads[x['teacher']]+=x.get('total_slots',0)
teachers={x['id']:x['name'] for x in d['teachers']}; old={x.get('teacher'):x for x in d['settings'].get('teacher_period_targets',[])}
d['settings']['teacher_period_targets']=[{'teacher':tid,'name':teachers[tid],'minimum_pairs':q,'desired_pairs':q,'selected_pairs':q,
  'minimum_safe_for_deadline':min(q,old.get(tid,{}).get('minimum_safe_for_deadline',0)),
  'remaining_semester_pairs':old.get(tid,{}).get('remaining_semester_pairs',q)} for tid,q in sorted(loads.items()) if q>0]
p.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'removed_lesson':360,'added_lesson':351,'total_pairs':sum(loads.values())}))
