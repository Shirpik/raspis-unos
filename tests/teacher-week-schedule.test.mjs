import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import path from 'node:path'
import {spawn,spawnSync} from 'node:child_process'
import net from 'node:net'

test('weekly teacher tiles are hard, date-scoped, and fall back outside the week',async()=>{
 const executable=path.resolve(process.env.SCOPED_TEST_SOLVER||'build/Release/timetable_solver.exe')
 const dir=await fs.mkdtemp(path.resolve('.tmp/teacher-week-test-'))
 await fs.mkdir(path.join(dir,'data'))
 const work_days=Array.from({length:7},(_,i)=>({day:i+1,enabled:i<6,start_slot:1,end_slot:7}))
 const weekly=[{week_from:'2026-10-19',work_days:[{day:5,slots:[]},{day:6,slots:[2,3]}]}]
 const data={settings:{semester_start_date:'2026-09-01',semester_end_date:'2026-10-31',start_date:'2026-09-01',end_date:'2026-10-31',require_class_hours:false,solver_config:{solver_workers:1,week_time_limit_seconds:5,quality_improvement_seconds:0,stop_after_first_solution:true,min_student_pairs_per_study_day:2,max_student_pairs_per_day:4,allow_single_pair_day_fallback:true,hard_no_student_windows:true,hard_no_teacher_windows:true}},groups:[{id:0,name:'TEST-301',parts:1,class_hour_enabled:false,work_days}],teachers:[{id:0,name:'Teacher',work_days,week_schedule_overrides:weekly}],rooms:[{id:0,name:'Room',campus:0,active:true,work_days}],lessons:[{id:0,name:'Subject',subject_id:0,teacher:0,group:0,subgroup:-1,total_hours:4,total_slots:2,generation_active:true,plan_active:true,curriculum_active:true,allowed_campuses:[0],fixed_room:-1,required_room_type:0}],teaching_ledger:[]}
 const run=async(date,name)=>{
  await fs.writeFile(path.join(dir,'data/timetable_data.json'),JSON.stringify(data))
  return spawnSync(executable,['--generate','--scope-from',date,'--scope-to',date,'--output',name],{cwd:dir,encoding:'utf8',windowsHide:true,timeout:30000,maxBuffer:5e6})
 }
 const closed=await run('2026-10-23','closed')
 assert.notEqual(closed.status,0,closed.stdout+'\n'+closed.stderr)
 assert.match(closed.stdout+closed.stderr,/PREFLIGHT_INFEASIBLE|INFEASIBLE/)
 const saturday=await run('2026-10-24','saturday')
 assert.equal(saturday.status,0,saturday.stdout+'\n'+saturday.stderr)
 const schedule=JSON.parse(await fs.readFile(path.join(dir,'saturday/schedule_all.json'),'utf8'))
 assert.deepEqual(schedule.groups[0].days[0].slots.filter(s=>s.lessons?.length).map(s=>s.slot),[2,3])
 const audit=JSON.parse(await fs.readFile(path.join(dir,'saturday/strict_audit.json'),'utf8'))
 assert.equal(audit.ok,true,JSON.stringify(audit))
 const nextWeek=await run('2026-10-30','next-week')
 assert.equal(nextWeek.status,0,nextWeek.stdout+'\n'+nextWeek.stderr)
 data.teachers[0].date_slot_overrides=[{date:'2026-10-23',slots:[4,5]}]
 const exactDate=await run('2026-10-23','exact-date')
 assert.equal(exactDate.status,0,exactDate.stdout+'\n'+exactDate.stderr)
 const exactSchedule=JSON.parse(await fs.readFile(path.join(dir,'exact-date/schedule_all.json'),'utf8'))
 assert.deepEqual(exactSchedule.groups[0].days[0].slots.filter(s=>s.lessons?.length).map(s=>s.slot),[4,5])
 const saved=JSON.parse(await fs.readFile(path.join(dir,'data/timetable_data.json'),'utf8'))
 assert.deepEqual(saved.teachers[0].week_schedule_overrides,weekly)
 assert.deepEqual(saved.teaching_ledger,[])
})

test('teacher API persists and clears a weekly grid without altering the ordinary graph or other teachers',async()=>{
 const executable=path.resolve(process.env.SCOPED_TEST_SOLVER||'build/Release/timetable_solver.exe')
 const dir=await fs.mkdtemp(path.resolve('.tmp/teacher-week-api-'))
 await fs.mkdir(path.join(dir,'data'))
 await fs.writeFile(path.join(dir,'data/timetable_data.json'),JSON.stringify({settings:{start_date:'2026-09-01',end_date:'2026-12-19'},groups:[],teachers:[{id:0,name:'First teacher'},{id:1,name:'Other teacher'}],rooms:[],lessons:[],teaching_ledger:[]}))
 await fs.writeFile(path.join(dir,'data/auth_config.json'),JSON.stringify({username:'fake_grid',password_salt:'00112233445566778899aabbccddeeff',password_hash:'b866e38c02150905dcadf54bf7eddfd833b09c541192b974ea5ea48bd64680d8',iterations:150000}))
 const socket=net.createServer()
 await new Promise(resolve=>socket.listen(0,'127.0.0.1',resolve))
 const port=socket.address().port
 await new Promise(resolve=>socket.close(resolve))
 const child=spawn(executable,[String(port)],{cwd:dir,windowsHide:true,stdio:'ignore'})
 const base=`http://127.0.0.1:${port}`
 try{
  let ready=false
  for(let i=0;i<80;i++){
   try{const status=await fetch(base+'/api/auth/status');if(status.ok){ready=true;break}}catch{}
   await new Promise(resolve=>setTimeout(resolve,50))
  }
  assert(ready,'isolated server must start')
  const login=await fetch(base+'/api/auth/login',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({username:'fake_grid',password:'fake-grid-pass'})})
  assert.equal(login.status,200)
  const cookie=login.headers.get('set-cookie').split(';')[0]
  const call=async(method,route,body)=>{
   const response=await fetch(base+route,{method,headers:{cookie,'content-type':'application/json'},...(body?{body:JSON.stringify(body)}:{})})
   assert(response.ok,`${method} ${route}: ${response.status}`)
   return response.json()
  }
  const before=await call('GET','/api/teachers/0'),other=await call('GET','/api/teachers/1')
  const weeks=[{week_from:'2026-10-19',work_days:Array.from({length:7},(_,i)=>({day:i+1,slots:[]}))}]
  await call('PUT','/api/teachers/0',{week_schedule_overrides:weeks})
  const reread=await call('GET','/api/teachers/0')
  assert.deepEqual(reread.week_schedule_overrides,weeks)
  assert.deepEqual(reread.work_days,before.work_days)
  assert.deepEqual(reread.date_slot_overrides,before.date_slot_overrides)
  assert.deepEqual(await call('GET','/api/teachers/1'),other)
  const disk=JSON.parse(await fs.readFile(path.join(dir,'data/timetable_data.json'),'utf8'))
  assert.deepEqual(disk.teachers.find(t=>t.id===0).week_schedule_overrides,weeks)
  await call('PUT','/api/teachers/0',{week_schedule_overrides:[]})
  const cleared=await call('GET','/api/teachers/0')
  assert.deepEqual(cleared.week_schedule_overrides,[])
  assert.deepEqual(cleared.work_days,before.work_days)
 }finally{
  child.kill()
  await new Promise(resolve=>child.exitCode!==null?resolve():child.once('exit',resolve))
 }
})
