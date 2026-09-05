import fs from 'node:fs'
const dir = 'output/week-2026-09-07-v2'
const d = JSON.parse(fs.readFileSync(`${dir}/data/timetable_data.json`, 'utf8'))
const q = JSON.parse(fs.readFileSync(`${dir}/quota-result.json`, 'utf8'))
const schedule = { groups: d.groups.map(g => ({ group_index: g.id, group_name: g.name, days: Array.from({ length: 6 }, (_, i) => ({
  date_iso: `2026-09-${String(7 + i).padStart(2, '0')}`, date: `${String(7 + i).padStart(2, '0')}.09.2026`, weekday: ['ПН', 'ВТ', 'СР', 'ЧТ', 'ПТ', 'СБ'][i],
  slots: Array.from({ length: 7 }, (_, s) => ({ slot: s + 1, lessons: d.lessons.filter(l => l.group === g.id && q.placement_witness[l.id]?.includes(i * 7 + s)).map(l => ({ id: l.id, teacher_id: l.teacher, room_id: l.fixed_room >= 0 ? l.fixed_room : d.teachers.find(t => t.id === l.teacher)?.default_room ?? -1 })) }))
})) })) }
fs.writeFileSync(`${dir}/witness-diagnostic.json`, JSON.stringify(schedule))
