"""Create Friday locks and merge a regenerated Saturday over approved Friday."""
import argparse, json
from pathlib import Path

def read(p): return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def write(p,v):
    p=Path(p); p.parent.mkdir(parents=True,exist_ok=True)
    p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

ap=argparse.ArgumentParser(); ap.add_argument('mode',choices=['locks','merge']); ap.add_argument('--original',required=True); ap.add_argument('--candidate'); ap.add_argument('--output',required=True)
a=ap.parse_args(); original=read(a.original)
if a.mode=='locks':
    assignments=[]
    for g in original['groups']:
        for day in g['days']:
            # Friday is approved in full. Group 27 additionally keeps its
            # four-pair Saturday block so none of it can spill into Friday.
            if day.get('date_iso')!='2026-09-04' and not (g.get('group_index')==27 and day.get('date_iso')=='2026-09-05'): continue
            for slot in day['slots']:
                for lesson in slot.get('lessons',[]):
                    if lesson.get('id',-1)>=0: assignments.append({'lesson_id':lesson['id'],'date':day['date_iso'],'slot':slot['slot']-1})
    write(a.output,{'source':'approved-friday-only','assignments':assignments}); print(json.dumps({'locks':len(assignments),'output':a.output}))
else:
    candidate=read(a.candidate); cgroups={g['group_index']:g for g in candidate['groups']}
    for g in original['groups']:
        friday=[d for d in g['days'] if d.get('date_iso')=='2026-09-04']
        saturday=[d for d in cgroups[g['group_index']]['days'] if d.get('date_iso')=='2026-09-05']
        if not saturday:
            # The solver omits a completely empty day for a group. Preserve the
            # existing empty Saturday grid so the Excel template remains whole.
            saturday=[d for d in g['days'] if d.get('date_iso')=='2026-09-05']
        if not saturday:
            times=['1 пара (08:30-09:45)','2 пара (09:55-11:10)','3 пара (11:30-12:45)','4 пара (12:55-14:10)','5 пара (14:20-15:30)','6 пара (15:40-16:50)','7 пара (17:00-18:10)']
            saturday=[{'day_index':1,'date':'05.09.2026','date_iso':'2026-09-05','weekday':'СБ','slots':[{'slot':i+1,'time':t,'text':'-','lessons':[]} for i,t in enumerate(times)]}]
        if not friday:
            times=['1 пара (08:30-09:55)','2 пара (10:05-11:30)','3 пара (12:25-13:50)','4 пара (14:00-15:25)','5 пара (15:35-16:55)','6 пара (17:05-18:25)','7 пара (18:35-19:55)']
            friday=[{'day_index':0,'date':'04.09.2026','date_iso':'2026-09-04','weekday':'ПТ','slots':[{'slot':i+1,'time':t,'text':'-','lessons':[]} for i,t in enumerate(times)]}]
        if len(friday)!=1 or len(saturday)!=1:
            raise RuntimeError(f"Дни не найдены: {g['group_name']}")
        g['days']=friday+saturday
    original['date_from']='04.09.2026'; original['date_to']='05.09.2026'
    write(a.output,original); print(json.dumps({'groups':len(original['groups']),'output':a.output}))
