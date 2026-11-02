import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import path from 'node:path'
import {spawnSync} from 'node:child_process'

const date='2026-10-26'
const workDays=(slots=[1,2,3,4,5,6,7])=>Array.from({length:7},(_,i)=>({day:i+1,enabled:i<6&&slots.length>0,start_slot:1,end_slot:7,slots:i<6?[...slots]:[]}))

async function generate(classPair,kind='whole'){
 const dir=await fs.mkdtemp(path.resolve('.tmp/monday-class-test-'))
 await fs.mkdir(path.join(dir,'data'))
 const isUp=kind==='up', first=isUp?3:Math.min(7,classPair+1)
 const subject={group:0,subgroup:-1,teacher:1,subject_id:0,name:isUp?'УП.01':'Subject',total_hours:isUp?6:2,total_slots:1,generation_active:true,plan_active:true,curriculum_active:true,is_block:isUp,is_pp:false,allowed_campuses:[0],fixed_room:-1,required_room_type:0}
 const split=kind==='first-subgroup'||kind==='second-subgroup'||kind==='both-subgroups'
 const firstPart=kind==='second-subgroup'?1:0
 const lessons=split?[{...subject,id:0,subgroup:firstPart},{...subject,id:1,uid:'second',teacher:2,subgroup:1-firstPart}]:[{...subject,id:0}]
 const teacherSlots=id=>id===0?[]:isUp?[1,2,3,4,5,6,7]:id===1?[first]:[kind==='both-subgroups'?first:Math.min(7,first+1)]
 const data={settings:{semester_start_date:'2026-09-01',semester_end_date:'2026-10-31',start_date:'2026-09-01',end_date:'2026-10-31',require_class_hours:true,solver_config:{solver_workers:1,week_time_limit_seconds:5,solver_time_limit_seconds:5,quality_improvement_seconds:0,stop_after_first_solution:true,min_student_pairs_per_study_day:1,max_student_pairs_per_day:4,hard_no_student_windows:true,hard_no_teacher_windows:true}},groups:[{id:0,name:'TEST-301',parts:2,curator_teacher:0,class_hour_enabled:true,class_hour_fixed_pair:classPair,work_days:workDays(isUp?[1,2,3,4,5,6,7]:[first,...(split?[Math.min(7,first+1)]:[])])}],teachers:[0,1,2].map(id=>({id,name:`Teacher ${id}`,scheduling_active:id!==0,work_days:workDays(teacherSlots(id))})),teacher_unavailable:[{id:0,teacher:0,from:date,to:date,text:'Sick leave'}],rooms:[0,1,2].map(id=>({id,name:`Room ${id}`,campus:0,active:true,class_hour_open:true,work_days:workDays()})),lessons,teaching_ledger:[]}
 await fs.writeFile(path.join(dir,'data/timetable_data.json'),JSON.stringify(data))
 const result=spawnSync(path.resolve(process.env.SCOPED_TEST_SOLVER||'build/Release/timetable_solver.exe'),['--generate','--scope-from',date,'--scope-to',date,'--output','result'],{cwd:dir,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:5e6})
 assert.equal(result.status,0,`${kind}, CH ${classPair}\n${result.stdout}\n${result.stderr}\n${dir}`)
 const schedule=JSON.parse(await fs.readFile(path.join(dir,'result/schedule_all.json'),'utf8'))
 const entries=schedule.groups[0].days[0].slots.flatMap(slot=>(slot.lessons||[]).map(lesson=>({slot:slot.slot,lesson})))
 const classHours=entries.filter(e=>e.lesson.is_class_hour),academic=entries.filter(e=>!e.lesson.is_class_hour)
 assert.equal(classHours.length,1)
 assert.equal(classHours[0].slot,classPair)
 assert.equal(classHours[0].lesson.half,classPair===0?0:2)
 assert.equal(classHours[0].lesson.teacher_id,0)
 assert.equal(classHours[0].lesson.hours,0)
 assert(academic.every(e=>e.slot>classPair&&e.lesson.teacher_id!==0))
 const quality=JSON.parse(await fs.readFile(path.join(dir,'result/quality_report.json'),'utf8'))
 assert.equal(quality.student_windows,0)
 assert.equal(quality.teacher_windows,0)
 assert.equal(quality.planned_hours,isUp?6:split?4:2)
 assert.equal(quality.scheduled_hours,quality.planned_hours)
 const audit=JSON.parse(await fs.readFile(path.join(dir,'result/strict_audit.json'),'utf8'))
 assert.equal(audit.ok,true,JSON.stringify(audit))
 assert.equal(audit.summary.events,academic.length,'class hours must not enter academic quotas')
 const saved=JSON.parse(await fs.readFile(path.join(dir,'data/timetable_data.json'),'utf8'))
 assert.deepEqual(saved.teacher_unavailable.map(({id,teacher,from,to,text})=>({id,teacher,from,to,text})),data.teacher_unavailable)
 assert.deepEqual(saved.teaching_ledger,[])
 return academic
}

for(const pair of [0,1,2,3,4,5]) test(`Monday class hour ${pair} allows later teaching and keeps an absent curator`,async()=>{await generate(pair)})
for(const kind of ['first-subgroup','second-subgroup','both-subgroups','up']) test(`a Monday class hour can precede ${kind} without occupying the student timeline`,async()=>{
 const events=await generate(1,kind)
 if(kind==='both-subgroups')assert.equal(events[0].slot,events[1].slot)
 if(kind==='first-subgroup'||kind==='second-subgroup')assert.notEqual(events[0].slot,events[1].slot)
})
