import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import path from 'node:path'
import {spawnSync} from 'node:child_process'

test('a scoped range can choose remaining zero-quota UP on every selected study day',async()=>{
 const executable=path.resolve(process.env.SCOPED_TEST_SOLVER||'build/Release/timetable_solver.exe'),dir=await fs.mkdtemp(path.resolve('.tmp/range-practice-test-'))
 await fs.mkdir(path.join(dir,'data'))
 const days=Array.from({length:7},(_,i)=>({day:i+1,enabled:i<6,start_slot:1,end_slot:7}))
 const data={settings:{semester_start_date:'2026-09-01',semester_end_date:'2026-12-19',start_date:'2026-09-01',end_date:'2026-12-19',require_class_hours:false,solver_config:{solver_workers:1,week_time_limit_seconds:10,quality_improvement_seconds:0,min_student_pairs_per_study_day:2,hard_no_student_windows:true,hard_no_teacher_windows:true}},groups:[{id:0,name:'TEST-301',parts:1,class_hour_enabled:false,work_days:days}],teachers:[{id:0,name:'Teacher',work_days:days}],rooms:[{id:0,name:'101',campus:0,active:true,work_days:days}],lessons:[{id:0,uid:'up',name:'УП.01',group:0,teacher:0,subgroup:-1,is_block:true,is_pp:false,total_hours:12,total_slots:0,generation_active:false,plan_active:true,curriculum_active:true,allowed_campuses:[0],fixed_room:-1,required_room_type:0}],teaching_ledger:[]}
 await fs.writeFile(path.join(dir,'data/timetable_data.json'),JSON.stringify(data))
 const run=spawnSync(executable,['--generate','--scope-from','2026-10-21','--scope-to','2026-10-22','--output','result'],{cwd:dir,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:5e6})
 assert.equal(run.status,0,run.stdout+'\n'+run.stderr)
 const schedule=JSON.parse(await fs.readFile(path.join(dir,'result/schedule_all.json'),'utf8'))
 const daysResult=schedule.groups[0].days,events=daysResult.flatMap(d=>d.slots.flatMap(s=>s.lessons||[]))
 assert.equal(events.length,2);assert(events.every(e=>e.id===0&&e.is_block));assert.deepEqual(daysResult.map(d=>d.date_iso),['2026-10-21','2026-10-22'])
 const audit=JSON.parse(await fs.readFile(path.join(dir,'result/strict_audit.json'),'utf8'));assert.equal(audit.ok,true)
 const saved=JSON.parse(await fs.readFile(path.join(dir,'data/timetable_data.json'),'utf8'))
 assert.deepEqual(saved.teaching_ledger,[])
 assert.equal(saved.lessons[0].generation_active,false)
 assert.equal(saved.lessons[0].total_slots,0)
 assert.equal(saved.lessons[0].total_hours,12)
})

test('an unavailable required study day gives a concrete group and date before CP-SAT',async()=>{
 const executable=path.resolve(process.env.SCOPED_TEST_SOLVER||'build/Release/timetable_solver.exe'),dir=await fs.mkdtemp(path.resolve('.tmp/scoped-empty-day-'));await fs.mkdir(path.join(dir,'data'))
 const days=Array.from({length:7},(_,i)=>({day:i+1,enabled:i<6,start_slot:1,end_slot:7}))
 const data={settings:{semester_start_date:'2026-09-01',semester_end_date:'2026-12-19',start_date:'2026-09-01',end_date:'2026-12-19',solver_config:{min_student_pairs_per_study_day:2}},groups:[{id:0,name:'TEST-301',parts:1,work_days:days}],teachers:[{id:0,name:'Teacher',work_days:days.map(d=>({...d,enabled:d.day!==2&&d.enabled}))}],rooms:[],lessons:[{id:0,name:'Subject',teacher:0,group:0,subgroup:-1,total_hours:36,total_slots:1,generation_active:true,plan_active:true,curriculum_active:true}],teaching_ledger:[]}
 await fs.writeFile(path.join(dir,'data/timetable_data.json'),JSON.stringify(data))
 const run=spawnSync(executable,['--generate','--scope-from','2026-10-20','--scope-to','2026-10-21','--output','result'],{cwd:dir,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:5e6});assert.notEqual(run.status,0)
 assert.match(run.stdout+run.stderr,/TEST-301, 2026-10-20/)
 const report=JSON.parse(await fs.readFile(path.join(dir,'result/solver_preflight.json'),'utf8'));assert.equal(report.ok,false);assert.equal(report.issues[0].code,'group_day_no_candidates')
})
