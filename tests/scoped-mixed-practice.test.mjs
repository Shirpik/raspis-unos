import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import path from 'node:path'
import {spawnSync} from 'node:child_process'

test('a dated mixed-practice exception preserves UP and later labs without false windows', async()=>{
 const executable=path.resolve(process.env.SCOPED_TEST_SOLVER || 'build/Release/timetable_solver.exe')
 const root=await fs.mkdtemp(path.resolve('.tmp/scoped-mixed-practice-'))
 await fs.mkdir(path.join(root,'data'))
 const days=Array.from({length:7},(_,i)=>({day:i+1,enabled:i<6,start_slot:1,end_slot:7}))
 const lesson={uid:'',group:0,subgroup:0,teacher:0,name:'УП.01',subject_id:0,total_hours:6,total_slots:0,plan_active:true,curriculum_active:true,generation_active:false,is_block:true,is_pp:false,is_lab:false,consecutive_pairs:1,allowed_campuses:[0],fixed_room:-1,required_room_type:0,required_room_purpose:'',allow_room_substitution:true}
 const data={settings:{start_date:'2026-10-21',end_date:'2026-12-19',semester_start_date:'2026-09-01',semester_end_date:'2026-12-19',semester_weeks:16,automatic_period_quotas:true,require_class_hours:false,solver_config:{solver_workers:1,week_time_limit_seconds:10,solver_time_limit_seconds:10,quality_improvement_seconds:0,stop_after_first_solution:true,max_student_pairs_per_day:5,min_student_pairs_per_study_day:2,hard_no_student_windows:true,hard_no_teacher_windows:true}},teachers:[0,1].map(id=>({id,name:`Teacher ${id}`,scheduling_active:true,allowed_campuses:[0],work_days:days})),groups:[{id:0,name:'TEST-3101',parts:2,class_hour_enabled:false,mixed_up_dates:['2026-10-21'],work_days:days}],rooms:[0,1,2].map(id=>({id,name:`Room ${id}`,campus:0,active:true,access_mode:'general',room_type:0,purpose:'',capacity:0,work_days:days})),lessons:[{...lesson,id:0,uid:'up-am'},{...lesson,id:1,uid:'up-pm',subgroup:1,teacher:1},{...lesson,id:2,uid:'lab',subgroup:1,teacher:1,is_block:false,is_lab:true,name:'ЛПЗ Технология',subject_id:1,total_hours:4,total_slots:2,generation_active:true}],teaching_ledger:[],unavailable:[],teacher_unavailable:[]}
 const locks={source:'manual',assignments:[{lesson_id:0,slot:0},{lesson_id:1,slot:2},{lesson_id:2,slot:5},{lesson_id:2,slot:6}].map(r=>({...r,date:'2026-10-21'}))}
 const run=async(name)=>{
  await fs.writeFile(path.join(root,'data/timetable_data.json'),JSON.stringify(data))
  await fs.writeFile(path.join(root,'locks.json'),JSON.stringify(locks))
  const result=spawnSync(executable,['--generate','--scope-from','2026-10-21','--scope-to','2026-10-21','--locks','locks.json','--output',name],{cwd:root,encoding:'utf8',timeout:60000,windowsHide:true})
  await fs.writeFile(path.join(root,`${name}.log`),`${result.stdout}\n${result.stderr}`)
  return result
 }
 const allowed=await run('allowed')
 assert.equal(allowed.status,0,`${allowed.error || ''}\n${allowed.stdout}\n${allowed.stderr}\n${root}`)
 const schedule=JSON.parse(await fs.readFile(path.join(root,'allowed/schedule_all.json'),'utf8'))
 const actual=schedule.groups.flatMap(g=>g.days.flatMap(d=>d.slots.flatMap(s=>(s.lessons||[]).map(l=>`${l.id}|${d.date_iso}|${s.slot-1}`))))
 for(const l of locks.assignments)assert(actual.includes(`${l.lesson_id}|${l.date}|${l.slot}`))
 const quality=JSON.parse(await fs.readFile(path.join(root,'allowed/quality_report.json'),'utf8'))
 assert.equal(quality.final_validated,true)
 assert.equal(quality.student_windows,0)
 assert.equal(quality.teacher_windows,0)
 assert.equal(quality.up_day_rule_violations,0)
 const audit=JSON.parse(await fs.readFile(path.join(root,'allowed/strict_audit.json'),'utf8'))
 assert.equal(audit.ok,true,JSON.stringify(audit))
 data.groups[0].mixed_up_dates=['2026-10-22']
 const wrongDate=await run('wrong-date')
 assert.notEqual(wrongDate.status,0,'an exception must not apply on another day')
 assert.match(wrongDate.stdout+wrongDate.stderr,/INFEASIBLE/)
 assert.doesNotMatch(wrongDate.stderr,/Требование пары сразу после классного часа/)
 data.groups[0].mixed_up_dates=['2026-10-21']
 data.settings.solver_config.max_student_pairs_per_day=4
 const overloaded=await run('overload')
 assert.notEqual(overloaded.status,0,'six practice hours plus two ordinary pairs require a five-pair limit')
 assert.match(overloaded.stdout+overloaded.stderr,/INFEASIBLE/)
 data.settings.solver_config.max_student_pairs_per_day=5
 locks.assignments[2].slot=3
 locks.assignments[3].slot=4
 const overlapping=await run('overlap')
 assert.notEqual(overlapping.status,0,'ordinary pairs must not reuse the full UP interval')
 assert.match(overlapping.stdout+overlapping.stderr,/INFEASIBLE/)
})
