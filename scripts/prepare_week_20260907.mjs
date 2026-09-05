// Reproducible database migration into an isolated candidate. Never overwrites
// the approved database or schedule, and never invents confirmed teaching hours.
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const destination = path.join(root, 'output', 'week-2026-09-07-v2')
const dataPath = path.join(destination, 'data', 'timetable_data.json')
if (fs.existsSync(dataPath)) throw new Error(`Candidate already exists: ${dataPath}. Review it instead of overwriting.`)
const data = JSON.parse(fs.readFileSync(path.join(root, 'data', 'timetable_data.json'), 'utf8').replace(/^\uFEFF/, ''))
const notes = []
data.settings.start_date = '2026-09-07'
data.settings.end_date = '2026-09-12'
data.settings.class_hour_from = '08:15'
data.settings.class_hour_to = '08:55'
data.settings.flag_raising_time = '07:55'
data.settings.require_class_hours = true
data.settings.semester_weeks = 16
data.settings.enforce_semester_readout = true
// Provisional full 16-week interval: first academic week starts on Monday
// 31 August. This assumption is explicitly reported, not silently extended.
data.settings.semester_start_date = '2026-09-02'
notes.push('Пользователь подтвердил: 16 недель с 02.09.2026; конец интервала включительно 22.12.2026.')
for (const item of [...data.teacher_unavailable, ...data.unavailable]) {
  if (!item.from && item.from_date) item.from = item.from_date
  if (!item.to && item.to_date) item.to = item.to_date
}
const teacher = id => { const result = data.teachers.find(t => t.id === id); if (!result) throw new Error(`Teacher ${id} missing`); return result }
const room = (name, campus = 1) => { const result = data.rooms.find(r => r.name === name && r.campus === campus); if (!result) throw new Error(`Room ${name} missing`); return result }
const addTeacher = (name, source) => {
  const existing = data.teachers.find(t => t.name === name)
  if (existing) return existing.id
  const id = Math.max(...data.teachers.map(t => t.id)) + 1
  data.teachers.push({ id, uid: `teacher-curator-${id}`, name, source, curator_only: true,
    default_room: -1, allowed_campuses: [0, 1], campus_priority: [],
    work_days: Array.from({ length: 7 }, (_, i) => ({ day: i + 1, enabled: i === 0, start_slot: 1, end_slot: 7, slots: i === 0 ? [1, 2, 3, 4, 5, 6, 7] : [] })) })
  return id
}
const gabets = addTeacher('Габец Н.Е.', 'Фотография кураторов, предоставленная пользователем; полное ФИО не раскрыто')
const fedorova = addTeacher('Федорова М.В.', 'Фотография кураторов, предоставленная пользователем; полное ФИО не раскрыто')
const polyakova = addTeacher('Полякова Ю.А.', 'Закрепление кабинета 18/1 из базы и подтверждение пользователя')
const rabenok = addTeacher('Рабенок Т.С.', 'Пользователь подтвердил: отдельный человек, не Рабенок М.А.')
const sidelnikova = addTeacher('Сидельникова А.А.', 'Пользователь подтвердил: отдельный человек, не Синельникова Е.В.')
teacher(26).class_hour_available_dates = [...new Set([...(teacher(26).class_hour_available_dates || []), '2026-09-07'])]
for (const id of [18, 35]) teacher(id).scheduling_active = false

const addClassRoom = (name, fallback = false) => {
  const old = data.rooms.find(r => r.name === name && r.campus === 1)
  if (old) return old
  const id = Math.max(...data.rooms.map(r => r.id)) + 1
  const item = { id, uid: `room-class-hour-${id}`, name, campus: 1, room_type: 0, capacity: 0,
    active: false, access_mode: 'blocked', class_hour_open: true, class_hour_fallback: fallback,
    source: 'Уточнение пользователя: открыто для классных часов; обычные пары не разрешены' }
  data.rooms.push(item)
  return item
}
const hall = addClassRoom('Актовый зал')
addClassRoom('Библиотека', true)
room('1а').class_hour_open = true
room('1а').class_hour_fallback = true
room('57').class_hour_open = true
for (const name of ['34', '42', '23', '41', '28', '27', '37', '18/1', '2', '3']) room(name).class_hour_zero_blocked = true
Object.assign(room('18/1'), { active: true, access_mode: 'exclusive', responsible_teacher_ids: [polyakova] })
Object.assign(room('18/2'), { active: true, access_mode: 'exclusive', responsible_teacher_ids: [30], class_hour_open: true })
teacher(30).default_room = room('18/2').id

// Group IDs are resolved against the existing database; unknown identities are
// deliberately left unresolved, never merged by a similar surname alone.
const mappings = [
  [0, gabets, '70'], [1, 47, '48'], [2, 62, '50'], [3, 46, '65'], [4, 23, '69'], [5, 56, '64'],
  [6, fedorova, '57'], [7, 5, '63'], [8, 54, '13'], [9, 2, '68'], [10, 13, '22'],
  [11, 0, '23'], [12, 31, '68'], [13, 31, '68'], [14, 43, '65'], [15, 19, '22'], [16, 46, '41'],
  [17, 10, '54'], [18, 49, '47'], [19, 20, '15'], [20, 1, '60'], [21, 52, '57'], [22, 53, '61'],
  [23, 45, '50'], [24, 11, '31'], [25, 26, '27'], [26, 56, '69'], [28, 26, '27'], [29, 54, '13'],
  [30, 8, '3'], [31, 11, '31'], [32, 33, '34'], [33, 11, '31'],
  [34, rabenok, '2'], [27, sidelnikova, '18/2'],
  [35, 27, 'Актовый зал'], [36, 27, 'Актовый зал'], [37, 13, '48'], [38, 13, '48'], [39, 27, 'Актовый зал'],
  [40, 15, '63'], [41, gabets, '70'], [42, 17, '28'], [43, 21, '38'], [44, 21, '38'], [45, 22, '33']
]
for (const [groupId, curator, preferred] of mappings) {
  const group = data.groups.find(g => g.id === groupId)
  if (!group) throw new Error(`Group ${groupId} missing`)
  Object.assign(group, { curator_teacher: curator, class_hour_enabled: true, class_hour_campus: -1,
    class_hour_room: preferred === 'Актовый зал' ? hall.id : room(preferred).id, curator_source: 'Фото пользователя' })
}
notes.push('Все 46 кураторов сопоставлены. Рабенок Т.С. и Сидельникова А.А. заведены отдельно без изменения существующих преподавателей.')
notes.push('Дроговейко проводит свои классные часы 07.09; отсутствие для обычных пар до 12.09 сохранено.')
notes.push('Третяк и Серянина временно не активны по указанию пользователя; их часы не списаны.')

const semenova = teacher(10)
semenova.date_slot_overrides = (semenova.date_slot_overrides || []).filter(x => x.date !== '2026-09-07')
semenova.date_slot_overrides.push({ date: '2026-09-07', slots: [1, 2, 3] })
semenova.date_load_targets = (semenova.date_load_targets || []).filter(x => x.date !== '2026-09-07')
semenova.date_load_targets.push({ date: '2026-09-07', minimum_pairs: 3 })
Object.assign(semenova.work_days.find(d => d.day === 6), { enabled: true, start_slot: 1, end_slot: 4, slots: [1, 2, 3, 4] })
const tymerov = teacher(60)
tymerov.desired_load_rules = [...(tymerov.desired_load_rules || []), { group_ids: [], course_year: 3,
  deadline: '2026-10-31', minimum_pairs_per_week: 0, source: 'Пользователь: вычитать третий курс до ноября' }]
for (const lesson of data.lessons) if (lesson.teacher === 57 && /лпз/i.test(lesson.name)) {
  lesson.fixed_room = 68
  lesson.preferred_room = 68
  lesson.allow_room_substitution = false
  lesson.allowed_campuses = [0]
}
// Keep curriculum hours, quotas, minimum targets and all previous absences.
// Existing two-day quotas are not a valid week plan: the new preflight must
// identify the shortfall before any schedule can be published.
notes.push('Квоты прежнего периода сохранены для диагностики, не объявлены недельным планом. Автоматическое ослабление минимумов запрещено.')
notes.push('Подтверждённый журнал проведённых часов отсутствует; старое расписание не зачисляется как факт автоматически.')
fs.mkdirSync(path.dirname(dataPath), { recursive: true })
fs.writeFileSync(dataPath, JSON.stringify(data, null, 2) + '\n', { flag: 'wx' })
fs.writeFileSync(path.join(destination, 'preparation-notes.json'), JSON.stringify({ status: 'needs_clarification_and_quota_planning', notes }, null, 2) + '\n', { flag: 'wx' })
console.log(JSON.stringify({ candidate: dataPath, mapped_curators: mappings.length, notes }, null, 2))
