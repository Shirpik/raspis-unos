import fs from 'node:fs'

const input = process.argv[2] || '.tmp/google-sheets/final-week-base.json'
const output = process.argv[3] || '.tmp/google-sheets/final-week.json'
const data = JSON.parse(fs.readFileSync(input, 'utf8'))

// Уточнение пользователя: оставить ФИО Вагайской/Потаповой в классных часах.
// Исключение относится только к классному часу и не разрешает обычные пары.
for (const teacher of data.teachers) {
  if ([1, 8].includes(Number(teacher.id))) {
    teacher.class_hour_available_dates = [...new Set([...(teacher.class_hour_available_dates || []), '2026-09-28'])]
  }
  if ([11, 13, 27].includes(Number(teacher.id))) teacher.class_hour_max_groups = 3
}
for (const group of data.groups) {
  group.class_hour_enabled = true
  delete group.class_hour_disabled_reason
  if (Number(group.curator_teacher) === 21) group.class_hour_room_required = true
}

// Окна оставляем мягким критерием качества, чтобы не жертвовать обязательными
// парами и доступностью. Фиксированные занятия, доступность, кабинеты и УП
// остаются жёсткими ограничениями.
data.settings.hard_no_student_windows = false
data.settings.hard_no_teacher_windows = false
data.settings.require_class_hours = true
data.settings.class_hour_zero_only = true
data.settings.solver_config = {
  ...(data.settings.solver_config || {}),
  hard_no_student_windows: false,
  hard_no_teacher_windows: false,
  allow_single_pair_day_fallback: true,
  require_class_hours: true,
}

// Лабораторные без подтверждённой теоретической базы на этой неделе не ставим.
const withheldLabs = [386, 420, 421, 433, 714, 987]
for (const id of withheldLabs) {
  const lesson = data.lessons.find(item => Number(item.id) === id)
  if (!lesson) continue
  lesson.total_slots = 0
  lesson.generation_active = false
  lesson.week_hold_reason = 'В текущей вклейке нет подтверждённой теории; отложено во избежание постановки лабораторной без базы'
}

// Подтверждённая теория из четырёх уже прошедших недель является входом для
// правила «теория до ЛПЗ». Старые вручную сохранённые значения не уменьшаем.
const priorTheory = new Map((data.settings.prior_theory_pairs || []).map(row =>
  [`${row.group}:${row.subject}`, Number(row.pairs || 0)]))
const ledgerTheory = new Map()
const lessonById = new Map(data.lessons.map(lesson => [Number(lesson.id), lesson]))
for (const row of data.teaching_ledger || []) {
  if (row.status && row.status !== 'confirmed') continue
  const lesson = lessonById.get(Number(row.lesson_id))
  if (!lesson || lesson.is_lab || lesson.is_block || lesson.is_pp || Number(lesson.subject_id) < 0) continue
  const key = `${lesson.group}:${lesson.subject_id}`
  ledgerTheory.set(key, (ledgerTheory.get(key) || 0) + Number(row.hours || 0) / 2)
}
for (const [key, pairs] of ledgerTheory)
  priorTheory.set(key, Math.max(priorTheory.get(key) || 0, pairs))
data.settings.prior_theory_pairs = [...priorTheory].map(([key, pairs]) => {
  const [group, subject] = key.split(':').map(Number)
  return { group, subject, pairs: Math.floor(pairs) }
})

// Для Поповой оставляем только пары, перечисленные на фотографиях пользователя.
const popovaTeacher = 43
const popovaFixed = new Set([286, 290, 293, 294, 295, 313, 316, 317, 318, 337, 338, 339])
for (const lesson of data.lessons) {
  if (Number(lesson.teacher) !== popovaTeacher || popovaFixed.has(Number(lesson.id))) continue
  lesson.total_slots = 0
  lesson.generation_active = false
  lesson.week_hold_reason = 'Не входит в зафиксированные пользователем пары Поповой на 28.09–03.10.2026'
}

data.meta = {
  ...(data.meta || {}),
  week_generation_policy: 'Окна преподавателей и групп — мягкий критерий качества; доступность, фиксированные пары, кабинеты, классные часы и УП — жёсткие ограничения.',
  withheld_lab_lessons: withheldLabs,
  popova_fixed_lessons: [...popovaFixed],
}
fs.writeFileSync(output, JSON.stringify(data, null, 2) + '\n')
console.log(JSON.stringify({ output, disabledClassHours: data.groups.filter(g => g.class_hour_enabled === false).map(g => g.name), withheldLabs, popovaDisabled: data.lessons.filter(l => Number(l.teacher) === popovaTeacher && !popovaFixed.has(Number(l.id))).length }))
