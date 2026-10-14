const weekdayNames = ['Вс', 'Пн', 'Вт', 'Ср', 'Чт', 'Пт', 'Сб']
const regularSlotTimes = [
  '08:30-09:55', '10:05-11:30', '12:25-13:50', '14:00-15:25',
  '15:35-16:55', '17:05-18:25', '18:35-19:55',
]
const mondaySlotTimes = [
  '09:15-10:40', '10:50-12:15', '13:10-14:35', '14:45-16:10',
  '16:20-17:40', '17:50-19:10', '19:20-20:40',
]

function slotTime(date, slot) {
  const times = new Date(`${date}T12:00:00`).getDay() === 1 ? mondaySlotTimes : regularSlotTimes
  return `${slot} пара (${times[slot - 1]})`
}

function displayDate(iso) {
  return `${iso.slice(8, 10)}.${iso.slice(5, 7)}.${iso.slice(0, 4)}`
}

export function mergeConfirmedSchedule(schedule, data) {
  const groups = new Map((schedule?.groups || []).map(group => [group.group_index, {
    ...group,
    days: (group.days || []).map(day => ({
      ...day,
      slots: (day.slots || []).map(slot => ({ ...slot, lessons: [...(slot.lessons || [])] })),
    })),
  }]))
  const lessons = new Map((data?.lessons || []).map(item => [item.id, item]))
  const teachers = new Map((data?.teachers || []).map(item => [item.id, item.name]))
  const rooms = new Map((data?.rooms || []).map(item => [item.id, item.name]))
  const confirmed = (data?.teaching_ledger || []).filter(item => item.status === 'confirmed' &&
    /^\d{4}-\d{2}-\d{2}$/.test(item.date) && item.slot >= 1 && item.slot <= 7)
  const replacedCells = new Set()
  for (const item of confirmed) {
    const sourceGroup = (data?.groups || []).find(group => group.id === item.group_id)
    if (!sourceGroup) continue
    if (!groups.has(item.group_id)) groups.set(item.group_id, {
      group_index: item.group_id, group_name: sourceGroup.name, days: [],
    })
    const group = groups.get(item.group_id)
    let day = group.days.find(value => value.date_iso === item.date)
    if (!day) {
      const date = new Date(`${item.date}T12:00:00`)
      day = { date_iso: item.date, date: displayDate(item.date),
        weekday: weekdayNames[date.getDay()], slots: [] }
      group.days.push(day)
    }
    let slot = day.slots.find(value => Number(value.slot) === Number(item.slot))
    if (!slot) {
      slot = { slot: Number(item.slot), time: slotTime(item.date, Number(item.slot)), text: '-', lessons: [] }
      day.slots.push(slot)
    }
    const cellKey = `${item.group_id}|${item.date}|${item.slot}`
    if (!replacedCells.has(cellKey)) {
      slot.lessons = []
      replacedCells.add(cellKey)
    }
    const lesson = lessons.get(item.lesson_id)
    if (!lesson) continue
    // A confirmed event is the source of truth for this lesson and cell.
    slot.lessons = slot.lessons.filter(value => value.id !== item.lesson_id)
    const teacherId = item.actual_teacher ?? lesson.teacher
    const roomId = rooms.has(Number(item.room)) ? Number(item.room) : null
    slot.lessons.push({
      id: lesson.id, uid: lesson.uid, name: lesson.name, subgroup: lesson.subgroup,
      teacher_id: teacherId, teacher_name: teachers.get(teacherId) || '', room_id: roomId,
      room_name: item.room || (roomId === null ? '' : rooms.get(roomId)),
      confirmed: true, hours: item.hours, is_lab: lesson.is_lab === true,
      is_block: lesson.is_block === true, is_class_hour: lesson.is_class_hour === true,
    })
    slot.text = slot.lessons.map(value => `${value.name} — ${teachers.get(value.teacher_id) || ''}${value.room_name ? `, каб. ${value.room_name}` : ''}`).join(' / ')
  }
  for (const group of groups.values()) {
    group.days.sort((a, b) => a.date_iso.localeCompare(b.date_iso))
    for (const day of group.days) day.slots.sort((a, b) => a.slot - b.slot)
  }
  return {
    ...(schedule || {}),
    groups: [...groups.values()].sort((a, b) => a.group_index - b.group_index),
    confirmed_count: confirmed.length,
  }
}
