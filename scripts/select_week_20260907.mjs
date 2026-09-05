import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'

const root = process.cwd()
const dir = path.join(root, 'output/week-2026-09-07-v2')
const file = path.join(dir, 'data/timetable_data.json')
const d = JSON.parse(fs.readFileSync(file, 'utf8'))
const weekdays = Array.from({ length: 6 }, (_, i) => `2026-09-${String(7 + i).padStart(2, '0')}`)
const teachers = new Map(d.teachers.map(t => [t.id, t]))
const groups = new Map(d.groups.map(g => [g.id, g]))
const dayOfWeek = date => new Date(date + 'T12:00:00Z').getUTCDay() || 7
function allows(entity, date, pair) {
  if (entity.scheduling_active === false) return false
  const p = entity.work_period || {}
  if (p.from && p.to && (date < p.from || date > p.to)) return false
  const override = (entity.date_slot_overrides || []).find(o => o.date === date)
  if (override) return override.slots.includes(pair)
  const rule = (entity.work_days || []).find(w => w.day === dayOfWeek(date))
  return !rule || (rule.enabled !== false && (Array.isArray(rule.slots) ? rule.slots.includes(pair) : pair >= rule.start_slot && pair <= rule.end_slot))
}
function absent(items, key, id, date) {
  return items.some(i => (i[key] === id || (key === 'group' && i.all_groups)) &&
    ((i.dates || []).includes(date) || ((i.from || i.from_date) <= date && date <= (i.to || i.to_date))))
}
function capacity(t, from, to) {
  const weeks = new Map()
  for (let date = from; date <= to;) {
    const when = new Date(date + 'T12:00:00Z')
    const dow = dayOfWeek(date)
    const monday = new Date(when); monday.setUTCDate(when.getUTCDate() - dow + 1)
    let count = dow === 7 || absent(d.teacher_unavailable, 'teacher', t.id, date) ? 0 : [1, 2, 3, 4, 5, 6, 7].filter(p => allows(t, date, p)).length
    count = Math.min(count, t.max_pairs_per_day || 7)
    const key = monday.toISOString().slice(0, 10)
    weeks.set(key, [...(weeks.get(key) || []), count])
    when.setUTCDate(when.getUTCDate() + 1); date = when.toISOString().slice(0, 10)
  }
  return [...weeks.values()].reduce((total, counts) => total + counts.sort((a, b) => b - a).slice(0, t.max_work_days_per_week || 7).reduce((a, b) => a + b, 0), 0)
}
const prior = new Map((d.settings.prior_theory_pairs || []).map(p => [`${p.group}|${p.subject}`, p.pairs]))
const lessons = d.lessons.filter(l => l.curriculum_active !== false && !l.is_pp && !l.is_block && teachers.has(l.teacher) && teachers.get(l.teacher).scheduling_active !== false && l.total_hours > 0)
const rows = []
for (const l of lessons) {
  const t = teachers.get(l.teacher), g = groups.get(l.group)
  const teacherCampuses = t.allowed_campuses?.length ? t.allowed_campuses : [0, 1]
  let campuses = t.allowed_campuses?.length ? [...teacherCampuses] : (l.allowed_campuses?.length ? [...l.allowed_campuses] : [0, 1])
  const fixed = d.rooms.find(r => r.id === l.fixed_room)
  if (fixed) campuses = [fixed.campus]
  const lpz = /лпз/i.test(l.name)
  let restrictedRoom = -1
  if ((l.teacher === 49 || l.teacher === 57) && lpz) restrictedRoom = 68
  if (l.teacher === 55 && lpz) restrictedRoom = 66
  if (fixed && l.allow_room_substitution === false) restrictedRoom = fixed.id
  const allowed = []
  weekdays.forEach((date, day) => {
    for (let pair = 1; pair <= 7; pair++) {
      if (!allows(g, date, pair) || !allows(t, date, pair) || absent(d.teacher_unavailable, 'teacher', t.id, date) || absent(d.unavailable, 'group', l.group, date)) continue
      if (restrictedRoom >= 0) {
        const room = d.rooms.find(r => r.id === restrictedRoom)
        if (!room || !allows(room, date, pair) || (room.available_slots?.length && !room.available_slots.includes(pair))) continue
      }
      allowed.push(day * 7 + pair - 1)
    }
  })
  const count = Math.ceil(l.total_hours / 2)
  let maximum = Math.min(count, allowed.length, 12)
  if (l.consecutive_pairs === 2) maximum -= maximum % 2
  const parts = l.subgroup === -1 ? Array.from({ length: g.parts || 2 }, (_, p) => g.id * 2 + p) : [l.subgroup]
  rows.push({ id: l.id, teacher: l.teacher, group: l.group, minimum: 0, maximum, semester_total: count,
    parts, part_weight: parts.length, whole_group: l.subgroup === -1, allowed_slots: allowed, allowed_campuses: campuses,
    subject: String(l.subject_id >= 0 ? l.subject_id : l.name), sports_room: l.required_room_purpose === 'sports_hall',
    restricted_room: restrictedRoom, consecutive_pairs: l.consecutive_pairs || 1, avoid_lunch_split: l.avoid_lunch_split === true })
}
const partKeys = [...new Set(rows.flatMap(l => l.parts))]
const partRows = partKeys.map(key => {
  const list = rows.filter(l => l.parts.includes(key))
  const pace = Math.ceil(list.reduce((s, l) => s + l.semester_total, 0) / 16)
  return { key, lesson_ids: list.map(l => l.id), minimum_target: Math.min(24, pace), maximum_target: Math.min(30, pace + 5) }
})
const teacherRows = d.teachers.map(t => {
  const hours = lessons.filter(l => l.teacher === t.id).reduce((s, l) => s + l.total_hours, 0)
  const max = capacity(t, weekdays[0], weekdays.at(-1))
  const future = capacity(t, weekdays[0], '2026-12-22')
  const target = future ? Math.min(max, Math.ceil(hours / 2 * max / future)) : 0
  return { id: t.id, minimum: [64, 67].includes(t.id) ? 42 : target, hard_minimum: [64, 67].includes(t.id) ? 42 : t.id === 10 ? 3 : 0,
    maximum: max, maximum_daily: t.max_pairs_per_day || 7, max_work_days: t.max_work_days_per_week || 0,
    day_targets: (t.date_load_targets || []).filter(x => weekdays.includes(x.date)).map(x => ({ day: weekdays.indexOf(x.date), minimum: x.minimum_pairs })) }
})
const families = new Map()
for (const l of lessons) {
  const key = `${l.group}|${l.subject_id}`
  const family = families.get(key) || { theory_ids: [], lab_ids: [], prior_theory: prior.get(key) || 0 }
  family[l.is_lab ? 'lab_ids' : 'theory_ids'].push(l.id); families.set(key, family)
}
const accelerated = lessons.filter(l => l.teacher === 60 && /^.*-3\d/.test(groups.get(l.group).name)).map(l => l.id)
const model = { variables: rows, parts: partRows, teachers: teacherRows, lab_rules: [...families.values()].filter(f => f.theory_ids.length && f.lab_ids.length),
  day_count: 6, slots_per_day: 7, distribution_weeks: 16, min_student_pairs_per_day: 2, max_student_pairs_per_day: 5,
  hard_no_student_windows: true, whole_group_same_subject_limit: 2, physical_part_same_subject_limit: 3,
  workers: 8, time_limit_seconds: 60, random_seed: 37, allow_teacher_shortfalls: false,
  lesson_load_targets: [{ lesson_ids: accelerated, minimum: 13 }],
  room_capacity_by_campus: [0, 1].map(c => Math.min(...weekdays.flatMap(date => [1,2,3,4,5,6,7].map(pair => d.rooms.filter(r => r.campus === c && r.active !== false && !['blocked','exclusive'].includes(r.access_mode) && r.purpose !== 'sports_hall' && allows(r,date,pair) && (!r.available_slots?.length || r.available_slots.includes(pair))).length)))),
  sports_capacity_by_campus: [0, 1].map(c => d.rooms.filter(r => r.campus === c && r.active !== false && r.access_mode !== 'blocked' && r.purpose === 'sports_hall').length) }
const modelFile = path.join(dir, 'quota-model.json')
fs.writeFileSync(modelFile, JSON.stringify(model))
if (process.argv.includes('--model-only')) { console.log(`Model ready: ${rows.length} rows`); process.exit(0) }
const result = spawnSync(path.join(root, '.tmp/build-sep7/Release/quota_optimizer.exe'), [modelFile], { encoding: 'utf8', timeout: 150000, env: { ...process.env, PATH: `C:/or-tools/bin;${process.env.PATH}` } })
fs.writeFileSync(path.join(dir, 'quota-result.json'), result.stdout || JSON.stringify({ error: result.error?.message, stderr: result.stderr }))
if (result.status !== 0) { console.log(result.stdout || result.stderr); process.exit(1) }
const q = JSON.parse(result.stdout)
for (const l of d.lessons) { l.total_slots = q.quotas[l.id] || 0; l.generation_active = l.total_slots > 0; if (l.total_slots > 0) l.plan_active = true }
d.settings.teacher_period_targets = teacherRows.filter(t => t.hard_minimum > 0).map(t => ({ teacher: t.id, minimum_pairs: t.hard_minimum }))
d.settings.solver_config.week_time_limit_seconds = 120
d.settings.solver_config.quality_improvement_seconds = 0
d.settings.solver_config.use_quality_objective = false
d.settings.solver_config.solver_workers = 8
if (!fs.existsSync(path.join(dir, 'data/before-week-quotas.json'))) fs.copyFileSync(file, path.join(dir, 'data/before-week-quotas.json'))
fs.writeFileSync(file, JSON.stringify(d, null, 2))
const assignments = Object.entries(q.placement_witness).flatMap(([id, times]) => times.map(time => ({ lesson_id: Number(id), date: weekdays[Math.floor(time / 7)], slot: time % 7 })))
fs.writeFileSync(path.join(dir, 'witness-locks.json'), JSON.stringify({ source: 'quota_witness', assignments }))
console.log(JSON.stringify({ status: q.status, events: assignments.length, teachers: teacherRows.map(t => [t.id, rows.filter(l => l.teacher === t.id).reduce((sum, l) => sum + (q.quotas[l.id] || 0), 0)]), output: file }))
