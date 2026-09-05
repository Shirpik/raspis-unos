import fs from 'node:fs'
const filename = 'output/week-2026-09-07-v2/data/timetable_data.json'
const d = JSON.parse(fs.readFileSync(filename, 'utf8'))
const selected = new Map([[11,1],[31,1],[46,1],[13,0],[27,0],[21,0],[56,0],[54,1],[83,1],[26,0]])
for (const g of d.groups) if (selected.has(g.curator_teacher)) g.class_hour_campus = selected.get(g.curator_teacher)
d.settings.solver_config.random_seed = 38
fs.writeFileSync(filename, JSON.stringify(d, null, 2))
