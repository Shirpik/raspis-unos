import fs from 'node:fs'

const source = process.argv[2] || '.tmp/google-sheets/merged-ledger-preview.json'
const output = process.argv[3] || '.tmp/google-sheets/week-20260928-candidate.json'
const data = JSON.parse(fs.readFileSync(source, 'utf8'))
const weekDates = ['2026-09-28', '2026-09-29', '2026-09-30', '2026-10-01', '2026-10-02', '2026-10-03']

data.settings.start_date = '2026-09-28'
data.settings.end_date = '2026-10-03'
data.settings.class_hour_from = '08:15'
data.settings.class_hour_to = '08:55'
data.settings.automatic_period_quotas = false

for (const group of data.groups) {
  group.class_hour_from = '08:15'
  group.class_hour_to = '08:55'
}
// Классные часы Дроговейко — всегда Кривоусова. Комарова — утром на
// Лесной в 411 кабинете (оба её куратора).
for (const group of data.groups) {
  if (group.curator_teacher === 26) group.class_hour_campus = 1
  if (group.curator_teacher === 21) {
    group.class_hour_campus = 0
    group.class_hour_room = 49
  }
}

const unavailable = [
  [1, ['2026-09-28', '2026-10-01']],
  [12, weekDates],
  [44, weekDates],
  [41, ['2026-09-29', '2026-10-03']],
  [8, ['2026-09-28']],
  [24, ['2026-10-01']],
  [82, ['2026-09-29', '2026-09-30', '2026-10-03']],
]
const nextUnavailableId = () => Math.max(-1, ...data.teacher_unavailable.map(x => Number(x.id ?? -1))) + 1
let unavailableId = nextUnavailableId()
for (const [teacher, dates] of unavailable) {
  data.teacher_unavailable.push({
    id: unavailableId++, uid: `teacher_unavailable-week-20260928-${teacher}`,
    teacher, dates: [...dates], from_date: null, to_date: null,
    text: 'Ограничение на неделю 28.09–03.10.2026',
  })
}
// Нагрузка Рабенок присутствует в плане, но на этой неделе ни одна её строка
// не может быть назначена. Нулевой недельной квотой оставляем её на следующую
// доступную неделю, чтобы балансировщик не пытался разместить недоступные пары.
for (const lesson of data.lessons) {
  if (lesson.teacher === 12 && !lesson.is_pp) {
    lesson.total_slots = 0
    lesson.generation_active = false
  }
}

const lessonById = id => {
  const lesson = data.lessons.find(item => Number(item.id) === id)
  if (!lesson) throw new Error(`Не найдено занятие ${id}`)
  return lesson
}
const lockAssignments = []
const fixedOrdinaryCounts = new Map()
const ordinary = (lessonId, date, slots) => {
  const lesson = lessonById(lessonId)
  lesson.generation_active = true
  const count = (fixedOrdinaryCounts.get(lessonId) || 0) + slots.length
  fixedOrdinaryCounts.set(lessonId, count)
  lesson.total_slots = Math.max(Number(lesson.total_slots || 0), count)
  for (const slot of slots) lockAssignments.push({ lesson_id: lessonId, date, slot })
}
const up = (lessonId, date, startSlot, fixedRoom = -1) => {
  const lesson = lessonById(lessonId)
  lesson.generation_active = true
  lesson.total_slots = Math.max(Number(lesson.total_slots || 0), 1)
  lesson.block_start_slots = [startSlot]
  if (fixedRoom >= 0) {
    lesson.fixed_room = fixedRoom
    lesson.allow_room_substitution = false
    lesson.allowed_campuses = [0]
  }
  lockAssignments.push({ lesson_id: lessonId, date, slot: startSlot })
  lockAssignments.push({ lesson_id: lessonId, date, slot: startSlot + 1 })
}

// Попова Т.В. — обязательные строки с фото пользователя.
ordinary(293, '2026-09-28', [0, 1]) // МЦМ-Пф-202 МДК 02.02
ordinary(316, '2026-09-28', [2, 3]) // МЦМ-Пф-301 МДК 02.03
ordinary(290, '2026-09-29', [0])
ordinary(286, '2026-09-29', [1])
ordinary(337, '2026-09-29', [2, 3])
ordinary(317, '2026-09-30', [0, 1])
ordinary(313, '2026-09-30', [2])
ordinary(318, '2026-09-30', [3, 4])
up(294, '2026-10-01', 0)
up(295, '2026-10-01', 2)
ordinary(338, '2026-10-02', [2, 3, 4])
ordinary(339, '2026-10-02', [0, 1])
ordinary(290, '2026-10-03', [0])
ordinary(286, '2026-10-03', [1])
ordinary(293, '2026-10-03', [2])

// СП-Пф-3601: обе подгруппы УП в ЦПДЭ, первая утром, вторая после обеда.
up(577, '2026-10-01', 0, 68)
up(578, '2026-10-01', 2, 68)

// Две строки УП.02 из семестровой части вклеек нужны только как исторический
// факт 26.09, но не должны создавать план следующей недели.
for (const id of [1123, 1124]) {
  const lesson = data.lessons.find(item => Number(item.id) === id)
  if (lesson) {
    lesson.generation_active = false
    lesson.plan_active = false
    lesson.curriculum_active = false
    lesson.total_slots = 0
    lesson.total_hours = 0
  }
}

data.meta = {
  ...(data.meta || {}),
  week_prepared_at: new Date().toISOString(),
  week_period: '2026-09-28/2026-10-03',
  week_notes: 'План недели подготовлен по четырём подтверждённым таблицам, вклейкам и ограничениям пользователя.',
}
data.settings.week_source = 'Вклейки 2026-2027 (6).xlsx + Google Sheets 02.09–26.09.2026'
data.settings.week_fixed_assignments = lockAssignments.length

const lockFile = output.replace(/\.json$/i, '.locks.json')
fs.writeFileSync(output, JSON.stringify(data, null, 2) + '\n')
fs.writeFileSync(lockFile, JSON.stringify({ source: 'week-20260928-user-fixed', assignments: lockAssignments }, null, 2) + '\n')
console.log(JSON.stringify({ output, lockFile, locks: lockAssignments.length, lessons: data.lessons.length, ledger: data.teaching_ledger.length }))
