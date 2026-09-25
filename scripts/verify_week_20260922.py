"""Independent checks and room allocation for the September 22–26 candidate."""
import json,sys
from pathlib import Path
from collections import Counter,defaultdict
from datetime import date
from ortools.sat.python import cp_model
from teaching_balance import balances,course,calendar_allows
from prepare_one_week_generation import lesson_parts,rule_allows,unavailable_dates
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'outputs/generation-20260922-26'
def main():
 data=json.loads((OUT/'data/strict_input.json').read_text(encoding='utf8'));rows=json.loads((OUT/'strict_assignments.json').read_text(encoding='utf8'))
 ls={l['id']:l for l in data['lessons']};ts={t['id']:t for t in data['teachers']};gs={g['id']:g for g in data['groups']};rooms={r['id']:r for r in data['rooms'] if r.get('active',True) and r.get('access_mode')!='blocked'}
 c,res,_=balances(data,date(2026,9,22));blocked=unavailable_dates(data);errors=[];tc=Counter();pc=Counter();used=Counter();lp=defaultdict(list);ps=defaultdict(set)
 group_slots=defaultdict(set)
 for r in rows:
  l=ls[r['lesson_id']];day=date.fromisoformat(r['date']);s=r['slot'];t=l['teacher'];g=l['group'];used[l['id']]+=2;tc[t,r['date'],s]+=1;lp[l['id'],r['date']].append(s)
  if day in blocked[t] or not rule_allows(ts[t],day,s):errors.append(['availability',t,r['date'],s])
  if t in [35,8] or (t==5 and day.day<=25):errors.append(['absence',t,r['date'],s])
  if t==60 and r['date']=='2026-09-25':errors.append(['timerov_absent_friday',r])
  if t==31 and r['campus']!=(0 if r['date'] in ['2026-09-22','2026-09-24'] else 1):errors.append(['lanitina_weekly_campus',r])
  if t==39 and r['campus']!=1:errors.append(['korobkova_krivousova',r])
  group_slots[g,r['date']].add(s)
  for _,p in lesson_parts(l,gs):pc[g,p,r['date'],s]+=1;ps[g,p,r['date']].add(s)
 for k,n in tc.items():
  if n>1:errors.append(['teacher_collision',k,n])
 for k,n in pc.items():
  if n>1:errors.append(['subgroup_collision',k,n])
 for k,ss in ps.items():
  if len(ss)>4 or max(ss)-min(ss)+1!=len(ss):errors.append(['student_count_or_window',k,sorted(ss)])
 for (g,d),ss in group_slots.items():
  if d!='2026-09-22' and (len(ss)<2 or len(ss)>4):errors.append(['group_pair_count',g,d,len(ss),sorted(ss)])
 for i,h in used.items():
  if h+c[i]+res[i]>ls[i]['total_hours']:errors.append(['hours_exceeded',i,h,c[i],ls[i]['total_hours']])
 for (i,d),ss in lp.items():
  l=ls[i];paired=l.get('consecutive_pairs',1)==2 or (l.get('is_lab') and gs[l['group']]['name'].startswith(('ТМ-','ПКД-','ТОРД-','ТОиРА-','СП-','МЦМ-','ТАКХС-')))
  if l['teacher']==62 and d in ['2026-09-22','2026-09-23']:paired=False
  if paired:
   ss=sorted(ss)
   if len(ss)%2 or any(ss[j+1]!=ss[j]+1 for j in range(0,len(ss)-1,2)):errors.append(['unpaired_lpz',i,d,ss])
 days=[f'2026-09-{d}' for d in range(22,27)]
 for g in gs:
  for d in days:
   if not calendar_allows(gs[g],date.fromisoformat(d)):continue
   for p in range(gs[g].get('parts',2)):
    n=len(ps[g,p,d]);target=4 if course(gs[g])<=2 and d in [days[0],days[2]] else 2
    if n<target:errors.append(['student_minimum',g,p,d,n,target])
  for t in [60,62,63,64,66,67]:
   for di,d in enumerate(days):
    expected=6 if t==60 and di!=3 else 7 if t in [64,67] else 7 if t==62 and di<2 else 6 if t==63 and di<4 else 3 if t==66 and di==0 else 0
    n=sum(r['teacher']==t and r['date']==d for r in rows)
    if n<expected:errors.append(['teacher_target',t,d,n,expected])
    if t==66 and di!=0 and n:errors.append(['koltyshev_only_tuesday',d,n])
  for d in ['2026-09-24','2026-09-25','2026-09-26']:
   n=sum(r['teacher']==39 and r['date']==d for r in rows)
   if n!=4:errors.append(['korobkova_daily_four',d,n])
  # Photo placements for Popova T.V.
  expected_photo={(286,'2026-09-23',2),(290,'2026-09-23',1),(290,'2026-09-24',2),(292,'2026-09-25',1),(292,'2026-09-25',2),(296,'2026-09-24',1),(316,'2026-09-24',3),(316,'2026-09-24',4),(316,'2026-09-25',3),(316,'2026-09-25',4),(337,'2026-09-23',3),(337,'2026-09-23',4),(338,'2026-09-26',1),(338,'2026-09-26',2),(339,'2026-09-26',3),(339,'2026-09-26',4)}
  for item in expected_photo:
   if not any(r['lesson_id']==item[0] and r['date']==item[1] and r['slot']==item[2] for r in rows):errors.append(['popova_photo_missing',item])
  pismak={(r['lesson_id'],r['date'],r['slot']) for r in rows if r['teacher']==44}
  for item in {(327,'2026-09-23',4),(328,'2026-09-23',1),(328,'2026-09-23',2),(328,'2026-09-23',3),(329,'2026-09-23',5),(329,'2026-09-23',6)}:
   if item not in pismak:errors.append(['pismak_remaining_hours_missing',item])
  if sum(r['teacher']==44 and r['date']=='2026-09-23' for r in rows)!=6:errors.append(['pismak_wednesday_total'])
  if sum(r['lesson_id']==423 for r in rows)!=2:errors.append(['sutyagin_mdk0301_remaining',sum(r['lesson_id']==423 for r in rows)])
  for r in rows:
   if r['date'] in ['2026-09-22','2026-09-24'] and r['teacher']==67 and r['group']!=41:errors.append(['garbuzov_group_route',r])
   if r['date']=='2026-09-23' and r['teacher']==67 and r['campus']!=0:errors.append(['garbuzov_wednesday_campus',r])
   if r['date']=='2026-09-22' and r['teacher']==66 and r['group']!=40:errors.append(['koltyshev_group_route',r])
 report={'passed':not errors,'errors':errors,'events':len(rows),'rooms_checked':False}
 if errors:(OUT/'strict_audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps(report,ensure_ascii=False));return
 m=cp_model.CpModel();y={};occupancy=defaultdict(list);teacher_room=defaultdict(list);reward=[];missing=[]
 for index,r in enumerate(rows):
  if r.get('remote'):r['room_id']=-1;r['room_name']='Дистант';continue
  l=ls[r['lesson_id']];t=ts[r['teacher']];special=r['date'] in [days[0],days[2]];candidates=[]
  for rid,room in rooms.items():
   if special and course(gs[r['group']])>=3 and room['campus']==0 and (rid not in [63,64,65,66,68] or not l.get('is_lab')):continue
   if room['campus']!=r['campus']:continue
   if l.get('required_room_purpose')=='sports_hall' and room.get('purpose')!='sports_hall':continue
   if l.get('required_room_purpose')!='sports_hall' and room.get('purpose')=='sports_hall':continue
   if l.get('required_equipment') and not set(l['required_equipment']).issubset(room.get('equipment',[])):continue
   if l.get('required_capacity',0)>room.get('capacity',0):continue
   if room.get('access_mode')=='exclusive' and r['teacher'] not in room.get('responsible_teacher_ids',[]):continue
   if not special and l.get('fixed_room',-1)>=0 and rid!=l['fixed_room']:continue
   if rid==15 and r['date'] in ['2026-09-23','2026-09-25'] and r['slot']==3:continue
   if r['teacher'] in [64,65] and special and room['name'] not in ['16','60','64']:continue
   if r['teacher']==24 and room['name'] not in ['16','60','64']:continue
   if r['teacher']==39 and room['campus']!=1:continue
   if r['teacher']==67 and rid!=49:continue
   if not rule_allows(room,date.fromisoformat(r['date']),r['slot']):continue
   v=m.new_bool_var(f'r{index}_{rid}');y[index,rid]=v;candidates.append(v);occupancy[r['date'],r['slot'],rid].append(v);teacher_room[r['teacher'],r['date'],rid].append(v)
   if rid==l.get('fixed_room') or rid==t.get('default_room'):reward.append(v*10)
  if not candidates:missing.append({'lesson':l['id'],'teacher':t['name'],'date':r['date'],'campus':r['campus']})
  m.add(sum(candidates)==1)
 for vs in occupancy.values():m.add(sum(vs)<=1)
 for key,vs in teacher_room.items():
  active=m.new_bool_var('roomuse'+str(key));m.add(sum(vs)<=7*active);reward.append(-100*active)
 # Hall cuts identify the exact set of simultaneous events competing for too
 # few rooms. Feed them back into time placement instead of double-booking.
 cuts=[];buckets=defaultdict(list)
 for i,r in enumerate(rows):
  if not r.get('remote'):buckets[r['date'],r['slot'],r['campus']].append(i)
 options={i:{rid for j,rid in y if j==i} for i in range(len(rows))}
 for (day,slot,campus),indices in buckets.items():
  matched={}
  def augment(i,seen):
   for rid in options[i]:
    if rid in seen:continue
    seen.add(rid)
    if rid not in matched or augment(matched[rid],seen):matched[rid]=i;return True
   return False
  for i in indices:augment(i,set())
  unmatched=set(indices)-set(matched.values())
  if unmatched:
   left=set(unmatched);right=set();pending=list(left)
   while pending:
    i=pending.pop()
    for rid in options[i]:
     right.add(rid)
     if rid in matched and matched[rid] not in left:left.add(matched[rid]);pending.append(matched[rid])
   cuts.append({'date':day,'slot':slot,'campus':campus,'lesson_ids':[rows[i]['lesson_id'] for i in left],'room_ids':sorted(right),'maximum':len(right)})
 if cuts:
  previous=OUT/'room_conflict_cuts.json';old=json.loads(previous.read_text(encoding='utf8')) if previous.exists() else []
  previous.write_text(json.dumps(old+cuts,ensure_ascii=False,indent=2),encoding='utf8')
 m.maximize(sum(reward));solver=cp_model.CpSolver();solver.parameters.max_time_in_seconds=60;solver.parameters.num_search_workers=8;status=solver.solve(m)
 report['room_status']=solver.status_name(status);report['no_compatible_rooms']=missing;report['room_conflicts']=cuts
 if status in [cp_model.FEASIBLE,cp_model.OPTIMAL]:
  for (i,rid),v in y.items():
   if solver.value(v):rows[i]['room_id']=rid;rows[i]['room_name']=rooms[rid]['name']
  report['rooms_checked']=True;(OUT/'verified_assignments.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf8')
 else:report['passed']=False
 (OUT/'strict_audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps(report,ensure_ascii=False))
if __name__=='__main__':main()
