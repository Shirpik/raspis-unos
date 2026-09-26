import fs from 'node:fs'
import { execFileSync } from 'node:child_process'

const inputPath = process.argv[2] || '.tmp/google-sheets/final-week.json'
const target = Number(process.argv[3] || 16)
const outPath = process.argv[4] || `.tmp/google-sheets/week-full-${target}.json`
const data = JSON.parse(fs.readFileSync(inputPath, 'utf8'))
const dates = ['2026-09-28','2026-09-29','2026-09-30','2026-10-01','2026-10-02','2026-10-03']
const teacher = new Map(data.teachers.map(x => [Number(x.id), x]))
const group = new Map(data.groups.map(x => [Number(x.id), x]))
const ledger = new Map()
const priorTheory = new Map()
for (const row of data.teaching_ledger) {
  if (row.status && row.status !== 'confirmed') continue
  ledger.set(Number(row.lesson_id), (ledger.get(Number(row.lesson_id)) || 0) + Number(row.hours || 0))
}
for (const lesson of data.lessons) if (!lesson.is_lab && !lesson.is_block && Number(lesson.subject_id) >= 0) {
  const key = `${lesson.group}:${lesson.subject_id}`
  priorTheory.set(key, (priorTheory.get(key) || 0) + Math.floor((ledger.get(Number(lesson.id)) || 0) / 2))
}
const weekLocks = JSON.parse(fs.readFileSync('.tmp/google-sheets/final-week-base.locks.json', 'utf8')).assignments
const fixed = weekLocks.map(x => ({lesson_id:Number(x.lesson_id),time:dates.indexOf(x.date)*7+Number(x.slot)}))
const fixedCount = new Map()
for (const x of fixed) fixedCount.set(x.lesson_id,(fixedCount.get(x.lesson_id)||0)+1)
const upDaysByPart = new Map()
for (const lock of weekLocks) {
  const lesson = data.lessons.find(item => Number(item.id) === Number(lock.lesson_id))
  if (!lesson?.is_block) continue
  const parts = Number(lesson.subgroup) < 0
    ? [Number(lesson.group) * 2, Number(lesson.group) * 2 + 1]
    : [Number(lesson.subgroup)]
  for (const part of parts) {
    if (!upDaysByPart.has(part)) upDaysByPart.set(part, new Set())
    upDaysByPart.get(part).add(lock.date)
  }
}
const isUnavailable = (id,date) => data.teacher_unavailable.some(x => Number(x.teacher) === id && ((x.dates||[]).includes(date) || (x.from_date||x.from||'9999') <= date && date <= (x.to_date||x.to||'0000')))
const allows = (entity,date,pair) => {
  if (!entity || entity.scheduling_active === false) return false
  const period=entity.work_period || {}
  if (period.from && date < period.from || period.to && date > period.to) return false
  const override=(entity.date_slot_overrides||[]).find(x=>x.date===date)
  if (override) return (override.slots||[]).map(Number).includes(pair)
  const day=dates.indexOf(date)+1
  const wd=(entity.work_days||[]).find(x=>Number(x.day)===day)
  return !wd || (wd.enabled !== false && (wd.slots ? wd.slots.map(Number).includes(pair) : pair>=Number(wd.start_slot||1)&&pair<=Number(wd.end_slot||7)))
}
const unavailableGroup = (id,date) => data.unavailable?.some(x=>Number(x.group??x.group_id)===id && ((x.dates||[]).includes(date) || (x.from_date||x.from||'9999')<=date && date<=(x.to_date||x.to||'0000'))) || false
const eligible=[]
for(const lesson of data.lessons){
  const id=Number(lesson.id), gid=Number(lesson.group), tid=Number(lesson.teacher)
  const rem=Number(lesson.total_hours||0)-(ledger.get(id)||0)
  const locked=fixedCount.get(id)||0
  let allowed=lesson.curriculum_active!==false && lesson.plan_active!==false && !lesson.is_pp && tid>=0 && teacher.has(tid) && group.has(gid) && !lesson.week_hold_reason
  if (lesson.is_block && !locked) allowed=false // УП только по явному подтверждению этой недели
  if (!allowed || (rem < (lesson.is_block?6:2) && !locked)) { lesson.total_slots=0; lesson.generation_active=false; continue }
  const t=teacher.get(tid),g=group.get(gid)
  const tc=new Set((t.allowed_campuses||[]).map(Number)),lc=new Set((lesson.allowed_campuses||[]).map(Number))
  const campuses=[0,1].filter(c=>(!tc.size||tc.has(c))&&(!lc.size||lc.has(c)))
  const allowedSlots=[]
  for(let day=0;day<6;day++)for(let slot=0;slot<7;slot++){
    const date=dates[day],pair=slot+1
    const physicalParts=Number(lesson.subgroup)<0?[gid*2,gid*2+1]:[Number(lesson.subgroup)]
    if (!lesson.is_block && physicalParts.some(part => upDaysByPart.get(part)?.has(date))) continue
    if(isUnavailable(tid,date)||unavailableGroup(gid,date)||!allows(t,date,pair)||!allows(g,date,pair))continue
    if(lesson.is_block && ![0,1,2,3].includes(slot))continue
    if(g.class_hour_enabled && g.class_hour_campus>=0 && day===0 && campuses.length===1 && campuses[0]!==Number(g.class_hour_campus))continue
    allowedSlots.push(day*7+slot)
  }
  const unit=lesson.is_block?6:2
  const availablePeriods=Math.floor(Math.max(0,rem)/unit)
  // The photographed Popova timetable is exact, not merely a minimum.
  const max=lesson.is_block||tid===43?locked:Math.min(6,availablePeriods)
  if(max<locked || max===0 || !campuses.length){lesson.total_slots=0;lesson.generation_active=false;continue}
  const part=Number(lesson.subgroup)<0?[gid*2,gid*2+1]:[Number(lesson.subgroup)]
  const targetByRemaining=Math.max(0,Math.round((Math.max(0,rem)/unit)/12*1000))
  const historical=(data.teaching_ledger||[]).filter(x=>Number(x.lesson_id)===id && x.date>='2026-09-14').reduce((a,x)=>a+Number(x.hours||0),0)/2
  const targetMilli=lesson.is_block?locked*1000:Math.max(targetByRemaining,Math.round(historical/2*1000))
  eligible.push({id,lesson,tid,gid,parts:part,campuses,allowedSlots,maximum:max,minimum:locked,targetMilli})
}
const byId=new Map(eligible.map(x=>[x.id,x]))
for(const lock of fixed) if(!byId.has(lock.lesson_id)) throw new Error(`Fixed lesson ${lock.lesson_id} is not eligible`)
const variables=eligible.map(x=>({id:x.id,minimum:x.minimum,maximum:x.maximum,semester_total:x.maximum*12,target_pairs_milli:x.targetMilli,teacher:x.tid,group:x.gid,parts:x.parts,subject:String(x.lesson.subject_id??x.id),whole_group:x.parts.length===2,allowed_slots:x.allowedSlots,allowed_campuses:x.campuses,part_weight:x.parts.length,sports_room:x.lesson.required_room_purpose==='sports_hall',computer_room:x.lesson.required_room_type===2,consecutive_pairs:x.lesson.is_block?2:Number(x.lesson.consecutive_pairs||1),block_start_slots:x.lesson.block_start_slots||[],restricted_room:Number(x.lesson.fixed_room??-1),avoid_lunch_split:x.lesson.avoid_lunch_split===true}))
const parts=[]
for(const g of data.groups)for(let p=0;p<Math.max(1,Number(g.parts||2));p++){
 const key=Number(g.id)*2+p
 const ids=eligible.filter(x=>x.parts.includes(key)).map(x=>x.id)
 const fixedHere=eligible.filter(x=>x.parts.includes(key)).reduce((a,x)=>a+x.minimum,0)
 const capacity=eligible.filter(x=>x.parts.includes(key)).reduce((a,x)=>a+x.maximum,0)
 const goal=Math.min(Math.max(target,fixedHere),capacity)
 parts.push({group:Number(g.id),part:p,key,lesson_ids:ids,minimum_target:goal,maximum_target:Math.min(21,Math.max(goal,capacity))})
}
const teacherRows=[]
for(const t of data.teachers){const tid=Number(t.id);const vars=eligible.filter(x=>x.tid===tid);if(!vars.length)continue
 const dayCap=dates.map(date=>isUnavailable(tid,date)?0:Array.from({length:7},(_,i)=>allows(t,date,i+1)?1:0).reduce((a,b)=>a+b,0))
 const maxDaily=Number(t.max_pairs_per_day||7)
 const max=dayCap.reduce((a,n)=>a+Math.min(n,maxDaily),0)
 const fixedTeacher=vars.reduce((a,x)=>a+x.minimum,0)
 const priority=/Гарбузов|Вальдиянов|Меренчуков/.test(t.name)
 teacherRows.push({id:tid,minimum:Math.max(fixedTeacher,priority?Math.min(max,7):0),maximum:max,maximum_daily:maxDaily,max_work_days:Number(t.max_work_days_per_week||0)})
}
const labRules=[]
const families=new Map()
for(const x of eligible){const key=`${x.gid}:${x.lesson.subject_id}`;if(!families.has(key))families.set(key,{g:x.gid,s:x.lesson.subject_id,theory:[],labs:[]});(x.lesson.is_lab?families.get(key).labs:families.get(key).theory).push(x.id)}
for(const f of families.values())if(f.theory.length&&f.labs.length)labRules.push({group:f.g,subject:f.s,theory_ids:f.theory,lab_ids:f.labs,prior_theory:priorTheory.get(`${f.g}:${f.s}`)||0})
const countRooms=(campus,purpose)=>data.rooms.filter(r=>r.active!==false && r.access_mode!=='blocked' && r.access_mode!=='exclusive' && Number(r.campus)===campus && (r.purpose==='sports_hall')===purpose).length
const model={day_count:6,slots_per_day:7,variables,parts,teachers:teacherRows,lab_rules:labRules,fixed,room_capacity_by_campus:[countRooms(0,false),countRooms(1,false)],sports_capacity_by_campus:[countRooms(0,true),countRooms(1,true)],min_student_pairs_per_day:2,max_student_pairs_per_day:4,preferred_student_pairs_per_day:4,student_day_shortfall_weight:100000,hard_no_student_windows:false,hard_no_teacher_windows:false,whole_group_same_subject_limit:2,physical_part_same_subject_limit:3,require_same_subgroup_study_days:true,distribution_weeks:12,workers:8,time_limit_seconds:60,random_seed:38,maximize_part_load:true}
const modelPath=outPath.replace(/\.json$/,'.quota-model.json')
fs.writeFileSync(modelPath,JSON.stringify(model))
console.log('model',JSON.stringify({target,variables:variables.length,parts:parts.length,teachers:teacherRows.length,labRules:labRules.length,locks:fixed.length,room_capacity:model.room_capacity_by_campus}))
if (process.argv.includes('--model-only')) process.exit(0)
let result
try {result=JSON.parse(execFileSync('.tmp/audit-build-clean/Release/quota_optimizer.exe',[modelPath],{encoding:'utf8',maxBuffer:100*1024*1024}).replace(/^\uFEFF/,''))}
catch(e){console.error(e.stdout?.toString().slice(0,5000)||e.message);process.exit(1)}
fs.writeFileSync(outPath.replace(/\.json$/,'.quota-result.json'),JSON.stringify(result,null,2))
console.log('result',result.success,result.status,result.message,result.objective)
if(!result.success)process.exit(2)
for(const lesson of data.lessons){const row=byId.get(Number(lesson.id));if(!row)continue;const slots=Number(result.quotas[String(row.id)]||0);lesson.total_slots=lesson.is_block?slots/2:slots;lesson.generation_active=slots>0;lesson.week_quota_source='exact_quota_optimizer_20260928'}
data.meta={...(data.meta||{}),week_quota_target_per_physical_subgroup:target,week_quota_model:modelPath,week_quota_status:result.status}
fs.writeFileSync(outPath,JSON.stringify(data,null,2)+'\n')
console.log('written',outPath,'selected',data.lessons.filter(l=>l.generation_active&&l.total_slots>0).length,'ordinaryPairs',data.lessons.filter(l=>!l.is_block&&l.generation_active).reduce((a,l)=>a+Number(l.total_slots||0),0))
