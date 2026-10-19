import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import path from 'node:path'
import { spawnSync } from 'node:child_process'

test('direct scoped day selects zero-quota remaining UP and skips an unavailable optional UP', async()=>{
 const executable=path.resolve(process.env.SCOPED_TEST_SOLVER || '.tmp/week8-class-hours-bin/timetable_solver.exe')
 const root=await fs.mkdtemp(path.resolve('.tmp/scoped-practice-test-'))
 await fs.mkdir(path.join(root,'data'))
 const days=Array.from({length:7},(_,i)=>({day:i+1,enabled:i<6,start_slot:1,end_slot:7}))
 const teachers=Array.from({length:4},(_,id)=>({id,name:`Teacher ${id}`,scheduling_active:true,allowed_campuses:[0],work_days:id===3?days.map(d=>({...d,enabled:false})):days}))
 const lessons=[0,1,2].map(id=>({id,uid:`up-${id}`,group:0,subgroup:id===1?1:0,teacher:id+1,name:`УП.0${id+1}`,subject_id:id,total_hours:12,total_slots:0,plan_active:true,curriculum_active:true,generation_active:false,is_block:true,is_pp:false,is_lab:false,consecutive_pairs:1,allowed_campuses:[0],fixed_room:-1,required_room_type:0,required_room_purpose:'',allow_room_substitution:true}))
 lessons.push({...lessons[0],id:3,uid:'ordinary-after-up',group:1,subgroup:-1,teacher:1,name:'Ordinary lab after UP',subject_id:3,is_block:false,is_lab:true,total_hours:4,total_slots:2,consecutive_pairs:2,block_start_slots:[5],generation_active:true})
 const data={settings:{start_date:'2026-10-19',end_date:'2026-12-19',semester_start_date:'2026-09-01',semester_end_date:'2026-12-19',semester_weeks:16,automatic_period_quotas:true,require_class_hours:true,solver_config:{solver_workers:1,solver_time_limit_seconds:10,week_time_limit_seconds:10,max_student_pairs_per_day:4,min_student_pairs_per_study_day:2,hard_no_student_windows:true,hard_no_teacher_windows:true}},teachers,groups:[{id:0,name:'TEST-3101',parts:2,curator_teacher:0,class_hour_enabled:true,work_days:days}],rooms:[0,1,2].map(id=>({id,name:`Room ${id}`,campus:0,active:true,access_mode:'general',room_type:0,purpose:'',capacity:0,work_days:days})),lessons,teaching_ledger:[],unavailable:[],teacher_unavailable:[]}
 data.groups.push({id:1,name:'TEST-3102',parts:2,class_hour_enabled:false,work_days:days})
 await fs.writeFile(path.join(root,'data/timetable_data.json'),JSON.stringify(data))
 const run=spawnSync(executable,['--generate','--scope-from','2026-10-19','--scope-to','2026-10-19','--output','result'],{cwd:root,encoding:'utf8',timeout:60000,windowsHide:true})
 await fs.writeFile(path.join(root,'run.log'),run.stdout+'\n'+run.stderr)
 assert.equal(run.status,0,`${run.error||''}\n${run.stdout}\n${run.stderr}\nArtifacts: ${root}`)
 const schedule=JSON.parse(await fs.readFile(path.join(root,'result/schedule_all.json'),'utf8'))
 const events=schedule.groups.flatMap(g=>g.days.flatMap(d=>d.slots.flatMap(s=>s.lessons||[])))
 assert.equal(events.filter(l=>l.is_class_hour).length,1)
 assert.deepEqual(events.filter(l=>!l.is_class_hour).map(l=>l.id).sort(),[0,1,3,3])
 const audit=JSON.parse(await fs.readFile(path.join(root,'result/strict_audit.json'),'utf8'))
 assert.equal(audit.ok,true,JSON.stringify(audit))
 const quality=JSON.parse(await fs.readFile(path.join(root,'result/quality_report.json'),'utf8'))
 assert.equal(quality.teacher_windows,0,'the complete UP interval must not create false teacher windows')
 assert.equal(quality.student_windows,0)
 assert.equal(quality.class_hours,1)
 assert.equal(quality.final_validated,true)
 // CLI preview must not mutate the supplied source data or its teaching ledger.
 const saved=JSON.parse(await fs.readFile(path.join(root,'data/timetable_data.json'),'utf8'))
 assert.deepEqual(saved.teaching_ledger,[])
 assert(saved.lessons.filter(l=>l.is_block).every(l=>l.generation_active===false&&l.total_slots===0))
})
