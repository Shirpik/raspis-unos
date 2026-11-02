import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import path from 'node:path'
import {spawnSync} from 'node:child_process'

test('replacement credit can stay with absent teacher while actual teacher stays visible',async()=>{
 const dir=await fs.mkdtemp(path.resolve('.tmp/substitution-credit-'))
 await fs.mkdir(path.join(dir,'data'))
 const data={settings:{semester_start_date:'2026-09-01',semester_end_date:'2026-09-05',accounting_as_of_date:'2026-09-05'},groups:[{id:0,name:'TEST',parts:1}],teachers:[{id:0,name:'Absent'},{id:1,name:'Substitute'}],lessons:[{id:0,group:0,teacher:0,subgroup:-1,name:'Subject',total_hours:20}],teaching_ledger:[{id:0,lesson_id:0,group_id:0,date:'2026-09-02',status:'confirmed',hours:2,slot:1,actual_teacher:1,source_file:'week.xlsx'}],substitutions:[{id:0,lesson_id:0,date:'2026-09-02',slot:1,hours:2,absent_teacher:0,substitute_teacher:1,status:'active',credited_teacher:0}]}
 const run=async()=>{
  await fs.writeFile(path.join(dir,'data/timetable_data.json'),JSON.stringify(data))
  const r=spawnSync(path.resolve(process.env.CREDIT_TEST_SOLVER||'build/Release/timetable_solver.exe'),['--hours-report'],{cwd:dir,encoding:'utf8',windowsHide:true,maxBuffer:5e6})
  assert.equal(r.status,0,r.stderr)
  return JSON.parse(r.stdout)
 }
 let r=await run()
 assert.equal(r.teachers.find(t=>t.teacher_id===0).credited_hours,2)
 assert.equal(r.teachers.find(t=>t.teacher_id===1).credited_hours,0)
 const e=r.lessons[0].scheduled_occurrences[0]
 assert.equal(e.actual_teacher_id,1)
 assert.equal(e.credited_teacher_id,0)
 assert.equal(e.is_substitution,true)
 assert.equal(r.teachers.find(t=>t.teacher_id===0).credited_occurrences.length,1)
 assert.equal(r.teachers.find(t=>t.teacher_id===1).credited_occurrences.length,0)
 delete data.substitutions[0].credited_teacher
 r=await run()
 assert.equal(r.teachers.find(t=>t.teacher_id===0).credited_hours,2)
 assert.equal(r.teachers.find(t=>t.teacher_id===1).credited_hours,0)
 data.substitutions=[]
 r=await run()
 assert.equal(r.teachers.find(t=>t.teacher_id===0).credited_hours,2)
 assert.equal(r.teachers.find(t=>t.teacher_id===1).credited_hours,0)
 assert.equal(r.lessons[0].scheduled_occurrences[0].actual_teacher_id,1)
})
