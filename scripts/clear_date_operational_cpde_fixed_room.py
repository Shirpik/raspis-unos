import json
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'data/timetable_data.json'
d=json.loads(p.read_text(encoding='utf-8-sig'))
pod=next(t['id'] for t in d['teachers'] if 'Подчинен' in t['name'])
count=0
for x in d['lessons']:
    if x.get('teacher')==pod and 'лпз' in x.get('name','').casefold():
        x['fixed_room']=-1; x['preferred_room']=68; count+=1
p.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'cleared_global_fixed_room':count,'saturday_output_keeps_cpde':True}))
