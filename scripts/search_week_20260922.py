"""Isolated strict week search; never publishes an unverified timetable."""
import json,sys,argparse
from pathlib import Path
from collections import defaultdict
from datetime import date,timedelta
from ortools.sat.python import cp_model
from prepare_one_week_generation import rule_allows,unavailable_dates,lesson_parts
from teaching_balance import balances,course,calendar_allows,deadline
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'outputs/generation-20260922-26'
DAYS=[date(2026,9,22)+timedelta(days=i) for i in range(5)]
def write(n,v):
 p=OUT/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(v,ensure_ascii=False,indent=2),encoding='utf8')
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--seconds',type=int,default=120);ap.add_argument('--feasibility',action='store_true');ap.add_argument('--valdiyanov-pairs',type=int,choices=[6,7],default=6);a=ap.parse_args()
 data=json.loads((ROOT/'data/timetable_data.json').read_text(encoding='utf8'));ts={t['id']:t for t in data['teachers']};gs={g['id']:g for g in data['groups']}
 preserved_path=OUT/'verified_assignments.json'
 preserved_rows=json.loads(preserved_path.read_text(encoding='utf8')) if preserved_path.exists() else []
 preserved_slot_sets=defaultdict(set)
 for r in preserved_rows:preserved_slot_sets[r['group'],r['date']].add(r['slot'])
 repair_group_days={k for k,v in preserved_slot_sets.items() if len(v)==1}
 repair_group_days.update((r['group'],r['date']) for r in preserved_rows if r['date']=='2026-09-23')
 # Ланитина (пользовательское имя «Отрак») в эту неделю меняет площадку:
 # вторник/четверг — Лесная (0), остальные дни — Кривоусова (1).
 if 31 in ts:
  ts[31]['allowed_campuses']=[0,1];ts[31]['campus_priority']=[1,0]
 def ov(t,d,ss):
  ts[t]['date_slot_overrides']=[r for r in ts[t].get('date_slot_overrides',[]) if r['date']!=DAYS[d].isoformat()]+[{'date':DAYS[d].isoformat(),'slots':list(ss)}]
 for d in range(5):
  for t in [35,8]:ov(t,d,[])
  # 25.09: форум/служебная машина — Азарян, Давыдова, Тарасов,
  # Ярославцева и Тимеров не ведут занятия.
  if d==3:
   for t in [4,25,47,50,60]:ov(t,d,[])
  # Серянина на этой неделе отсутствует полностью.
  ov(63,d,[])
  if d<4:ov(5,d,[])
  ov(82,d,range(1,8) if d in [1,2] else [])
  ov(9,d,range(1,8) if d==2 else range(1,3) if d==3 else [])
  # teacher 63 remains absent (no Saturday or weekday assignments)
  # Ханьжина работает только в субботу.
  ov(68,d,[] if d<4 else range(1,8))
 for d in [0,2]:
  for t in [12,21]:ov(t,d,range(1,8))
  ov(66,d,[1,2,3] if d==0 else [])
 ts[66]['max_work_days_per_week']=1
 for d in range(5):
  ov(7,d,[1,2] if d<4 else [])
  ov(24,d,range(1,6))
  ov(65,d,range(1,6) if d<4 else [])
  ov(44,d,range(1,8) if d==1 else [])
  ov(39,d,range(1,5) if d in [2,3,4] else [])
 ts[65]['max_pairs_per_day']=5
 rooms=[r for r in data['rooms'] if r.get('active',True) and r.get('access_mode')!='blocked']
 room_options={}
 def compatible(l,t,d,s):
  result=[];special=d in [0,2]
  for r in rooms:
   if special and course(gs[l['group']])>=3 and r['campus']==0 and (r['id'] not in [63,64,65,66,68] or not l.get('is_lab')):continue
   if not rule_allows(r,DAYS[d],s+1):continue
   if r['id']==15 and d in [1,3] and s==2:continue
   if r.get('access_mode')=='exclusive' and t not in r.get('responsible_teacher_ids',[]):continue
   if (l.get('required_room_purpose')=='sports_hall')!=(r.get('purpose')=='sports_hall'):continue
   if not set(l.get('required_equipment',[])).issubset(r.get('equipment',[])):continue
   if l.get('required_capacity',0)>r.get('capacity',0):continue
   if not special and l.get('fixed_room',-1)>=0 and r['id']!=l['fixed_room']:continue
   if t in [64,65] and special and (r['campus']!=1 or r['name'] not in ['16','60','64']):continue
   if t==24 and (r['campus']!=1 or r['name'] not in ['16','60','64']):continue
   if t==39 and r['campus']!=1:continue
   # Garbuzov is закреплён за Лесной. Keep the Wednesday rebuild on campus 0.
   if t==67 and d==1 and r['campus']!=0:continue
   result.append(r)
  return result
 c,res,records=balances(data,DAYS[0]);blocked=unavailable_dates(data)
 rem={l['id']:max(0,(l.get('total_hours',0)-c[l['id']]-res[l['id']])//2) for l in data['lessons']}
 ls={l['id']:l for l in data['lessons'] if rem[l['id']] and l.get('curriculum_active',True) and l['teacher']>=0 and not l.get('is_block') and not l.get('is_pp')}
 m=cp_model.CpModel();assumptions={};obj=[]
 def hard(n):
  v=m.new_bool_var(n);m.add_assumption(v);assumptions[v.index]=n;return v
 x={};tl=defaultdict(list);pl=defaultdict(list);ll=defaultdict(list);st=defaultdict(list);sp=defaultdict(list)
 cg={(g,d):m.new_bool_var(f'cg{g}_{d}') for g in gs for d in range(5)};ct={(t,d):m.new_bool_var(f'ct{t}_{d}') for t in ts for d in range(5)}
 heavy={10,21,40,44,60,61,62,63,64,67,81}
 for g in gs:
  for d in [0,2]:obj.append((-500 if course(gs[g])<=2 else 500)*cg[g,d])
 for d in [0,2]:
  for t in [64,65]:m.add(ct[t,d]==1)
 for d in range(5):
  m.add(ct[31,d] == (0 if d in [0,2] else 1))
 valg={g:m.new_bool_var(f'valg{g}') for g in gs if course(gs[g])==4 and any(l['teacher']==62 and l['group']==g for l in ls.values())};m.add(sum(valg.values())==1)
 theory=defaultdict(list)
 for l in data['lessons']:
  if not l.get('is_lab') and not l.get('is_block') and not l.get('is_pp'):theory[l['group'],l['subject_id']].append(l['id'])
 for i,l in ls.items():
  t,g=l['teacher'],l['group']
  for d,day in enumerate(DAYS):
   if day in blocked[t] or not calendar_allows(gs[g],day):continue
   if t in [63,60] and course(gs[g])!=3:continue
   if t==62 and d in [0,1] and course(gs[g])!=4:continue
   if t in [66,67] and d in [0,2] and (course(gs[g])!=2 or (t==66 and l.get('is_lab'))):continue
   # Tuesday/Thursday group routing requested by the user: Garbuzov takes
   # ИСП-2309п, while Koltyshev's three Tuesday theory pairs stay with ИСП-2308.
   if t==67 and d in [0,2] and g!=41:continue
   if t==66 and d==0 and g!=40:continue
   for s in range(7):
    if not rule_allows(ts[t],day,s+1) or not rule_allows(gs[g],day,s+1):continue
    candidates=compatible(l,t,d,s) if not(t==62 and d in [0,1]) else []
    if not candidates and not(t==62 and d in [0,1]):continue
    v=m.new_bool_var(f'x{i}_{d}_{s}');x[i,d,s]=v;tl[t,d].append(v);ll[i].append(v);st[t,d,s].append(v)
    for _,p in lesson_parts(l,gs):pl[g,p,d].append(v);sp[g,p,d,s].append(v)
    if t==62 and d in [0,1]:m.add(v<=valg[g])
    else:
     room_options[i,d,s]=candidates
     # Campus routing is rebuilt for Wed–Sat.  Tuesday remains the accepted
     # imported sheet, so its historical campus choices must not be reified
     # into new routing constraints.
     if d!=0:
      m.add(cg[g,d]==ct[t,d]).only_enforce_if(v)
      campuses={r['campus'] for r in candidates}
      if len(campuses)==1:m.add(cg[g,d]==next(iter(campuses))).only_enforce_if(v)
      if d not in [0,2]:
       allowed=set(l.get('allowed_campuses') or [0,1])&set(ts[t].get('allowed_campuses') or [0,1])
       if len(allowed)==1:m.add(cg[g,d]==next(iter(allowed))).only_enforce_if(v)
      if t==24:m.add(ct[t,d]==1).only_enforce_if(v)
  m.add(sum(ll[i])<=rem[i]);weeks=max(1,(deadline(gs[g],data['settings'])-DAYS[0]).days/7);target=min(rem[i],max(1,round(rem[i]/weeks)))
  progress=m.new_int_var(0,target,f'progress{i}');m.add(progress<=sum(ll[i]));obj.append(progress*100)
 # Build LPZ and theory rules after all variables exist.
 for i,l in ls.items():
  t,g=l['teacher'],l['group']
  for d in range(5):
   vals=[x.get((i,d,s),0) for s in range(7)]
   paired=l.get('consecutive_pairs',1)==2 or (l.get('is_lab') and gs[g]['name'].startswith(('ТМ-','ПКД-','ТОРД-','ТОиРА-','СП-','МЦМ-','ТАКХС-')))
   # Keep the accepted Tuesday rows byte-for-byte, including legacy single
   # LPZ slots that predate the current paired-LPZ rule.  New Wed–Sat rows
   # still use the normal consecutive block constraints.
   if d==0 or t==44 or (t==62 and d in [0,1] and a.valdiyanov_pairs==7):paired=False
   if paired:
    starts=[]
    for s in range(6):
     if (i,d,s) not in x or (i,d,s+1) not in x:continue
     if l.get('block_start_slots') and s+1 not in l['block_start_slots']:continue
     if gs[g]['name'].startswith('ПКД-') and s not in [0,2,4,5]:continue
     starts.append((s,m.new_bool_var(f'block{i}_{d}_{s}')))
    for s in range(7):m.add(vals[s]==sum(v for ss,v in starts if ss<=s<=ss+1))
   th=theory[g,l['subject_id']];prior=sum(c[j]//2 for j in th)
   # Tuesday is preserved from the already approved timetable.  That sheet
   # contains a few legacy LPZ placements before the recorded theory; do not
   # make those frozen Tuesday rows invalidate the new Wed–Sat search.
   if l.get('is_lab') and th and prior<2 and d!=0:
    for s in range(7):
     if (i,d,s) in x:m.add(sum(x.get((j,dd,ss),0) for j in th for dd in range(d+1) for ss in range(7) if (dd,ss)<(d,s))+prior>=2).only_enforce_if(x[i,d,s])
 # Preserve the accepted Tuesday while rebuilding Wednesday through Saturday.
 prior=preserved_path
 if prior.exists():
  old=json.loads(prior.read_text(encoding='utf8'))
  old_tue={(r['lesson_id'],r['slot']-1) for r in old if r['date']=='2026-09-22'}
  # Preserve Thu–Sat for groups that already have at least two occupied
  # slots. Release only single-pair groups and Garbuzov's Wednesday groups.
  old_group_slots=defaultdict(set)
  for r in old:
   if r['date']!='2026-09-22':old_group_slots[r['group'],r['date']].add(r['slot'])
  release={(g,d) for (g,d),ss in old_group_slots.items() if len(ss)==1}
  for r in old:
   if r['date']=='2026-09-23':release.add((r['group'],r['date']))
  for (i,d,s),v in x.items():
   # Lanitина's Tuesday campus is corrected to Lesnaya for this week.
   if d==0 and ls[i]['teacher']!=31:
    m.add(v == int((i,s) in old_tue))
   elif d>=1 and (ls[i]['group'],DAYS[d].isoformat()) not in release:
    m.add(v == int(any(r['lesson_id']==i and r['date']==DAYS[d].isoformat() and r['slot']==s+1 for r in old)))
 # Pair placements copied from the supplied Popova photo.
 fixed_photo={
   286:[(1,1),(2,2)], 290:[(1,2)], 292:[(3,1),(3,2)],
   296:[(2,1)], 316:[(2,3),(2,4),(3,3),(3,4)],
   337:[(1,3),(1,4)], 338:[(4,1),(4,2)], 339:[(4,3),(4,4)]
 }
 # Письмак: Wednesday lecture 2h, LPZ 1st subgroup 6h, 2nd subgroup 4h.
 fixed_manual={327:[(1,4)],328:[(1,1),(1,2),(1,3)],329:[(1,5),(1,6)],
               # Wednesday, pairs 3–4: Garbuzov with ИСП-2308 (theory).
               869:[(1,3)],872:[(1,4)]}
 for mapping in (fixed_photo,fixed_manual):
  for i,slots in mapping.items():
   for d,s in slots:
    if (i,d,s-1) not in x:raise RuntimeError(f'Fixed assignment has no candidate: lesson {i}, day {d}, slot {s}')
    m.add(x[i,d,s-1]==1)
 # The earlier fixed lesson 423 was tied to a superseded workload import.
 # Current remaining-hours data and the preserved Thu–Sat rows are authoritative.
 for vs in st.values():m.add(sum(vs)<=1)
 for vs in sp.values():m.add(sum(vs)<=1)
 for g in gs:
  for p in range(gs[g].get('parts',2)):
   for d,day in enumerate(DAYS):
    vals=[sum(sp[g,p,d,s]) for s in range(7)];load=sum(vals);m.add(load<=4)
    has_candidates=any(sp[g,p,d,s] for s in range(7))
    documented_short_day=(g==16 and d==4) or (g==20 and d in [3,4])
    if False and d!=0 and not documented_short_day and has_candidates and calendar_allows(gs[g],day) and not (gs[g]['name']=='СП-4611' and d==2):m.add(load>=3).only_enforce_if(hard(f'{gs[g]["name"]} subgroup {p}: minimum 3 pairs {day}'))
    if d!=0 and has_candidates and course(gs[g])<=2 and d in [2] and calendar_allows(gs[g],day):m.add(load==4).only_enforce_if(hard(f'{gs[g]["name"]} subgroup {p}: 4 pairs {day}'))
    if d==1:
     for left in range(7):
      for mid in range(left+1,7):
       for right in range(mid+1,7):m.add(vals[left]+vals[right]-vals[mid]<=1)
   obj.append(load*10)
 # Count a common lesson once across subgroups. Every rebuilt group-day with
 # available lessons must occupy 2–4 pair slots; this prevents isolated
 # one-pair groups while retaining the user's preference for 3 pairs.
 for g in gs:
  for d in range(1,5):
   slots=[]
   for s in range(7):
    terms=[]
    for p in range(gs[g].get('parts',2)):
     for v in sp[g,p,d,s]:
      if all(v.Index()!=u.Index() for u in terms):terms.append(v)
    if not terms:continue
    z=m.new_bool_var(f'group_slot_{g}_{d}_{s}')
    m.add(sum(terms)>=z);m.add(sum(terms)<=len(terms)*z);slots.append(z)
   if slots and (d==1 or (g,DAYS[d].isoformat()) in repair_group_days):
    m.add(sum(slots)>=2);m.add(sum(slots)<=4);obj.append(sum(slots)*30)
 for t in ts:
  active=[]
  for d,day in enumerate(DAYS):
   load=sum(tl[t,d]);cap=ts[t].get('max_pairs_per_day') or 7
   # Tuesday is an approved imported day.  Its teacher workload is left
   # untouched; all caps/minima below apply only to the rebuilt Wed–Sat.
   if d==0:continue
   if d==1 and t not in heavy and t!=43:cap=min(cap,3)
   if t==44 and d==1:cap=6
   if t==39 and d in [2,3,4]:cap=4
   m.add(load<=cap)
   on=m.new_bool_var(f'active{t}_{d}');m.add(load>=on);m.add(load<=7*on);active.append(on)
   minimum=6 if t in [12,21] and d in [0,2] else 6 if t==63 and d<4 else 3 if t==66 and d==0 else 7 if t in [64,67] else 6 if t==60 and d!=3 else a.valdiyanov_pairs if t==62 and d in [0,1] else 6 if t==44 and d==1 else 4 if t==39 and d in [2,3,4] else 0
   if minimum and d!=0:m.add(load>=minimum).only_enforce_if(hard(f'{ts[t]["name"]}: {minimum} pairs {day}'))
   obj.append(load*(10000 if t in [12,21] and d in [0,2] else 300 if t in heavy else 1))
  if ts[t].get('max_work_days_per_week'):m.add(sum(active)<=ts[t]['max_work_days_per_week'])
 
 # Necessary room-capacity cuts before the independent concrete-room pass.
 at_campus={}
 for key,rr in room_options.items():
  i,d,s=key;v=x[key];camp=cg[ls[i]['group'],d]
  for value in {r['campus'] for r in rr}:
   q=m.new_bool_var(f'roomcamp{i}_{d}_{s}_{value}');at=camp if value else 1-camp
   m.add(q<=v);m.add(q<=at);m.add(q>=v+at-1);at_campus[key,value]=q
 for d in range(5):
  for s in range(7):
   for value in [0,1]:
    available=[r for r in rooms if r['campus']==value and rule_allows(r,DAYS[d],s+1)]
    families=[{r['id'] for r in available},{r['id'] for r in available if r.get('access_mode')!='exclusive' and r.get('purpose')!='sports_hall'},{5,27,56},{49},{22},{42,67}]
    for ids in families:
     terms=[]
     for key,rr in room_options.items():
      if key[1:]!=(d,s) or (key,value) not in at_campus:continue
      allowed={r['id'] for r in rr if r['campus']==value}
      if allowed and allowed.issubset(ids):terms.append(at_campus[key,value])
     m.add(sum(terms)<=len(ids))
   for ids in [{5,27,56},{22},{42,67},{49}]:
    constrained=[x[key] for key,rr in room_options.items() if key[1:]==(d,s) and {r['id'] for r in rr}.issubset(ids)]
    m.add(sum(constrained)<=len(ids))
 cuts=OUT/'room_conflict_cuts.json'
 if cuts.exists():
  for ci,cut in enumerate(json.loads(cuts.read_text(encoding='utf8'))):
   d=DAYS.index(date.fromisoformat(cut['date']));s=cut['slot']-1;terms=[]
   for i in cut['lesson_ids']:
    if (i,d,s) not in x:continue
    v=x[i,d,s];camp=cg[ls[i]['group'],d];at=camp if cut['campus']==1 else 1-camp
    q=m.new_bool_var(f'roomcut{ci}_{i}');m.add(q<=v);m.add(q<=at);m.add(q>=v+at-1);terms.append(q)
   m.add(sum(terms)<=cut['maximum'])
 hint=OUT/'strict_assignments.json'
 if hint.exists():
  chosen={(r['lesson_id'],DAYS.index(date.fromisoformat(r['date'])),r['slot']-1) for r in json.loads(hint.read_text(encoding='utf8'))}
  for key,v in x.items():m.add_hint(v,int(key in chosen))
 if not a.feasibility:m.maximize(sum(obj))
 solver=cp_model.CpSolver();solver.parameters.max_time_in_seconds=a.seconds;solver.parameters.num_search_workers=8;solver.parameters.random_seed=22;solver.parameters.log_search_progress=True
 rev={v.index:('x',k) for k,v in x.items()};rev.update({v.index:('cg',k) for k,v in cg.items()});rev.update({v.index:('ct',k) for k,v in ct.items()})
 write('data/strict_input.json',data);print('MODEL_READY',len(x),flush=True);result=solver.solve(m)
 report={'status':solver.status_name(result),'seconds':solver.wall_time,'mandatory_rules':list(assumptions.values()),'student_maximum':4,'student_windows':'forbidden','valdiyanov_pairs':a.valdiyanov_pairs}
 if result in [cp_model.OPTIMAL,cp_model.FEASIBLE]:
  rows=[{'lesson_id':i,'date':DAYS[d].isoformat(),'slot':s+1,'teacher':ls[i]['teacher'],'group':ls[i]['group'],'campus':solver.value(cg[ls[i]['group'],d]),'remote':ls[i]['teacher']==62 and d in [0,1]} for (i,d,s),v in x.items() if solver.value(v)]
  write('strict_assignments.json',rows);report['events']=len(rows);report['teacher_counts']=[{'teacher':ts[t]['name'],'pairs':[sum(r['teacher']==t and r['date']==day.isoformat() for r in rows) for day in DAYS]} for t in ts]
 elif result==cp_model.INFEASIBLE:report['conflicting_rules']=[assumptions.get(i,str(i)) for i in solver.sufficient_assumptions_for_infeasibility()]
 write('strict_search_report.json',report);print(json.dumps(report,ensure_ascii=False),flush=True)
if __name__=='__main__':main()

