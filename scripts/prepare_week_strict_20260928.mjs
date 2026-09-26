import fs from 'node:fs'
import path from 'node:path'
import { execFileSync } from 'node:child_process'

// Build the strict six-day input used by the weekly quota pass.  This file is
// deliberately separate from the older prepare/repair scripts: it starts from
// the policy file which contains the current front-end restrictions and keeps
// every eligible lesson visible to the quota solver.
const sourcePath = process.argv[2]?.startsWith('--') ? '.tmp/week-20260928-revision/input.json' : (process.argv[2] || '.tmp/week-20260928-revision/input.json')
const outDir = process.argv[2]?.startsWith('--') ? '.tmp/week-20260928-strict' : (process.argv[3] || '.tmp/week-20260928-strict')
const solve = process.argv.includes('--solve')
const dates = ['2026-09-28', '2026-09-29', '2026-09-30', '2026-10-01', '2026-10-02', '2026-10-03']
const days = new Set(dates)
const data = JSON.parse(fs.readFileSync(sourcePath, 'utf8'))
fs.mkdirSync(outDir, { recursive: true })

const num = value => Number(value)
const groupById = new Map(data.groups.map(x => [num(x.id), x]))
const teacherById = new Map(data.teachers.map(x => [num(x.id), x]))
const lessonById = new Map(data.lessons.map(x => [num(x.id), x]))
const ledger = new Map()
for (const row of data.teaching_ledger || []) {
  if (row.status && row.status !== 'confirmed') continue
  const id = num(row.lesson_id)
  ledger.set(id, (ledger.get(id) || 0) + num(row.hours || 0))
}

const unavailable = (teacherId, date) => (data.teacher_unavailable || []).some(row => {
  if (num(row.teacher) !== teacherId) return false
  if ((row.dates || []).includes(date)) return true
  const from = row.from_date || row.from
  const to = row.to_date || row.to
  return Boolean(from && to && from <= date && date <= to)
})
const groupUnavailable = (groupId, date) => (data.unavailable || []).some(row => {
  if (num(row.group ?? row.group_id) !== groupId) return false
  if ((row.dates || []).includes(date)) return true
  const from = row.from_date || row.from
  const to = row.to_date || row.to
  return Boolean(from && to && from <= date && date <= to)
})
const dayNumber = date => dates.indexOf(date) + 1
const allows = (entity, date, pair) => {
  if (!entity || entity.scheduling_active === false) return false
  const period = entity.work_period || {}
  if (period.from && date < period.from) return false
  if (period.to && date > period.to) return false
  const override = (entity.date_slot_overrides || []).find(x => x.date === date)
  if (override) return (override.slots || []).map(Number).includes(pair)
  const row = (entity.work_days || []).find(x => num(x.day) === dayNumber(date))
  if (!row) return true
  if (row.enabled === false) return false
  if (Array.isArray(row.slots)) return row.slots.map(Number).includes(pair)
  return pair >= num(row.start_slot || 1) && pair <= num(row.end_slot || 7)
}
const externallyBusy = (teacher, date, pair) =>
  (teacher?.external_busy_slots || []).some(row => row.date === date && (row.slots || []).map(Number).includes(pair))

// Only the hand-entered Popova and UP placements are hard locks.  The old
// 1165-cell witness is intentionally a search hint and is not carried over.
const lockArg = process.argv.slice(4).find(value => value && !value.startsWith('--') && !value.toLowerCase().endsWith('.exe'))
const lockFile = lockArg || '.tmp/google-sheets/final-week-base.locks.json'
const lockJson = JSON.parse(fs.readFileSync(lockFile, 'utf8'))
// Normal lessons use one lock per occupied pair.  UP is different: the
// workbook contains both visual halves of the six-hour block, while the CP
// model must receive one start cell and derive the second half itself.
const fixed = []
const seenBlockLocks = new Set()
for (const row of (lockJson.assignments || [])) {
  const lesson = lessonById.get(num(row.lesson_id))
  const time = dates.indexOf(row.date) * 7 + num(row.slot)
  if (!lesson?.is_block) {
    fixed.push({ lesson_id: num(row.lesson_id), time })
    continue
  }
  const key = `${num(row.lesson_id)}:${row.date}`
  if (seenBlockLocks.has(key)) continue
  seenBlockLocks.add(key)
  fixed.push({ lesson_id: num(row.lesson_id), time })
}
const fixedCount = new Map()
for (const row of fixed) fixedCount.set(row.lesson_id, (fixedCount.get(row.lesson_id) || 0) + 1)

// An UP block occupies the whole day for each physical subgroup.  Ordinary
// variables on those dates are therefore removed from their allowed cells.
const upDaysByPart = new Map()
for (const row of fixed) {
  const lesson = lessonById.get(row.lesson_id)
  if (!lesson?.is_block) continue
  const parts = num(lesson.subgroup) < 0
    ? [num(lesson.group) * 2, num(lesson.group) * 2 + 1]
    : [num(lesson.subgroup)]
  const date = dates[Math.floor(row.time / 7)]
  for (const part of parts) {
    if (!upDaysByPart.has(part)) upDaysByPart.set(part, new Set())
    upDaysByPart.get(part).add(date)
  }
}

const eligible = []
for (const lesson of data.lessons) {
  const id = num(lesson.id)
  const gid = num(lesson.group)
  const tid = num(lesson.teacher)
  const group = groupById.get(gid)
  const teacher = teacherById.get(tid)
  const locked = fixedCount.get(id) || 0
  const isBlock = Boolean(lesson.is_block)
  const remainingHours = Math.max(0, num(lesson.total_hours || 0) - (ledger.get(id) || 0))
  let active = Boolean(group && teacher && tid >= 0 && lesson.curriculum_active !== false && lesson.plan_active !== false && !lesson.week_hold_reason)
  // UP is scheduled only when it has a confirmed placement in the supplied
  // lock file.  This prevents an unrequested second UP block being invented.
  if (isBlock && !locked) active = false
  if (!active) continue
  const physicalParts = num(lesson.subgroup) < 0 ? [gid * 2, gid * 2 + 1] : [num(lesson.subgroup)]
  const teacherCampuses = new Set((teacher.allowed_campuses || []).map(Number))
  const lessonCampuses = new Set((lesson.allowed_campuses || []).map(Number))
  const campuses = [0, 1].filter(c => (!teacherCampuses.size || teacherCampuses.has(c)) && (!lessonCampuses.size || lessonCampuses.has(c)))
  const allowedSlots = []
  for (let day = 0; day < dates.length; day++) for (let slot = 0; slot < 7; slot++) {
    const date = dates[day]
    const pair = slot + 1
    if (unavailable(tid, date) || groupUnavailable(gid, date) || !allows(teacher, date, pair) || !allows(group, date, pair)) continue
    if (externallyBusy(teacher, date, pair)) continue
    if (!isBlock && physicalParts.some(part => upDaysByPart.get(part)?.has(date))) continue
    if (isBlock && ![0, 1, 2, 3].includes(slot)) continue
    allowedSlots.push(day * 7 + slot)
  }
  // Ordinary subjects are capped at three pairs per subject per week in this
  // quota stage; the placement solver can still distribute six cells for a
  // two-pair lesson. UP uses the exact locked two-cell footprint.
  const unit = isBlock ? 2 : 1
  const availablePeriods = Math.floor(remainingHours / (isBlock ? 6 : 2))
  const maximum = isBlock ? locked : Math.min(6, availablePeriods, allowedSlots.length)
  if (!campuses.length || maximum < locked || (!locked && maximum <= 0)) continue
  eligible.push({ lesson, id, gid, tid, group, teacher, physicalParts, campuses, allowedSlots, locked, maximum, isBlock })
}

const variables = eligible.map(row => ({
  id: row.id,
  // The quota optimizer counts occupied pair cells.  A UP placement is one
  // displayed block but occupies two consecutive cells in the CP model.
  minimum: row.isBlock ? row.locked * 2 : row.locked,
  maximum: row.isBlock ? row.maximum * 2 : row.maximum,
  semester_total: row.maximum * 12,
  target_pairs_milli: row.isBlock ? row.locked * 2000 : -1,
  teacher: row.tid,
  group: row.gid,
  parts: row.physicalParts,
  whole_group: row.physicalParts.length === 2,
  subject: String(row.lesson.subject_id ?? row.id),
  allowed_slots: row.allowedSlots,
  allowed_campuses: row.campuses,
  part_weight: row.physicalParts.length,
  sports_room: row.lesson.required_room_purpose === 'sports_hall',
  computer_room: row.lesson.required_room_type === 2,
  consecutive_pairs: row.isBlock ? 2 : num(row.lesson.consecutive_pairs || 1),
  block_start_slots: row.lesson.block_start_slots || [],
  restricted_room: num(row.lesson.fixed_room ?? -1),
  avoid_lunch_split: row.lesson.avoid_lunch_split === true,
}))

const parts = []
for (const group of data.groups) for (let part = 0; part < Math.max(1, num(group.parts || 2)); part++) {
  const key = num(group.id) * 2 + part
  const rows = eligible.filter(x => x.physicalParts.includes(key))
  const capacity = rows.reduce((sum, x) => sum + x.maximum, 0)
  const fixedHere = rows.reduce((sum, x) => sum + x.locked, 0)
  parts.push({ group: num(group.id), part, key, lesson_ids: rows.map(x => x.id), minimum_target: Math.max(18, fixedHere), maximum_target: Math.max(18, Math.min(21, capacity)) })
}

const teacherRows = []
for (const teacher of data.teachers) {
  const tid = num(teacher.id)
  const rows = eligible.filter(x => x.tid === tid)
  if (!rows.length) continue
  const maxDaily = num(teacher.max_pairs_per_day || 7) || 7
  const capacity = dates.reduce((sum, date) => {
    let available = 0
    for (let pair = 1; pair <= 7; pair++) if (allows(teacher, date, pair) && !unavailable(tid, date) && !externallyBusy(teacher, date, pair)) available++
    return sum + Math.min(maxDaily, available)
  }, 0)
  teacherRows.push({ id: tid, minimum: rows.reduce((sum, x) => sum + x.locked, 0), maximum: capacity, maximum_daily: maxDaily, external_busy_slots: teacher.external_busy_slots || [] })
}

const countRooms = (campus, sports) => data.rooms.filter(room => {
  if (room.active === false || num(room.campus) !== campus) return false
  if (room.access_mode === 'blocked' || room.access_mode === 'exclusive') return false
  if ((room.purpose === 'sports_hall') !== sports) return false
  return true
}).length

const priorTheory = new Map()
for (const row of data.teaching_ledger || []) {
  const lesson = lessonById.get(num(row.lesson_id))
  if (!lesson || lesson.is_lab || lesson.is_block || num(lesson.subject_id) < 0) continue
  const key = `${num(lesson.group)}:${num(lesson.subject_id)}`
  priorTheory.set(key, (priorTheory.get(key) || 0) + Math.floor(num(row.hours || 0) / 2))
}
const familyMap = new Map()
for (const row of eligible) {
  const key = `${row.gid}:${num(row.lesson.subject_id)}`
  if (!familyMap.has(key)) familyMap.set(key, { group: row.gid, subject: num(row.lesson.subject_id), theory_ids: [], lab_ids: [] })
  ;(row.lesson.is_lab ? familyMap.get(key).lab_ids : familyMap.get(key).theory_ids).push(row.id)
}
const labRules = [...familyMap.values()]
  .filter(row => row.theory_ids.length && row.lab_ids.length)
  .map(row => ({ ...row, prior_theory: priorTheory.get(`${row.group}:${row.subject}`) || 0 }))

// Static class-hour options are exported for the scheduler extension.  General
// active rooms of type 0/1 are valid even when class_hour_open is absent; only
// a fixed curator room/campus narrows the set.
const classHours = data.groups.filter(g => g.class_hour_enabled !== false && num(g.curator_teacher) >= 0).map(group => {
  const teacher = teacherById.get(num(group.curator_teacher))
  const fixedRoom = num(group.class_hour_room_required ? group.class_hour_room : -1)
  const rooms = data.rooms.filter(room => {
    if (room.active === false || room.access_mode === 'blocked' || room.access_mode === 'exclusive') return false
    if (room.capacity > 0 && group.size > room.capacity) return false
    if (!room.class_hour_open && (room.room_type !== 0 && room.room_type !== 1 || room.purpose)) return false
    if (fixedRoom >= 0 && num(room.id) !== fixedRoom) return false
    if (num(group.class_hour_campus) >= 0 && (group.class_hour_room_required || num(group.curator_teacher) === 26) && num(room.campus) !== num(group.class_hour_campus)) return false
    return true
  }).map(room => ({ id: num(room.id), campus: num(room.campus), name: room.name, fixed: num(room.id) === fixedRoom }))
  const allowedPairs = [0, 2, 3, 4]
  return {
    group: num(group.id), teacher: num(group.curator_teacher), parts: Array.from({ length: Math.max(1, num(group.parts || 2)) }, (_, part) => num(group.id) * 2 + part),
    date: '2026-09-28', allowed_pairs: allowedPairs, fixed_pair: num(group.class_hour_fixed_pair ?? -1),
    fixed_room: fixedRoom, required: true, allowed_rooms: rooms,
    allowed_room_ids: rooms.map(room => room.id), room_campuses: [...new Set(rooms.map(room => room.campus))],
    teacher_external_busy_slots: teacher?.external_busy_slots || [],
  }
})

const model = {
  day_count: 6, slots_per_day: 7, variables, parts, teachers: teacherRows, lab_rules: labRules,
  fixed, class_hours: classHours,
  room_capacity_by_campus: [countRooms(0, false), countRooms(1, false)],
  sports_capacity_by_campus: [countRooms(0, true), countRooms(1, true)],
  // Four is the preferred daily load.  A small number of groups have narrow
  // availability (UP/teacher absences); allowing up to six keeps the weekly
  // 18..21 target feasible while the objective still minimizes this fallback.
  min_student_pairs_per_day: 0, max_student_pairs_per_day: 6, preferred_student_pairs_per_day: 4,
  hard_no_student_windows: true, hard_no_teacher_windows: true,
  whole_group_same_subject_limit: 2, physical_part_same_subject_limit: 3,
  require_same_subgroup_study_days: true, distribution_weeks: 12, workers: 8, time_limit_seconds: 120,
  random_seed: 38, maximize_part_load: true,
}

const modelPath = path.join(outDir, 'quota-model.json')
const candidatePath = path.join(outDir, 'input.json')
fs.writeFileSync(modelPath, JSON.stringify(model, null, 2) + '\n')
fs.writeFileSync(path.join(outDir, 'class-hours.json'), JSON.stringify(classHours, null, 2) + '\n')

const candidate = JSON.parse(JSON.stringify(data))
candidate.meta = { ...(candidate.meta || {}), week_quota_target_per_physical_subgroup: '18..21', week_quota_model: modelPath, class_hours_model: path.join(outDir, 'class-hours.json'), strict_policy_source: sourcePath }
// Keep the runtime solver consistent with the quota feasibility envelope.  A
// two-pair minimum on every active day is impossible for groups with a locked
// full-day UP; zero is allowed and the no-window rule controls continuity.
candidate.settings = candidate.settings || {}
candidate.settings.solver_config = { ...(candidate.settings.solver_config || {}),
  min_student_pairs_per_study_day: 0,
  max_student_pairs_per_day: 6,
  hard_no_student_windows: true,
  hard_no_teacher_windows: true,
}
for (const lesson of candidate.lessons) {
  const row = eligible.find(x => x.id === num(lesson.id))
  lesson.total_slots = 0
  lesson.generation_active = false
  if (row?.locked) { lesson.total_slots = lesson.is_block ? row.locked / 2 : row.locked; lesson.generation_active = true }
}

if (solve) {
  const exe = process.argv.slice(4).find(value => value && value.toLowerCase().endsWith('.exe')) || '.tmp/audit-build-clean/Release/quota_optimizer.exe'
  const raw = JSON.parse(execFileSync(exe, [modelPath], { encoding: 'utf8', maxBuffer: 100 * 1024 * 1024 }))
  fs.writeFileSync(path.join(outDir, 'quota-result.json'), JSON.stringify(raw, null, 2) + '\n')
  if (!raw.success) throw new Error(`quota optimizer: ${raw.status || 'failed'} ${raw.message || ''}`)
  const byId = new Map(variables.map(x => [x.id, x]))
  for (const lesson of candidate.lessons) {
    const row = byId.get(num(lesson.id))
    if (!row) continue
    const slots = num(raw.quotas?.[String(row.id)] || 0)
    lesson.total_slots = lesson.is_block ? slots / 2 : slots
    lesson.generation_active = slots > 0
    if (slots > 0) lesson.week_quota_source = 'strict_18_21_20260928'
  }
}
fs.writeFileSync(candidatePath, JSON.stringify(candidate, null, 2) + '\n')
console.log(JSON.stringify({ source: sourcePath, candidate: candidatePath, model: modelPath, eligible: eligible.length, variables: variables.length, parts: parts.length, class_hours: classHours.length, fixed: fixed.length, solve }, null, 2))
