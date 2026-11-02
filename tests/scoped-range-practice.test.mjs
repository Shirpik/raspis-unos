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

for(const dailyLimit of [4,5]) test(`Constructor UP contributes three academic pairs with daily limit ${dailyLimit}`, async()=>{
 const executable=path.resolve(process.env.SCOPED_TEST_SOLVER||'build/Release/timetable_solver.exe')
 const dir=await fs.mkdtemp(path.resolve('.tmp/range-up-load-test-'))
 await fs.mkdir(path.join(dir,'data'))
 const work_days=Array.from({length:7},(_,i)=>({day:i+1,enabled:i<6,start_slot:1,end_slot:7}))
 const ordinary={group:0,subgroup:-1,teacher:1,total_hours:4,total_slots:2,generation_active:true,plan_active:true,curriculum_active:true,is_block:false,is_pp:false,allowed_campuses:[0],fixed_room:-1,required_room_type:0}
 const data={
  settings:{semester_start_date:'2026-09-01',semester_end_date:'2026-10-24',start_date:'2026-09-01',end_date:'2026-10-24',require_class_hours:false,solver_config:{solver_workers:1,week_time_limit_seconds:10,quality_improvement_seconds:0,stop_after_first_solution:true,max_student_pairs_per_day:dailyLimit,min_student_pairs_per_study_day:2,hard_no_student_windows:true,hard_no_teacher_windows:true}},
  groups:[{id:0,name:'TEST-301',parts:2,class_hour_enabled:false,work_days}],
  teachers:[0,1].map(id=>({id,name:`Teacher ${id}`,work_days})),
  rooms:[0,1].map(id=>({id,name:`Room ${id}`,campus:0,active:true,work_days})),
  lessons:[0,1].map(id=>({...ordinary,id,uid:`up-${id}`,name:'УП.01',teacher:0,subgroup:id,is_block:true,total_hours:6,total_slots:0,generation_active:false})).concat(
   [2,3,4,5].map(id=>({...ordinary,id,uid:`ordinary-${id}`,subject_id:id,name:`Subject ${id}`}))),
  teaching_ledger:[]
 }
 const locks={source:'manual',assignments:[{lesson_id:0,date:'2026-10-23',slot:0},{lesson_id:1,date:'2026-10-23',slot:2}]}
 await fs.writeFile(path.join(dir,'data/timetable_data.json'),JSON.stringify(data))
 await fs.writeFile(path.join(dir,'locks.json'),JSON.stringify(locks))
 const run=spawnSync(executable,['--generate','--scope-from','2026-10-23','--scope-to','2026-10-24','--locks','locks.json','--output','result'],{cwd:dir,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:5e6})
 assert.equal(run.status,0,run.stdout+'\n'+run.stderr)
 assert.doesNotMatch(run.stdout+run.stderr,/вЂ|В«|В»|\uFFFD/,'diagnostics must contain readable UTF-8')
 const schedule=JSON.parse(await fs.readFile(path.join(dir,'result/schedule_all.json'),'utf8'))
 const dates=schedule.groups[0].days
 assert.deepEqual(dates.map(d=>d.date_iso),['2026-10-23','2026-10-24'])
 const events=dates.flatMap(d=>d.slots.flatMap(s=>(s.lessons||[]).map(l=>({id:l.id,date:d.date_iso,slot:s.slot-1}))))
 for(const lock of locks.assignments)assert(events.some(e=>e.id===lock.lesson_id&&e.date===lock.date&&e.slot===lock.slot))
 assert.equal(events.filter(e=>e.date==='2026-10-24').length,dailyLimit)
 const audit=JSON.parse(await fs.readFile(path.join(dir,'result/strict_audit.json'),'utf8'))
 assert.equal(audit.ok,true,JSON.stringify(audit))
 const quality=JSON.parse(await fs.readFile(path.join(dir,'result/quality_report.json'),'utf8'))
 assert.equal(quality.student_windows,0)
 assert.equal(quality.teacher_windows,0)
 const saved=JSON.parse(await fs.readFile(path.join(dir,'data/timetable_data.json'),'utf8'))
 assert.deepEqual(saved.teaching_ledger,[])
 for(const original of data.lessons){
  const lesson=saved.lessons.find(l=>l.id===original.id)
  for(const key of ['total_hours','total_slots','generation_active'])assert.equal(lesson[key],original[key])
 }
})

for(const entity of ['student','teacher']) test(`a locked ${entity} window fails even with the daily-minimum fallback enabled`,async()=>{
 const executable=path.resolve(process.env.SCOPED_TEST_SOLVER||'build/Release/timetable_solver.exe')
 const dir=await fs.mkdtemp(path.resolve(`.tmp/hard-${entity}-window-`))
 await fs.mkdir(path.join(dir,'data'))
 const work_days=Array.from({length:7},(_,i)=>({day:i+1,enabled:i<6,start_slot:1,end_slot:7}))
 const data={
  settings:{semester_start_date:'2026-09-01',semester_end_date:'2026-10-24',start_date:'2026-09-01',end_date:'2026-10-24',require_class_hours:false,solver_config:{solver_workers:1,week_time_limit_seconds:5,quality_improvement_seconds:0,stop_after_first_solution:true,min_student_pairs_per_study_day:1,max_student_pairs_per_day:4,allow_single_pair_day_fallback:true,hard_no_student_windows:entity==='student',hard_no_teacher_windows:entity==='teacher'}},
  groups:(entity==='student'?[0]:[0,1]).map(id=>({id,name:`TEST-${id}`,parts:1,class_hour_enabled:false,work_days})),
  teachers:[{id:0,name:'Teacher',work_days}],rooms:[{id:0,name:'101',campus:0,active:true,work_days}],
  lessons:[0,1].map(id=>({id,name:`Subject ${id}`,subject_id:id,uid:`lesson-${id}`,group:entity==='student'?0:id,subgroup:-1,teacher:0,total_hours:2,total_slots:1,generation_active:true,plan_active:true,curriculum_active:true,allowed_campuses:[0],fixed_room:-1,required_room_type:0})),teaching_ledger:[]
 }
 await fs.writeFile(path.join(dir,'locks.json'),JSON.stringify({source:'manual',assignments:[{lesson_id:0,date:'2026-10-23',slot:0},{lesson_id:1,date:'2026-10-23',slot:2}]}))
 const run=async(output)=>{
  await fs.writeFile(path.join(dir,'data/timetable_data.json'),JSON.stringify(data))
  return spawnSync(executable,['--generate','--scope-from','2026-10-23','--scope-to','2026-10-23','--locks','locks.json','--output',output],{cwd:dir,encoding:'utf8',windowsHide:true,timeout:30000,maxBuffer:5e6})
 }
 const hard=await run('hard')
 assert.notEqual(hard.status,0,hard.stdout+'\n'+hard.stderr)
 assert.match(hard.stdout+hard.stderr,/INFEASIBLE/)
 // A soft control proves that the failure is caused by the window rule.
 data.settings.solver_config.hard_no_student_windows=false
 data.settings.solver_config.hard_no_teacher_windows=false
 const soft=await run('soft')
 assert.equal(soft.status,0,soft.stdout+'\n'+soft.stderr)
 const quality=JSON.parse(await fs.readFile(path.join(dir,'soft/quality_report.json'),'utf8'))
 assert(quality[`${entity}_windows`]>0)
})
