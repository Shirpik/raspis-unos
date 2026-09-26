import fs from 'node:fs'
const folder = '.tmp/week-20260928-revision'
fs.mkdirSync(folder, {recursive:true})
const original = `${folder}/before.json`
if (!fs.existsSync(original)) fs.copyFileSync('data/timetable_data.json', original)
const data = JSON.parse(fs.readFileSync(original,'utf8'))
data.settings.class_hour_zero_only = false
data.settings.hard_no_student_windows = true
data.settings.hard_no_teacher_windows = true
Object.assign(data.settings.solver_config, {
  hard_no_student_windows:true, hard_no_teacher_windows:true,
  week_time_limit_seconds:180, solver_time_limit_seconds:200,
  use_quality_objective:false, stop_after_first_solution:true,
})
for (const group of data.groups) {
  group.class_hour_fixed_pair = group.curator_teacher === 21 ? 0 : -1
  if (group.curator_teacher === 21) {
    group.class_hour_room_required = true
    group.class_hour_room = 49
    group.class_hour_campus = 0
  }
}
const potapova = data.teacher_unavailable.find(x=>x.uid==='teacher_unavailable-week-20260928-8')
potapova.dates = ['2026-09-28','2026-09-29']
data.teacher_unavailable.push({id:18,uid:'teacher-unavailable-demina-20260928',teacher:2,dates:['2026-09-28'],text:'Уточнение пользователя: понедельник без пар'})
for (const id of [9,29]) {
  const teacher = data.teachers.find(t=>t.id===id)
  teacher.date_slot_overrides = (teacher.date_slot_overrides||[]).filter(x=>x.date!=='2026-09-28')
  teacher.date_slot_overrides.push({date:'2026-09-28',slots:[1,2,3,4,5,6,7]})
}
data.teachers.find(t=>t.id===21).external_busy_slots = [{date:'2026-09-28',slots:[1,2,3],source:'Работа в школе; подтверждено пользователем'}]
// Кабинеты, которые администрация разрешила открывать для классных часов.
// 411 обязателен для Комаровой; 1А, 57, библиотека и актовый зал являются
// резервом для остальных кураторов.
for (const roomId of [0, 49, 54, 69, 70]) {
  const room = data.rooms.find(r => Number(r.id) === roomId)
  if (room) room.class_hour_open = true
}
// The absent curator names were explicitly retained for Monday's class hour.
// These curators may still be named for the Monday class hour while their
// ordinary teaching pairs are blocked by the user's absence instructions.
for (const id of [1,8,2]) data.teachers.find(t=>t.id===id).class_hour_available_dates=['2026-09-28']
data.meta.week_generation_policy = 'Без окон студентов и преподавателей. Классный час: 0 либо вторая половина пар 2–4. Общие классные часы факультативны; Комарова — обе группы в 411 в 08:15. УП — один показанный блок, 6 часов.'
const output = `${folder}/input.json`
fs.writeFileSync(output,JSON.stringify(data,null,2)+'\n')
fs.copyFileSync(output,'data/timetable_data.json')
console.log(output)
