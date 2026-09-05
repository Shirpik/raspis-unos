import fs from 'node:fs'
const original = 'output/week-2026-09-07-v2'
const target = 'output/week-2026-09-07-contiguous'
if (fs.existsSync(target)) throw new Error('Revision already exists')
const d = JSON.parse(fs.readFileSync(`${original}/delivery/source-database.json`, 'utf8'))
// These campus values were selected by align_class_hour_campuses.mjs, not
// supplied as hard user restrictions. Let the integrated model choose them.
// All teacher/lesson/room campus permissions remain unchanged.
for (const group of d.groups) group.class_hour_campus = -1
fs.mkdirSync(`${target}/data`, { recursive: true })
fs.writeFileSync(`${target}/data/timetable_data.json`, JSON.stringify(d,null,2))
console.log(target)
