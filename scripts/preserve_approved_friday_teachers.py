"""Keep teacher ownership for lesson IDs already approved on Friday."""
import json, sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
data_path=root/'data/timetable_data.json'
data=json.loads(data_path.read_text(encoding='utf-8-sig'))
old=json.loads(Path(sys.argv[1]).read_text(encoding='utf-8-sig'))
schedule=json.loads(Path(sys.argv[2]).read_text(encoding='utf-8-sig'))
friday={e['id'] for g in schedule['groups'] for day in g['days'] if day.get('date_iso')=='2026-09-04' for slot in day['slots'] for e in slot.get('lessons',[]) if e.get('id',-1)>=0}
old_by={x['id']:x for x in old['lessons']}; changed=0
for x in data['lessons']:
    if x['id'] in friday and x['id'] in old_by and x.get('teacher')!=old_by[x['id']].get('teacher'):
        x['teacher']=old_by[x['id']]['teacher']; changed+=1
data_path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'friday_lesson_ids':len(friday),'teacher_assignments_preserved':changed}))
