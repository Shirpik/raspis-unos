"""Student loads count physical subgroups, with optional daily preferences."""
import copy
import sys
from pathlib import Path
from test_quota_study_days import run

exe=Path(sys.argv[1]).resolve()
row=dict(id=0,teacher=0,group=0,minimum=0,maximum=4,semester_total=32,
         parts=[0],part_weight=1,whole_group=True,allowed_slots=list(range(7)),
         allowed_campuses=[0],subject='0')
m=dict(variables=[row],parts=[dict(key=0,lesson_ids=[0],minimum_target=2,maximum_target=4)],
       teachers=[dict(id=0,minimum=0,maximum=4)],lab_rules=[],day_count=1,slots_per_day=7,
       distribution_weeks=16,min_student_pairs_per_day=2,max_student_pairs_per_day=4,
       whole_group_same_subject_limit=4,physical_part_same_subject_limit=4,
       hard_no_student_windows=True,workers=1,time_limit_seconds=5)
assert run(exe,m)['quotas']['0']==2
m['preferred_student_pairs_per_day']=3
assert run(exe,m)['quotas']['0']==3
over=copy.deepcopy(m);over['variables'][0].update(minimum=5,maximum=5)
assert run(exe,over)['status']=='INFEASIBLE'
gap=copy.deepcopy(m);gap['variables'][0].update(minimum=2,maximum=2,allowed_slots=[0,2])
gap.update(hard_no_student_windows=False,hard_no_teacher_windows=True)
assert run(exe,gap)['status']=='INFEASIBLE'
split=copy.deepcopy(m)
split['variables']=[dict(row,id=0,minimum=3,maximum=3,parts=[0],whole_group=False,allowed_slots=[0,1,2]),
                    dict(row,id=1,teacher=1,minimum=3,maximum=3,parts=[1],whole_group=False,allowed_slots=[4,5,6]),
                    dict(row,id=2,teacher=2,minimum=1,maximum=1,parts=[0,1],allowed_slots=[3])]
split['parts']=[dict(key=i,lesson_ids=[i,2],target=4) for i in [0,1]]
split['teachers']=[dict(id=i,minimum=0,maximum=4) for i in range(3)]
result=run(exe,split)
assert result['success']
assert len({t for times in result['placement_witness'].values() for t in times})==7
campus=copy.deepcopy(m)
campus['teacher_day_campuses']=[dict(teacher=0,day=0,campus=1)]
assert run(exe,campus)['status']=='INFEASIBLE'
balanced=copy.deepcopy(m)
balanced.pop('preferred_student_pairs_per_day')
balanced['variables'][0]['semester_total']=64
balanced['teachers'][0].update(minimum=2,hard_minimum=0)
balanced.update(allow_teacher_shortfalls=True,teacher_shortfall_weight=1000,teacher_overload_weight=1000)
assert run(exe,balanced)['quotas']['0']==2
hall=copy.deepcopy(m)
hall['variables'][0].update(sports_room=True,allowed_campuses=[1],allowed_slots=[0,1])
hall.update(room_capacity_by_campus=[7,7],sports_capacity_by_campus=[1,1])
assert run(exe,hall)['success']
hall['sports_capacity_by_time']=[[1,0] for _ in range(7)]
assert run(exe,hall)['status']=='INFEASIBLE'
computer=copy.deepcopy(m)
computer['variables'][0].update(computer_room=True,allowed_campuses=[1])
computer.update(room_capacity_by_campus=[7,7],computer_capacity_by_campus=[3,0])
assert run(exe,computer)['status']=='INFEASIBLE'
computer['computer_capacity_by_campus']=[0,1]
assert run(exe,computer)['success']
lunch=copy.deepcopy(m)
lunch['variables'][0].update(allowed_slots=[1,2],minimum=2,maximum=2,avoid_lunch_split=True)
assert run(exe,lunch)['status']=='INFEASIBLE'
lunch['variables'][0]['allowed_slots']=[2,3]
assert run(exe,lunch)['success']
blocks=copy.deepcopy(m)
blocks['variables'][0].update(consecutive_pairs=2,minimum=2,maximum=2,allowed_slots=[3,4],block_start_slots=[0,2,4,5])
assert run(exe,blocks)['status']=='INFEASIBLE'
blocks['variables'][0]['allowed_slots']=[2,3]
assert run(exe,blocks)['success']
timed_room=copy.deepcopy(m)
timed_room['room_capacity_by_time']=[[0,1] for _ in range(7)]
assert run(exe,timed_room)['status']=='INFEASIBLE'
deadline=copy.deepcopy(m)
deadline.pop('preferred_student_pairs_per_day')
deadline['variables'][0].update(semester_total=32,distribution_weeks=8)
assert run(exe,deadline)['quotas']['0']==4
exception=copy.deepcopy(m)
exception['physical_part_same_subject_limit']=3
exception['variables'][0].update(minimum=4,maximum=4,whole_group=False,daily_subject_limits=[dict(day=0,maximum=4)])
assert run(exe,exception)['success']
exception['variables'][0]['daily_subject_limits']=[dict(day=1,maximum=4)]
assert run(exe,exception)['status']=='INFEASIBLE'
scaled=copy.deepcopy(deadline)
scaled['variables'][0]['target_pairs_milli']=3000
assert run(exe,scaled)['quotas']['0']==3
scaled['lesson_load_targets']=[dict(lesson_ids=[0],maximum=2)]
assert run(exe,scaled)['quotas']['0']==2
scaled['placement_hints']={'0':[0,1,2]}
assert run(exe,scaled)['quotas']['0']==2  # A conflicting old hint must not become a lock.
scaled['feasibility_only']=True
assert run(exe,scaled)['quotas']['0']==2
scaled['lesson_load_targets'][0]['maximum']=1
assert run(exe,scaled)['status']=='INFEASIBLE'
print('Daily load/window/room/deadline/dated subject limit and scaled quota/aggregate cap regressions passed')
