import fs from 'node:fs'
const dir = 'output/week-2026-09-07-v2'
const filename = `${dir}/data/timetable_data.json`
const d = JSON.parse(fs.readFileSync(filename, 'utf8'))
const s = JSON.parse(fs.readFileSync(`${dir}/output/unlocked/schedule_all.json`, 'utf8'))
const counts = new Map()
for (const group of s.groups) for (const day of group.days) for (const slot of day.slots)
  for (const lesson of slot.lessons || []) if (lesson.id >= 0) counts.set(lesson.id, (counts.get(lesson.id) || 0) + 1)
fs.copyFileSync(filename, `${dir}/data/quota-second-attempt.json`)
for (const l of d.lessons) { l.total_slots = counts.get(l.id) || 0; l.generation_active = l.total_slots > 0; if (l.total_slots) l.plan_active = true }
fs.writeFileSync(filename, JSON.stringify(d, null, 2))
console.log([...counts.values()].reduce((a,b)=>a+b,0))
