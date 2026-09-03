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

export const emptyLessonForm = () => ({
  name: '', group: 0, subgroup: -1, teacher: 0,
  total_slots: 10, total_hours: 20, is_lab: false, is_block: false,
  allowed_campuses: [0, 1], subject_id: -1, week_parity: 'all', fixed_room: -1,
  allow_room_substitution: true, required_room_type: 0,
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
  const hoursPerOccurrence = payload.is_block ? 6 : 2
  payload.total_slots = Math.max(1, Math.ceil(Number(payload.total_hours || 0) / hoursPerOccurrence))
  payload.required_capacity = Math.max(0, Number(payload.required_capacity || 0))
  payload.required_room_type = Math.max(0, Number(payload.required_room_type || 0))
  payload.required_equipment = stringList(equipmentText ?? payload.required_equipment)
  return payload
}
