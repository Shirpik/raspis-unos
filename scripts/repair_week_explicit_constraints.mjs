import fs from 'node:fs'
const sourceDir = process.argv[2] || 'output/week-20260928-finalized'
const targetDir = process.argv[3] || 'output/week-20260928-repaired'
const data = JSON.parse(fs.readFileSync('data/timetable_data.json', 'utf8'))
const schedule = JSON.parse(fs.readFileSync(sourceDir + '/schedule_all.json', 'utf8'))
fs.mkdirSync(targetDir, { recursive: true })
const byId = (items, id) => items.find(item => Number(item.id) === Number(id))
const days = schedule.groups.flatMap(group => (group.days || []).map(day => ({ group, day })))
const dayMap = new Map()
for (const entry of days) dayMap.set(String(entry.group.group_index) + '|' + entry.day.date_iso, entry.day)
const dateList = [...new Set(days.map(({ day }) => day.date_iso))].sort()
const teacherBusy = new Map()
const roomBusy = new Map()
const groupBusy = new Map()
const key = (date, slot) => date + '|' + slot
const addBusy = (map, id, date, slot) => { if (id === undefined || id === null || id < 0) return; map.set(String(id) + '|' + key(date, slot), true) }
const isBusy = (map, id, date, slot) => map.has(String(id) + '|' + key(date, slot))
for (const { group, day } of days) for (const slot of day.slots || []) {
  const number = Number(slot.slot); if (number < 1) continue
  for (const lesson of slot.lessons || []) { if (lesson.is_class_hour) continue; addBusy(teacherBusy, Number(lesson.teacher_id), day.date_iso, number); addBusy(roomBusy, Number(lesson.room_id), day.date_iso, number); addBusy(groupBusy, Number(group.group_index), day.date_iso, number) }
}
const blocked = [
  { teacher: 8, dates: new Set(['2026-09-28', '2026-09-29']), fromDate: '2026-09-30' },
  { teacher: 2, dates: new Set(['2026-09-28']), fromDate: '2026-09-29' },
  { teacher: 1, dates: new Set(['2026-09-28']), fromDate: '2026-09-29' },
  { teacher: 9, dates: new Set(['2026-09-28']), fromDate: '2026-09-29' },
]
const teacherRule = new Map(blocked.map(rule => [rule.teacher, rule]))
const moves = []; const unresolved = []
const canUseSlot = (group, lesson, date, slot) => {
  const rule = teacherRule.get(Number(lesson.teacher_id)); if (date < (rule?.fromDate || '0000-00-00')) return false
  if (isBusy(teacherBusy, Number(lesson.teacher_id), date, slot) || isBusy(groupBusy, Number(group.group_index), date, slot)) return false
  if (lesson.room_id !== null && lesson.room_id !== undefined && isBusy(roomBusy, Number(lesson.room_id), date, slot)) return false
  const teacher = byId(data.teachers || [], Number(lesson.teacher_id)); const groupData = byId(data.groups || [], Number(group.group_index))
  const weekday = new Date(date + 'T00:00:00Z').getUTCDay() || 7
  const allowed = item => item?.day === weekday && item.enabled && item.slots?.includes(slot)
  if (teacher && !allowed(teacher.work_days?.find(item => item.day === weekday))) return false
  if (groupData && !allowed(groupData.work_days?.find(item => item.day === weekday))) return false
  return true
}
const chooseRoom = (lesson, date, slot) => {
  const original = Number(lesson.room_id); if (Number.isInteger(original) && !isBusy(roomBusy, original, date, slot)) return original
  const source = byId(data.rooms || [], original)
  return (data.rooms || []).find(room => room.active && room.access_mode !== 'blocked' && room.campus === source?.campus && !isBusy(roomBusy, Number(room.id), date, slot))?.id ?? null
}
for (const { group, day } of days) for (const slot of [...(day.slots || [])]) {
  const number = Number(slot.slot); if (number < 1) continue
  for (const lesson of [...(slot.lessons || [])]) {
    if (lesson.is_class_hour) continue
    const rule = teacherRule.get(Number(lesson.teacher_id)); if (!rule || !rule.dates.has(day.date_iso)) continue
    if (lesson.is_block) { unresolved.push({ group: group.group_name, date: day.date_iso, slot: number, teacher: lesson.teacher_id, reason: 'UP-блок нельзя безопасно перенести автоматически' }); continue }
    let destination = null
    for (const date of dateList) {
      if (date < rule.fromDate) continue
      for (let targetSlot = 1; targetSlot <= 7; targetSlot++) {
        if (!canUseSlot(group, lesson, date, targetSlot)) continue
        const room = chooseRoom(lesson, date, targetSlot); if (room === null) continue
        destination = { date, slot: targetSlot, room }; break
      }
      if (destination) break
    }
    if (!destination) { unresolved.push({ group: group.group_name, date: day.date_iso, slot: number, teacher: lesson.teacher_id, lesson: lesson.name, reason: 'свободный слот не найден' }); continue }
    slot.lessons = (slot.lessons || []).filter(item => item !== lesson)
    const targetDay = dayMap.get(String(group.group_index) + '|' + destination.date)
    let target = (targetDay.slots || []).find(item => Number(item.slot) === destination.slot)
    if (!target) { target = { slot: destination.slot, time: String(destination.slot) + ' пара', text: '-', lessons: [] }; targetDay.slots.push(target) }
    lesson.room_id = destination.room; const room = byId(data.rooms || [], destination.room); lesson.room_name = room?.name ?? lesson.room_name
    target.lessons ||= []; target.lessons.push(lesson); target.text = target.lessons.map(item => item.name || '').filter(Boolean).join(' | ') || '-'
    addBusy(teacherBusy, Number(lesson.teacher_id), destination.date, destination.slot); addBusy(roomBusy, Number(destination.room), destination.date, destination.slot); addBusy(groupBusy, Number(group.group_index), destination.date, destination.slot)
    moves.push({ group: group.group_name, teacher: lesson.teacher_id, lesson: lesson.name, from: day.date_iso + '/' + number, to: destination.date + '/' + destination.slot })
  }
}
for (const group of schedule.groups) for (const day of group.days || []) { day.slots = (day.slots || []).filter(slot => Number(slot.slot) === 0 || (slot.lessons || []).length); day.slots.sort((a, b) => Number(a.slot) - Number(b.slot)) }
schedule.status = 'draft_semester_risk'; schedule.repair_report = { explicit_constraints: true, moves, unresolved }
fs.writeFileSync(targetDir + '/schedule_all.json', JSON.stringify(schedule, null, 2))
fs.copyFileSync(sourceDir + '/quality_report.json', targetDir + '/quality_report.json')
fs.copyFileSync(sourceDir + '/strict_audit.json', targetDir + '/strict_audit.json')
fs.writeFileSync(targetDir + '/repair_report.json', JSON.stringify(schedule.repair_report, null, 2))
console.log(JSON.stringify({ moved: moves.length, unresolved: unresolved.length, unresolved_items: unresolved }, null, 2))

