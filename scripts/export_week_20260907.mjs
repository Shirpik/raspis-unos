import fs from 'node:fs'
import path from 'node:path'
import { buildScheduleExcelWorkbook, scheduleExcelFilename } from '../frontend/src/utils/scheduleTemplateExport.js'
const base = process.argv[3] || 'output/week-2026-09-07-v2'
const source = process.argv[2] || `${base}/output/week-final`
const audit = JSON.parse(fs.readFileSync(`${source}/strict_audit.json`, 'utf8'))
if (!audit.ok || audit.issues.some(i => i.severity === 'error')) throw new Error('Independent weekly validation failed')
const s = JSON.parse(fs.readFileSync(`${source}/schedule_all.json`, 'utf8'))
const db = JSON.parse(fs.readFileSync(`${base}/data/timetable_data.json`, 'utf8'))
const teachers = new Map(db.teachers.map(t => [t.id,t]))
// Independently check the exact exported payload, including pair zero. Do not
// rely on quality_report's legacy ordinary-pair-only window counter.
for (const group of s.groups) for (const day of group.days) {
  const definition = db.groups.find(g => g.id === group.group_index)
  for (let part = 0; part < (definition?.parts || 2); ++part) {
    const occupied = day.slots.filter(slot => (slot.lessons || []).some(l =>
      l.is_class_hour || l.subgroup == null || l.subgroup < 0 || l.subgroup === group.group_index * 2 + part
    )).map(slot => slot.slot).sort((a,b)=>a-b)
    if (occupied.length && occupied.at(-1)-occupied[0]+1 !== new Set(occupied).size)
      throw new Error(`Student gap including class hour: ${group.group_name}, ${day.date_iso}, subgroup ${part+1}: ${occupied}`)
  }
}
const loads = new Map(), classes = [], counts = new Map()
for (const group of s.groups) for (const day of group.days) for (const slot of day.slots) for (const lesson of slot.lessons || []) {
  lesson.teacher_name ||= teachers.get(lesson.teacher_id)?.name || ''
  if (lesson.is_class_hour) classes.push([group.group_name, slot.slot, lesson.teacher_name, lesson.room_name])
  else {
    const key = `${lesson.teacher_id}|${day.date_iso}`
    loads.set(key, (loads.get(key)||0)+1)
    counts.set(day.date_iso,(counts.get(day.date_iso)||0)+1)
  }
}
if (s.groups.length !== 46 || counts.size !== 6 || classes.length !== 46) throw new Error('Incomplete weekly schedule')
for (const id of [64,67]) for (const date of counts.keys()) if (loads.get(`${id}|${date}`) !== 7) throw new Error('Expected seven pairs per day')
const mondaySemenova = s.groups.flatMap(g=>g.days.filter(d=>d.date_iso==='2026-09-07').flatMap(d=>d.slots.flatMap(slot=>(slot.lessons||[]).filter(l=>!l.is_class_hour && l.teacher_id===10).map(()=>slot.slot)))).sort()
if (JSON.stringify(mondaySemenova)!=='[1,2,3]') throw new Error('Semenova morning request violated')
const template = fs.readFileSync('frontend/public/templates/schedule-template.xlsx')
const result = await buildScheduleExcelWorkbook(s, template)
const destination = path.resolve(`${base}/delivery`)
fs.mkdirSync(destination, {recursive:true})
const filename = path.join(destination, scheduleExcelFilename(s).replace('.xlsx', `${process.argv[4] || ''}.xlsx`))
await result.workbook.xlsx.writeFile(filename)
const verification = {file:filename, sheets:result.sheets, groups:result.groups, dates:result.dates, exported:result.insertedLessons,
  ordinary:[...counts.values()].reduce((a,b)=>a+b,0), class_hours:classes.length, days:Object.fromEntries(counts),
  semester_status:'not_confirmed', errors:audit.issues.filter(i=>i.severity==='error'), warnings:audit.issues.filter(i=>i.severity!=='error'),
  class_hour_placements:classes}
fs.writeFileSync(`${destination}/verification.json`, JSON.stringify(verification,null,2))
fs.copyFileSync(`${source}/strict_audit.json`, `${destination}/strict_audit.json`)
fs.copyFileSync(`${base}/data/timetable_data.json`, `${destination}/source-database.json`)
fs.writeFileSync(`${destination}/schedule_all.json`, JSON.stringify(s,null,2))
console.log(JSON.stringify({...verification,warnings:verification.warnings.length,class_hour_placements:undefined},null,2))
