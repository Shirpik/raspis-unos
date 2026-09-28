import { parseCompletedSchedule } from './completedScheduleImport.js'

const SLOT_TIMES = [
  '',
  '1 пара (08:30-09:55)',
  '2 пара (10:05-11:30)',
  '3 пара (12:25-13:50)',
  '4 пара (14:00-15:25)',
  '5 пара (15:35-16:55)',
  '6 пара (17:05-18:25)',
  '7 пара (18:35-19:55)',
]

const clone = value => typeof structuredClone === 'function'
  ? structuredClone(value)
  : JSON.parse(JSON.stringify(value))

const normalized = value => String(value ?? '')
  .toLocaleLowerCase('ru')
  .replaceAll('ё', 'е')
  .replace(/[^a-zа-я0-9]+/giu, '')

const displayDate = iso => `${iso.slice(8, 10)}.${iso.slice(5, 7)}.${iso.slice(0, 4)}`
const weekday = iso => ['', 'ПН', 'ВТ', 'СР', 'ЧТ', 'ПТ', 'СБ'][new Date(`${iso}T12:00:00Z`).getUTCDay()] || ''

const monday = iso => {
  const date = new Date(`${iso}T12:00:00Z`)
  const day = date.getUTCDay()
  date.setUTCDate(date.getUTCDate() - (day === 0 ? 6 : day - 1))
  return date.toISOString().slice(0, 10)
}

const subgroupLabel = subgroup => {
  const value = Number(subgroup)
  if (value < 0 || !Number.isInteger(value)) return 'вся группа'
  return value % 2 === 0 ? '1 п/г' : '2 п/г'
}

const roomReference = value => {
  const source = String(value || '').trim().replace(/^каб\.\s*/iu, '')
  const suffix = source.match(/[_-]([КкЛл])\s*$/u)?.[1]?.toLocaleUpperCase('ru') || ''
  return {
    name: source.replace(/[_-][КкЛл]\s*$/u, '').trim(),
    campus: suffix === 'Л' ? 0 : suffix === 'К' ? 1 : null,
  }
}

const findRoom = (raw, rooms) => {
  const reference = roomReference(raw)
  if (!reference.name) return null
  const candidates = (rooms || []).filter(room => normalized(room.name) === normalized(reference.name))
  return candidates.find(room => reference.campus === null || Number(room.campus) === reference.campus) || candidates[0] || null
}

const makeEmptyDay = (iso, startDate = '') => {
  let dayIndex = -1
  if (/^\d{4}-\d{2}-\d{2}$/.test(startDate || '')) {
    dayIndex = Math.round((new Date(`${iso}T12:00:00Z`) - new Date(`${startDate}T12:00:00Z`)) / 86400000)
  }
  return {
    date: displayDate(iso),
    date_iso: iso,
    day_index: dayIndex,
    weekday: weekday(iso),
    slots: SLOT_TIMES.slice(1).map((time, index) => ({ slot: index + 1, time, text: '-', lessons: [] })),
  }
}

const makeRenderedLesson = (row, lesson, data, warnings) => {
  const room = findRoom(row.room, data.rooms)
  const teacherId = Number.isInteger(row.actual_teacher) ? row.actual_teacher : Number(lesson.teacher)
  const teacher = (data.teachers || []).find(item => Number(item.id) === teacherId)
  const roomRef = roomReference(row.room)
  if (row.room && !room) warnings.push(`${row.date}, ${row.source_subject}: аудитория «${row.room}» не найдена; сохранено её название`)
  return {
    id: Number(lesson.id),
    uid: lesson.uid,
    name: lesson.name,
    teacher_id: teacherId,
    teacher_name: teacher?.name || row.source_teacher || '',
    subgroup: Number(lesson.subgroup ?? -1),
    is_lab: lesson.is_lab === true,
    is_block: lesson.is_block === true,
    avoid_lunch_split: lesson.avoid_lunch_split === true,
    consecutive_pairs: Number(lesson.consecutive_pairs || 1),
    week_parity: lesson.week_parity || 'all',
    room_id: room ? Number(room.id) : null,
    room_name: room?.name || roomRef.name || null,
    room_type: room ? Number(room.room_type || 0) : Number(lesson.required_room_type || 0),
    room_substituted: false,
    room_substitution_reason: null,
  }
}

const updateSlotText = (slot, data) => {
  if (!slot.lessons.length) {
    slot.text = '-'
    return
  }
  slot.text = slot.lessons.map(lesson => {
    const room = (data.rooms || []).find(item => Number(item.id) === Number(lesson.room_id))
    const campus = room ? (Number(room.campus) === 0 ? 'Лесная' : 'Кривоусова') : ''
    return `${lesson.name} — ${subgroupLabel(lesson.subgroup)}, ${lesson.teacher_name || ''}${campus ? `, ${campus}` : ''}`
  }).join(' | ')
}

export async function importScheduleWeekFromExcel(file, data, currentManual = { groups: [] }) {
  const parsed = await parseCompletedSchedule(file, data, { status: 'planned' })
  const errors = [...parsed.errors]
  const warnings = [...parsed.warnings]
  const dates = parsed.detectedDates || []
  const groupIds = new Set(parsed.detectedGroupIds || [])

  if (!dates.length) errors.push('В Excel не найдены даты расписания')
  const weeks = new Set(dates.map(monday))
  if (weeks.size > 1) errors.push('Файл содержит больше одной недели. Загрузите Excel только за одну неделю')
  if (!parsed.importedRows?.length) errors.push('В Excel не найдено ни одного занятия из текущей нагрузки')
  if (parsed.excludedClassHours) warnings.push(`Классные часы пропущены: ${parsed.excludedClassHours}. Их можно достроить генератором`)

  if (errors.length) {
    return { ok: false, errors, warnings, dates, imported: parsed.imported || 0, manualData: null }
  }

  const manualData = clone(currentManual && Array.isArray(currentManual.groups) ? currentManual : { groups: [] })
  manualData.groups ||= []
  const manualGroups = new Map(manualData.groups.map(group => [Number(group.group_index), group]))
  const startDate = data.settings?.start_date || data.settings?.semester_start_date || ''

  for (const groupId of groupIds) {
    const sourceGroup = (data.groups || []).find(group => Number(group.id) === groupId)
    if (!sourceGroup) continue
    let group = manualGroups.get(groupId)
    if (!group) {
      group = { group_index: groupId, group_name: sourceGroup.name, days: [] }
      manualData.groups.push(group)
      manualGroups.set(groupId, group)
    }
    group.group_name = sourceGroup.name
    group.days ||= []
    const importedDateSet = new Set(dates)
    group.days = group.days.filter(day => !importedDateSet.has(day.date_iso))
    group.days.push(...dates.map(iso => makeEmptyDay(iso, startDate)))
    group.days.sort((a, b) => String(a.date_iso).localeCompare(String(b.date_iso)))
  }

  const lessons = new Map((data.lessons || []).map(lesson => [Number(lesson.id), lesson]))
  for (const row of parsed.importedRows) {
    const group = manualGroups.get(Number(row.group_id))
    const lesson = lessons.get(Number(row.lesson_id))
    const day = group?.days?.find(item => item.date_iso === row.date)
    const slot = day?.slots?.find(item => Number(item.slot) === Number(row.slot))
    if (!group || !lesson || !slot) {
      errors.push(`${row.date}: не удалось подготовить ячейку для «${row.source_subject}»`)
      continue
    }
    slot.lessons.push(makeRenderedLesson(row, lesson, data, warnings))
    updateSlotText(slot, data)
  }

  if (errors.length) return { ok: false, errors, warnings, dates, imported: parsed.imported, manualData: null }
  return {
    ok: true,
    errors,
    warnings,
    dates,
    groupCount: groupIds.size,
    imported: parsed.imported,
    manualData,
  }
}
