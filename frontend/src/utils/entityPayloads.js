const clone = value => value == null ? value : JSON.parse(JSON.stringify(value))

const stringList = value => {
  const values = Array.isArray(value) ? value : String(value || '').split(/[;,\n]/)
  return [...new Set(values.map(item => String(item).trim()).filter(Boolean))]
}

export const emptyRoomForm = () => ({
  name: '', campus: 0, room_type: 0, capacity: 0, active: true,
  purpose: '', access_mode: 'general', responsible_teacher_ids: [],
  responsible_note: '', description: '', available_slots: [], equipment: [],
  equipment_text: '',
})

export function roomFormFromEntity(room = null) {
  if (!room) return emptyRoomForm()
  const source = clone(room)
  return {
    ...emptyRoomForm(),
    ...source,
    active: source.active !== false,
    responsible_teacher_ids: [...(source.responsible_teacher_ids || [])],
    available_slots: [...(source.available_slots || [])],
    equipment: [...(source.equipment || [])],
    equipment_text: (source.equipment || []).join(', '),
  }
}

export function roomPayloadFromForm(form) {
  const source = clone(form) || {}
  const { equipment_text: equipmentText, ...payload } = source
  payload.name = String(payload.name || '').trim()
  payload.active = payload.access_mode === 'blocked' ? false : payload.active !== false
  payload.equipment = stringList(equipmentText ?? payload.equipment)
  return payload
}

const defaultWorkDays = () => Array.from({ length: 7 }, (_, index) => ({
  day: index + 1,
  enabled: index < 6,
  start_slot: 1,
  end_slot: 7,
  slots: index < 6 ? Array.from({ length: 7 }, (__, slot) => slot + 1) : [],
}))

export const emptyTeacherForm = () => ({
  name: '', default_room: -1, preferred_campus: -1,
  allowed_campuses: [0, 1], room_responsibility: '', availability_note: '',
  max_work_days_per_week: 0, max_pairs_per_day: 0,
  work_period: { from: '', to: '' }, work_days: defaultWorkDays(),
  date_slot_overrides: [],
})

export function teacherFormFromEntity(teacher = null) {
  if (!teacher) return emptyTeacherForm()
  const source = clone(teacher)
  return {
    ...emptyTeacherForm(),
    ...source,
    preferred_campus: source.campus_priority?.[0] ?? -1,
    allowed_campuses: source.allowed_campuses?.length ? [...source.allowed_campuses] : [0, 1],
    work_period: { from: source.work_period?.from || '', to: source.work_period?.to || '' },
    work_days: (source.work_days || defaultWorkDays()).map(day => ({ ...day, slots: [...(day.slots || [])] })),
    date_slot_overrides: (source.date_slot_overrides || []).map(item => ({
      ...item,
      slots: [...(item.slots || [])],
    })),
  }
}

export function teacherPayloadFromForm(form) {
  const payload = clone(form) || {}
  const preferredCampus = Number(payload.preferred_campus)
  const existingPriority = Array.isArray(payload.campus_priority)
    ? payload.campus_priority.map(Number)
    : []
  delete payload.preferred_campus
  payload.name = String(payload.name || '').trim()
  payload.room_responsibility = String(payload.room_responsibility || '').trim()
  payload.availability_note = String(payload.availability_note || '').trim()
  payload.default_room = Number(payload.default_room ?? -1)
  payload.max_work_days_per_week = Math.max(0, Number(payload.max_work_days_per_week || 0))
  payload.max_pairs_per_day = Math.max(0, Number(payload.max_pairs_per_day || 0))
  // Если пользователь не менял приоритет, сохраняем исходный массив точно:
  // [1] не должен самопроизвольно превращаться в [1, 0]. При реальном
  // изменении выбранная площадка переносится в начало существующего порядка.
  payload.campus_priority = preferredCampus < 0
    ? []
    : existingPriority[0] === preferredCampus
      ? existingPriority
      : existingPriority.length
        ? [preferredCampus, ...existingPriority.filter(value => value !== preferredCampus)]
        : [preferredCampus, preferredCampus === 0 ? 1 : 0]
  payload.allowed_campuses = [...new Set((payload.allowed_campuses || []).map(Number))]
  payload.date_slot_overrides = (payload.date_slot_overrides || [])
    .filter(item => item?.date)
    .map(item => ({ ...item, slots: [...new Set((item.slots || []).map(Number))].sort((a, b) => a - b) }))
  return payload
}

export const emptyLessonForm = () => ({
  name: '', group: 0, subgroup: -1, teacher: 0,
  total_slots: 10, total_hours: 20, is_lab: false, is_block: false,
  consecutive_pairs: 1, avoid_lunch_split: false,
  allowed_campuses: [0, 1], subject_id: -1, week_parity: 'all', fixed_room: -1,
  allow_room_substitution: true, required_room_type: 0,
  required_room_purpose: '',
  required_capacity: 0, required_equipment: [], required_equipment_text: '',
})

export function lessonFormFromEntity(lesson = null) {
  if (!lesson) return emptyLessonForm()
  const source = clone(lesson)
  return {
    ...emptyLessonForm(),
    ...source,
    total_hours: source.total_hours ?? (source.total_slots || 0) * 2,
    allowed_campuses: [...(source.allowed_campuses || [])],
    allow_room_substitution: source.allow_room_substitution !== false,
    required_equipment: [...(source.required_equipment || [])],
    required_equipment_text: (source.required_equipment || []).join(', '),
  }
}

export function lessonPayloadFromForm(form) {
  const source = clone(form) || {}
  const { required_equipment_text: equipmentText, ...payload } = source
  payload.name = String(payload.name || '').trim()
  // total_hours — семестровый учебный план, total_slots — отдельная квота
  // выбранного периода. Одно не должно неявно перезаписывать другое.
  payload.total_hours = Math.max(0, Number(payload.total_hours || 0))
  payload.total_slots = Math.max(0, Number(payload.total_slots || 0))
  payload.required_capacity = Math.max(0, Number(payload.required_capacity || 0))
  payload.required_room_type = Math.max(0, Number(payload.required_room_type || 0))
  payload.consecutive_pairs = Number(payload.consecutive_pairs) === 2 ? 2 : 1
  payload.avoid_lunch_split = payload.avoid_lunch_split === true
  if (payload.consecutive_pairs === 2 && payload.total_slots % 2 !== 0) payload.total_slots++
  payload.required_equipment = stringList(equipmentText ?? payload.required_equipment)
  return payload
}
