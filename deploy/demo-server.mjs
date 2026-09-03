import http from 'node:http'
import { pbkdf2Sync, randomBytes, timingSafeEqual } from 'node:crypto'
import { mkdir, readFile, readdir, rename, stat, unlink, writeFile } from 'node:fs/promises'
import { existsSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const appDir = path.dirname(fileURLToPath(import.meta.url))
const distDir = path.join(appDir, 'dist')
const dataDir = path.join(appDir, 'data')
const historyDir = path.join(dataDir, 'history')
const transferBackupsDir = path.join(dataDir, 'transfer-backups')
const sourceDataFile = path.join(dataDir, 'timetable_data.json')
const stateFile = path.join(dataDir, 'demo-state.json')
const scheduleFile = path.join(dataDir, 'schedule_all.json')
const roomAllocationFile = path.join(dataDir, 'room_allocation.json')
const publishedScheduleFile = path.join(dataDir, 'published_schedule.json')
const manualScheduleFile = path.join(dataDir, 'manual_schedule.json')
const reportFiles = {
  room_allocation: roomAllocationFile,
  quality: path.join(dataDir, 'quality_report.json'),
  solver_metrics: path.join(dataDir, 'solver_metrics.json'),
  solver_preflight: path.join(dataDir, 'solver_preflight.json'),
  quota_balance: path.join(dataDir, 'quota_balance.json'),
  quota_runtime_repairs: path.join(dataDir, 'quota_runtime_repairs.json'),
  semester_readout: path.join(dataDir, 'semester_readout_report.json'),
}
const authConfigFile = path.join(dataDir, 'auth_config.json')
const host = process.env.HOST || '0.0.0.0'
const port = Number(process.env.PORT || 4173)

await mkdir(historyDir, { recursive: true })
await mkdir(transferBackupsDir, { recursive: true })
const sourceData = JSON.parse(await readFile(sourceDataFile, 'utf8'))
let schedule = JSON.parse(await readFile(scheduleFile, 'utf8'))
let roomAllocation = existsSync(roomAllocationFile)
  ? JSON.parse(await readFile(roomAllocationFile, 'utf8'))
  : { assigned: 0, substituted: 0, unassigned: 0, conflicts: [], substitutions: [] }
let publishedSchedule = existsSync(publishedScheduleFile)
  ? JSON.parse(await readFile(publishedScheduleFile, 'utf8'))
  : structuredClone(schedule)
let state = existsSync(stateFile)
  ? JSON.parse(await readFile(stateFile, 'utf8'))
  : structuredClone(sourceData)
let manualSchedule = existsSync(manualScheduleFile)
  ? JSON.parse(await readFile(manualScheduleFile, 'utf8'))
  : null

const defaultAuthConfig = {
  username: 'yana_10',
  password_salt: 'd68b78bbec869da6b73b3f62104ca3c5',
  password_hash: 'b148d0a796cebe8018f01cf005670ced314f506710d700b5817459b3139c3a2d',
  iterations: 150000,
}
if (!existsSync(authConfigFile)) {
  await writeFile(authConfigFile, `${JSON.stringify(defaultAuthConfig, null, 2)}\n`, { encoding: 'utf8', mode: 0o600 })
}
let authConfig = JSON.parse(await readFile(authConfigFile, 'utf8'))
const sessions = new Map()
const sessionLifetimeMs = 8 * 60 * 60 * 1000

const collectionNames = {
  groups: 'groups',
  teachers: 'teachers',
  lessons: 'lessons',
  unavailable: 'unavailable',
  'teacher-unavailable': 'teacher_unavailable',
  rooms: 'rooms',
  'room-types': 'room_types',
  substitutions: 'substitutions',
}
for (const key of Object.values(collectionNames)) state[key] ||= []
state.settings ||= {}

const mimeTypes = {
  '.css': 'text/css; charset=utf-8',
  '.html': 'text/html; charset=utf-8',
  '.ico': 'image/x-icon',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
  '.webmanifest': 'application/manifest+json',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2',
}

function json(res, status, value, additionalHeaders = {}) {
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff',
    'X-Frame-Options': 'SAMEORIGIN',
    'Referrer-Policy': 'same-origin',
    ...additionalHeaders,
  })
  res.end(JSON.stringify(value))
}

function parseCookies(req) {
  return Object.fromEntries(String(req.headers.cookie || '').split(';').map((part) => {
    const separator = part.indexOf('=')
    return separator < 0 ? ['', ''] : [part.slice(0, separator).trim(), part.slice(separator + 1).trim()]
  }).filter(([key]) => key))
}

function sessionCookie(token, clear = false) {
  return `raspis_session=${token}; Path=/; HttpOnly; SameSite=Strict; Max-Age=${clear ? 0 : 28800}`
}

function passwordHash(password, config = authConfig) {
  return pbkdf2Sync(String(password), Buffer.from(config.password_salt, 'hex'), Number(config.iterations), 32, 'sha256')
}

function safeEqualText(left, right) {
  const a = Buffer.from(String(left))
  const b = Buffer.from(String(right))
  return a.length === b.length && timingSafeEqual(a, b)
}

function credentialsAreValid(username, password) {
  const storedHash = Buffer.from(authConfig.password_hash, 'hex')
  const calculatedHash = passwordHash(password)
  return safeEqualText(username, authConfig.username)
    && storedHash.length === calculatedHash.length
    && timingSafeEqual(storedHash, calculatedHash)
}

function removeExpiredSessions() {
  const now = Date.now()
  for (const [token, session] of sessions) if (session.expires <= now) sessions.delete(token)
}

function createSession(username) {
  removeExpiredSessions()
  const token = randomBytes(32).toString('hex')
  sessions.set(token, { username, expires: Date.now() + sessionLifetimeMs })
  return token
}

function currentSession(req) {
  removeExpiredSessions()
  const token = parseCookies(req).raspis_session || ''
  const session = sessions.get(token)
  if (!session) return null
  session.expires = Date.now() + sessionLifetimeMs
  return { token, ...session }
}

async function saveAuthConfig(config) {
  await atomicWriteJson(authConfigFile, config)
  const { chmod } = await import('node:fs/promises')
  await chmod(authConfigFile, 0o600)
}

function envelope(message, data) {
  const result = { success: true, message, needs_regenerate: false }
  if (data !== undefined) result.data = data
  return result
}

async function readBody(req) {
  const chunks = []
  let size = 0
  for await (const chunk of req) {
    size += chunk.length
    if (size > 100 * 1024 * 1024) throw new Error('Тело запроса слишком большое (максимум 100 МБ)')
    chunks.push(chunk)
  }
  if (!chunks.length) return {}
  return JSON.parse(Buffer.concat(chunks).toString('utf8'))
}

let versionSequence = 0
function versionStamp() {
  const createdAt = new Date().toISOString()
  versionSequence = (versionSequence + 1) % 1000
  const filenamePart = `${createdAt.replace(/[-:TZ.]/g, '').slice(0, 17)}${String(versionSequence).padStart(3, '0')}`
  return { createdAt, filenamePart }
}

async function atomicWriteJson(filename, value) {
  const temporary = `${filename}.tmp-${process.pid}`
  await writeFile(temporary, JSON.stringify(value, null, 2), 'utf8')
  await rename(temporary, filename)
}

async function backupState(reason) {
  if (!existsSync(stateFile)) return
  const { createdAt, filenamePart } = versionStamp()
  await atomicWriteJson(path.join(historyDir, `version_${filenamePart}.json`), {
    created_at: createdAt,
    reason,
    data: JSON.parse(await readFile(stateFile, 'utf8')),
  })
}

async function saveState(reason = 'Изменение демо-данных') {
  await backupState(reason)
  await atomicWriteJson(stateFile, state)
}

function normalizeStateCollections(candidate) {
  for (const key of Object.values(collectionNames)) candidate[key] ||= []
  candidate.accounting_adjustments ||= []
  candidate.settings ||= {}
  return candidate
}

function isScheduleSnapshot(value) {
  return value === null || Boolean(value && typeof value === 'object' && Array.isArray(value.groups))
}

async function readOptionalJson(filename) {
  if (!existsSync(filename)) return null
  try {
    return JSON.parse(await readFile(filename, 'utf8'))
  } catch {
    return null
  }
}

function transferSummary(data, schedules) {
  return {
    groups: data.groups?.length || 0,
    teachers: data.teachers?.length || 0,
    lessons: data.lessons?.length || 0,
    rooms: data.rooms?.length || 0,
    substitutions: data.substitutions?.length || 0,
    accounting_adjustments: data.accounting_adjustments?.length || 0,
    has_auto_schedule: Boolean(schedules.auto),
    has_manual_schedule: Boolean(schedules.manual),
    has_published_schedule: Boolean(schedules.published),
    hours_recalculated_on_import: true,
  }
}

async function buildTransferBundle(source = 'hosted-site') {
  const schedules = {
    auto: schedule || null,
    manual: manualSchedule || null,
    published: publishedSchedule || null,
  }
  const reports = {}
  for (const [name, filename] of Object.entries(reportFiles)) reports[name] = await readOptionalJson(filename)
  return {
    format: 'raspis-transfer-bundle',
    schema_version: 1,
    exported_at: new Date().toISOString(),
    source,
    data: structuredClone(state),
    schedules: structuredClone(schedules),
    reports,
    summary: transferSummary(state, schedules),
  }
}

function defaultPrimarySchedule(schedules) {
  if (schedules.manual) return 'manual'
  if (schedules.auto) return 'auto'
  if (schedules.published) return 'published'
  return ''
}

async function writeOptionalJson(filename, value) {
  if (value === null || value === undefined) {
    if (existsSync(filename)) await unlink(filename)
    return
  }
  await atomicWriteJson(filename, value)
}

async function createTransferBackup() {
  const { filenamePart } = versionStamp()
  const filename = path.join(transferBackupsDir, `transfer_backup_${filenamePart}.raspis.json`)
  await atomicWriteJson(filename, await buildTransferBundle('pre-import-backup'))
  const files = (await readdir(transferBackupsDir))
    .filter((name) => /^transfer_backup_\d+\.raspis\.json$/.test(name))
    .sort()
  while (files.length > 20) await unlink(path.join(transferBackupsDir, files.shift()))
  return filename
}

function nextId(items) {
  return items.reduce((max, item) => Math.max(max, Number(item.id) || -1), -1) + 1
}

function findScheduleGroup(source, rawValue) {
  const value = decodeURIComponent(rawValue)
  const asNumber = Number(value)
  return source.groups.find((group) => Number.isInteger(asNumber)
    ? group.group_index === asNumber
    : group.group_name.toLocaleLowerCase('ru') === value.toLocaleLowerCase('ru'))
}

function isoDate(value) {
  if (!value) return ''
  if (/^\d{4}-\d{2}-\d{2}$/.test(value)) return value
  const match = String(value).match(/^(\d{2})\.(\d{2})\.(\d{4})$/)
  return match ? `${match[3]}-${match[2]}-${match[1]}` : ''
}

function addDays(value, days) {
  const date = new Date(`${value}T00:00:00Z`)
  date.setUTCDate(date.getUTCDate() + days)
  return date.toISOString().slice(0, 10)
}

function differenceDays(from, to) {
  return Math.floor((new Date(`${to}T00:00:00Z`) - new Date(`${from}T00:00:00Z`)) / 86400000)
}

function semesterWeeks() {
  const dates = schedule.groups.flatMap((group) => group.days.map((day) => isoDate(day.date_iso || day.date))).filter(Boolean).sort()
  const start = state.settings?.start_date || dates[0] || new Date().toISOString().slice(0, 10)
  const end = state.settings?.end_date || dates.at(-1) || start
  const result = []
  let cursor = start
  let index = 1
  while (cursor <= end && index <= 60) {
    const naturalEnd = addDays(cursor, 6)
    result.push({ index, from: cursor, to: naturalEnd > end ? end : naturalEnd })
    cursor = addDays(cursor, 7)
    index += 1
  }
  return result
}

function activeSubstitution(lessonId, date, slot) {
  return state.substitutions.find((item) => item.status !== 'cancelled'
    && Number(item.lesson_id) === Number(lessonId)
    && item.date === date
    && Number(item.slot) === Number(slot))
}

function scheduleOccurrences() {
  const start = state.settings?.start_date || ''
  const teachers = new Map(state.teachers.map((item) => [Number(item.id), item.name]))
  const lessons = new Map(state.lessons.map((item) => [Number(item.id), item]))
  const occurrences = []
  for (const group of schedule.groups || []) {
    for (const day of group.days || []) {
      const date = isoDate(day.date_iso || day.date)
      for (const slot of day.slots || []) {
        for (const scheduledLesson of slot.lessons || []) {
          const lesson = lessons.get(Number(scheduledLesson.id)) || scheduledLesson
          const teacherId = Number.isInteger(Number(scheduledLesson.teacher_id)) && scheduledLesson.teacher_id !== null
            ? Number(scheduledLesson.teacher_id)
            : Number(lesson.teacher ?? -1)
          const substitution = activeSubstitution(scheduledLesson.id, date, slot.slot)
          const actualTeacherId = substitution ? Number(substitution.substitute_teacher) : teacherId
          const weekIndex = start ? Math.max(1, Math.floor(differenceDays(start, date) / 7) + 1) : 1
          occurrences.push({
            actual_teacher_id: actualTeacherId,
            actual_teacher_name: teachers.get(actualTeacherId) || (actualTeacherId < 0 ? 'вакансия' : ''),
            date,
            group_id: Number(group.group_index),
            group_name: group.group_name,
            hours: 2,
            is_substitution: Boolean(substitution),
            lesson_id: Number(scheduledLesson.id),
            lesson_name: scheduledLesson.name || lesson.name || '',
            requested_room: scheduledLesson.requested_room_name || '',
            room: scheduledLesson.room_name || '',
            room_substituted: Boolean(scheduledLesson.room_substituted),
            room_substitution_reason: scheduledLesson.room_substitution_reason || '',
            room_type: Number(scheduledLesson.room_type || lesson.required_room_type || 1),
            slot: Number(slot.slot),
            teacher_id: teacherId,
            teacher_name: teachers.get(teacherId) || (teacherId < 0 ? 'вакансия' : ''),
            week_index: weekIndex,
          })
        }
      }
    }
  }
  return occurrences
}

function sum(items, selector) {
  return items.reduce((total, item) => total + Number(selector(item) || 0), 0)
}

function validateFinalOutput(data, scheduleSnapshot, allocationReport) {
  const scheduledByLesson = new Map()
  for (const group of scheduleSnapshot?.groups || []) {
    for (const day of group.days || []) {
      for (const slot of day.slots || []) {
        for (const lesson of slot.lessons || []) {
          const id = Number(lesson.id)
          if (!Number.isInteger(id) || id < 0) continue
          scheduledByLesson.set(id, (scheduledByLesson.get(id) || 0) + 1)
        }
      }
    }
  }
  let remainingHours = 0
  let incompleteLessons = 0
  for (const lesson of data?.lessons || []) {
    if (lesson.plan_active === false || lesson.generation_active === false) continue
    const planned = Number(lesson.total_slots || 0) * (lesson.is_block ? 4 : 2)
    const remaining = planned - (scheduledByLesson.get(Number(lesson.id)) || 0) * 2
    if (remaining <= 0) continue
    remainingHours += remaining
    incompleteLessons++
  }
  const unassignedRooms = Math.max(0, Number(allocationReport?.unassigned || 0))
  const ok = remainingHours === 0 && unassignedRooms === 0
  const problems = []
  if (remainingHours > 0) problems.push(`осталось ${remainingHours} ч. по ${incompleteLessons} занятиям`)
  if (unassignedRooms > 0) problems.push(`без кабинета ${unassignedRooms} событий`)
  return {
    checked: true,
    ok,
    remaining_hours: remainingHours,
    incomplete_lessons: incompleteLessons,
    unassigned_rooms: unassignedRooms,
    hours_source: 'active_total_slots_fallback',
    message: ok
      ? 'Итоговая проверка пройдена: часы закрыты, кабинеты назначены'
      : `Итоговая проверка не пройдена: ${problems.join('; ')}`,
  }
}

function weeklyHours(occurrences, weeks) {
  return weeks.map((week) => sum(occurrences.filter((item) => item.week_index === week.index), (item) => item.hours))
}

function buildHours() {
  const occurrences = scheduleOccurrences()
  const weeks = semesterWeeks()
  const lessons = state.lessons.map((lesson) => {
    const scheduled = occurrences.filter((item) => item.lesson_id === Number(lesson.id))
    const planned = Number(lesson.total_hours ?? (Number(lesson.total_slots || 0) * 2))
    const scheduledHours = sum(scheduled, (item) => item.hours)
    return {
      credited_hours: scheduledHours,
      group_id: Number(lesson.group),
      group_name: state.groups.find((item) => Number(item.id) === Number(lesson.group))?.name || `Группа ${lesson.group}`,
      is_block: Boolean(lesson.is_block),
      is_lab: Boolean(lesson.is_lab),
      lesson_id: Number(lesson.id),
      lesson_uid: lesson.uid || '',
      name: lesson.name || '',
      planned_hours: planned,
      remaining_hours: Math.max(0, planned - scheduledHours),
      scheduled_hours: scheduledHours,
      scheduled_occurrences: scheduled,
      subgroup: Number(lesson.subgroup ?? -1),
      subject_id: Number(lesson.subject_id ?? lesson.id),
      teacher_id: Number(lesson.teacher ?? -1),
      teacher_name: state.teachers.find((item) => Number(item.id) === Number(lesson.teacher))?.name || (Number(lesson.teacher) < 0 ? 'вакансия' : ''),
    }
  })
  const groups = state.groups.map((group) => {
    const plannedRows = lessons.filter((item) => item.group_id === Number(group.id))
    const scheduled = occurrences.filter((item) => item.group_id === Number(group.id))
    const planned = sum(plannedRows, (item) => item.planned_hours)
    const scheduledHours = sum(scheduled, (item) => item.hours)
    return {
      adjustment_hours: 0,
      credited_hours: scheduledHours,
      group_id: Number(group.id),
      group_name: group.name,
      planned_hours: planned,
      remaining_hours: Math.max(0, planned - scheduledHours),
      scheduled_hours: scheduledHours,
      scheduled_occurrences: scheduled,
      substitution_in_hours: 0,
      substitution_out_hours: 0,
      weekly_hours: weeklyHours(scheduled, weeks),
    }
  })
  const teachers = state.teachers.map((teacher) => {
    const teacherId = Number(teacher.id)
    const planned = sum(lessons.filter((item) => item.teacher_id === teacherId), (item) => item.planned_hours)
    const scheduled = occurrences.filter((item) => item.teacher_id === teacherId)
    const credited = occurrences.filter((item) => item.actual_teacher_id === teacherId)
    const scheduledHours = sum(scheduled, (item) => item.hours)
    const creditedHours = sum(credited, (item) => item.hours)
    const substitutionIn = sum(state.substitutions.filter((item) => item.status !== 'cancelled' && Number(item.substitute_teacher) === teacherId), (item) => item.hours)
    const substitutionOut = sum(state.substitutions.filter((item) => item.status !== 'cancelled' && Number(item.absent_teacher) === teacherId), (item) => item.hours)
    return {
      adjustment_hours: 0,
      credited_hours: creditedHours,
      credited_occurrences: credited,
      planned_hours: planned,
      remaining_hours: Math.max(0, planned - creditedHours),
      scheduled_hours: scheduledHours,
      scheduled_occurrences: scheduled,
      substitution_in_hours: substitutionIn,
      substitution_out_hours: substitutionOut,
      teacher_id: teacherId,
      teacher_name: teacher.name,
      weekly_hours: weeklyHours(scheduled, weeks),
    }
  })
  return {
    groups,
    lessons,
    schedule_found: Boolean(schedule.groups?.length),
    semester_end: state.settings?.end_date || '',
    semester_start: state.settings?.start_date || '',
    teachers,
    weeks,
  }
}

function buildOccupancy() {
  const entries = scheduleOccurrences().map((item) => ({
    date: item.date,
    group_id: item.group_id,
    group_name: item.group_name,
    is_substitution: item.is_substitution,
    lesson_id: item.lesson_id,
    lesson_name: item.lesson_name,
    original_teacher_id: item.teacher_id,
    room: item.room,
    slot: item.slot,
    teacher_id: item.actual_teacher_id,
    teacher_name: item.actual_teacher_name,
    weekday: new Date(`${item.date}T00:00:00Z`).getUTCDay() || 7,
  }))
  return { entries, schedule_found: Boolean(schedule.groups?.length) }
}

function buildAudit(candidate = state) {
  const issues = []
  const add = (severity, code, message, entityType = '', entityId = null) => issues.push({ severity, code, message, entity_type: entityType, entity_id: entityId })
  for (const [name, items] of Object.entries({ group: candidate.groups || [], teacher: candidate.teachers || [], lesson: candidate.lessons || [], room: candidate.rooms || [] })) {
    const ids = new Set()
    for (const item of items) {
      if (ids.has(Number(item.id))) add('error', 'duplicate_id', `Повторяющийся ID ${item.id}`, name, item.id)
      ids.add(Number(item.id))
    }
  }
  const groups = new Set((candidate.groups || []).map((item) => Number(item.id)))
  const teachers = new Set((candidate.teachers || []).map((item) => Number(item.id)))
  const roomTypes = new Set((candidate.room_types || []).map((item) => Number(item.id)))
  for (const group of candidate.groups || []) {
    if (!Number(group.size)) add('info', 'group_size_unknown', `Не указана численность группы ${group.name}`, 'group', group.id)
  }
  for (const lesson of candidate.lessons || []) {
    if (!groups.has(Number(lesson.group))) add('error', 'lesson_group_missing', `Для занятия «${lesson.name}» не найдена группа`, 'lesson', lesson.id)
    if (Number(lesson.teacher) >= 0 && !teachers.has(Number(lesson.teacher))) add('error', 'lesson_teacher_missing', `Для занятия «${lesson.name}» не найден преподаватель`, 'lesson', lesson.id)
    if (!roomTypes.has(Number(lesson.required_room_type || 1))) add('warning', 'room_type_missing', `Для занятия «${lesson.name}» не найден тип аудитории`, 'lesson', lesson.id)
  }
  if (!(candidate.rooms || []).length) add('warning', 'rooms_empty', 'Аудиторный фонд пока не заполнен')
  return {
    issues,
    ok: !issues.some((item) => item.severity === 'error'),
    summary: {
      errors: issues.filter((item) => item.severity === 'error').length,
      groups: (candidate.groups || []).length,
      info: issues.filter((item) => item.severity === 'info').length,
      lessons: (candidate.lessons || []).length,
      room_types: (candidate.room_types || []).length,
      rooms: (candidate.rooms || []).length,
      semester_slot_capacity: semesterWeeks().length * 6 * 7,
      teachers: (candidate.teachers || []).length,
      warnings: issues.filter((item) => item.severity === 'warning').length,
    },
  }
}

function normalizeCreatedAt(value, fallback) {
  const text = String(value || '')
  const compact = text.match(/^(\d{4})(\d{2})(\d{2})(\d{2})(\d{2})(\d{2})(\d{3})/)
  if (compact) return `${compact[1]}-${compact[2]}-${compact[3]}T${compact[4]}:${compact[5]}:${compact[6]}.${compact[7]}Z`
  return Number.isNaN(Date.parse(text)) ? fallback : text
}

async function listVersions() {
  const result = []
  for (const filename of await readdir(historyDir)) {
    if (!/^version_\d+\.json$/.test(filename)) continue
    const fullPath = path.join(historyDir, filename)
    try {
      const entry = JSON.parse(await readFile(fullPath, 'utf8'))
      const info = await stat(fullPath)
      result.push({ created_at: normalizeCreatedAt(entry.created_at, info.mtime.toISOString()), filename, reason: entry.reason || 'Изменение данных', size: info.size })
    } catch {}
  }
  return result.sort((a, b) => Date.parse(b.created_at) - Date.parse(a.created_at)).slice(0, 50)
}

async function handleApi(req, res, pathname) {
  const method = req.method || 'GET'

  if (method === 'OPTIONS') return json(res, 200, {})
  if (method === 'GET' && (pathname === '/api' || pathname === '/api/health')) {
    return json(res, 200, {
      name: 'Расписание Юность — демонстрационный API',
      mode: 'demo',
      solver: false,
      generation_enabled: false,
      groups: schedule.groups.length,
    })
  }

  if (pathname === '/api/auth/login' && method === 'POST') {
    const body = await readBody(req)
    if (!credentialsAreValid(body.username, body.password)) {
      return json(res, 401, { success: false, message: 'Неверный логин или пароль' })
    }
    const token = createSession(authConfig.username)
    return json(res, 200, { success: true, username: authConfig.username }, {
      'Set-Cookie': sessionCookie(token),
    })
  }

  if (pathname === '/api/auth/status' && method === 'GET') {
    const session = currentSession(req)
    return json(res, 200, session
      ? { authenticated: true, username: session.username }
      : { authenticated: false })
  }

  if (pathname === '/api/auth/logout' && method === 'POST') {
    const session = currentSession(req)
    if (session) sessions.delete(session.token)
    return json(res, 200, { success: true }, { 'Set-Cookie': sessionCookie('', true) })
  }

  // Опубликованная студенческая версия остаётся доступна без входа.
  if (pathname === '/api/schedule/published' && method === 'GET') {
    return json(res, 200, publishedSchedule)
  }

  const session = currentSession(req)
  if (!session) return json(res, 401, { success: false, message: 'Требуется вход диспетчера' })

  if (pathname === '/api/auth/credentials' && method === 'PUT') {
    const body = await readBody(req)
    const newUsername = String(body.new_username || '').trim()
    const newPassword = String(body.new_password || '')
    if (!credentialsAreValid(session.username, body.current_password)) {
      return json(res, 400, { success: false, message: 'Текущий пароль указан неверно' })
    }
    if (newUsername.length < 3 || newUsername.length > 64 || /\s/.test(newUsername)) {
      return json(res, 400, { success: false, message: 'Логин: от 3 до 64 символов, без пробелов' })
    }
    if (newPassword.length < 8 || newPassword.length > 128) {
      return json(res, 400, { success: false, message: 'Пароль должен содержать от 8 до 128 символов' })
    }
    const salt = randomBytes(16).toString('hex')
    const updated = {
      username: newUsername,
      password_salt: salt,
      password_hash: pbkdf2Sync(newPassword, Buffer.from(salt, 'hex'), 150000, 32, 'sha256').toString('hex'),
      iterations: 150000,
    }
    await saveAuthConfig(updated)
    authConfig = updated
    sessions.clear()
    const token = createSession(newUsername)
    return json(res, 200, { success: true, username: newUsername }, {
      'Set-Cookie': sessionCookie(token),
    })
  }

  if (pathname === '/api/transfer/export' && method === 'GET') {
    const bundle = await buildTransferBundle('hosted-site')
    const stamp = new Date().toISOString().replace(/[-:TZ.]/g, '').slice(0, 14)
    return json(res, 200, bundle, {
      'Content-Disposition': `attachment; filename="raspis-full-${stamp}.raspis.json"`,
    })
  }

  if (pathname === '/api/transfer/import' && method === 'POST') {
    const requestBody = await readBody(req)
    const bundle = requestBody?.bundle && typeof requestBody.bundle === 'object'
      ? requestBody.bundle : requestBody
    if (bundle?.format !== 'raspis-transfer-bundle' || Number(bundle?.schema_version) !== 1) {
      return json(res, 400, { success: false, message: 'Это не поддерживаемый пакет переноса расписания' })
    }
    const importedData = bundle.data
    const schedules = bundle.schedules
    if (!importedData || !Array.isArray(importedData.groups)
      || !Array.isArray(importedData.teachers) || !Array.isArray(importedData.lessons)) {
      return json(res, 400, { success: false, message: 'В пакете отсутствует полная база групп, преподавателей и занятий' })
    }
    if (!schedules || !isScheduleSnapshot(schedules.auto)
      || !isScheduleSnapshot(schedules.manual) || !isScheduleSnapshot(schedules.published)) {
      return json(res, 400, { success: false, message: 'В пакете повреждён раздел расписаний' })
    }
    const audit = buildAudit(importedData)
    if (!audit.ok) {
      return json(res, 422, {
        ...audit,
        success: false,
        message: 'Пакет не применён: аудит импортируемой базы обнаружил ошибки',
      })
    }
    const primarySchedule = String(requestBody.primary_schedule || defaultPrimarySchedule(schedules))
    if (!['auto', 'manual', 'published'].includes(primarySchedule) || !schedules[primarySchedule]) {
      return json(res, 400, { success: false, message: 'Выбранный вариант расписания отсутствует в пакете' })
    }
    const publish = requestBody.publish !== false
    const selectedSchedule = structuredClone(schedules[primarySchedule])
    const validation = validateFinalOutput(importedData, selectedSchedule, bundle.reports?.room_allocation)
    if (publish && !validation.ok) {
      return json(res, 422, { success: false, message: validation.message, data: validation })
    }
    const backupFile = await createTransferBackup()

    state = normalizeStateCollections(structuredClone(importedData))
    schedule = selectedSchedule
    manualSchedule = schedules.manual ? structuredClone(schedules.manual) : null
    publishedSchedule = publish
      ? structuredClone(selectedSchedule)
      : (schedules.published ? structuredClone(schedules.published) : structuredClone(selectedSchedule))

    await saveState('Импорт полного пакета с desktop-приложения')
    await atomicWriteJson(scheduleFile, schedule)
    await writeOptionalJson(manualScheduleFile, manualSchedule)
    await atomicWriteJson(publishedScheduleFile, publishedSchedule)
    for (const [name, filename] of Object.entries(reportFiles)) {
      await writeOptionalJson(filename, bundle.reports?.[name] ?? null)
    }
    roomAllocation = bundle.reports?.room_allocation || {
      assigned: 0, substituted: 0, unassigned: 0, conflicts: [], substitutions: [],
    }

    return json(res, 200, {
      success: true,
      message: 'Полный пакет применён. Учёт часов пересчитан по перенесённому расписанию и заменам.',
      primary_schedule: primarySchedule,
      published: publish,
      validation,
      backup_file: path.basename(backupFile),
      summary: transferSummary(state, schedules),
    })
  }

  if (pathname === '/api/data') {
    if (method === 'GET') return json(res, 200, state)
    if (method === 'PUT') {
      const replacement = await readBody(req)
      if (!replacement || !Array.isArray(replacement.groups) || !Array.isArray(replacement.teachers) || !Array.isArray(replacement.lessons)) {
        return json(res, 400, { success: false, message: 'Некорректная структура импортируемых данных' })
      }
      state = replacement
      for (const key of Object.values(collectionNames)) state[key] ||= []
      state.settings ||= {}
      await saveState('Импорт данных через интерфейс')
      return json(res, 200, envelope('Демо-данные импортированы.', state))
    }
  }

  if (pathname === '/api/audit' && (method === 'GET' || method === 'POST')) {
    const candidate = method === 'POST' ? await readBody(req) : state
    return json(res, 200, buildAudit(candidate))
  }
  if (pathname === '/api/hours' && method === 'GET') return json(res, 200, buildHours())
  if (pathname === '/api/accounting/teacher-occupancy' && method === 'GET') return json(res, 200, buildOccupancy())
  if (pathname === '/api/versions' && method === 'GET') return json(res, 200, await listVersions())
  const restoreMatch = pathname.match(/^\/api\/versions\/([^/]+)\/restore$/)
  if (restoreMatch && method === 'POST') {
    const filename = decodeURIComponent(restoreMatch[1])
    if (!/^version_\d+\.json$/.test(filename)) return json(res, 400, { success: false, message: 'Некорректное имя версии' })
    const versionPath = path.join(historyDir, filename)
    if (!existsSync(versionPath)) return json(res, 404, { success: false, message: 'Версия не найдена' })
    const entry = JSON.parse(await readFile(versionPath, 'utf8'))
    await backupState('Перед восстановлением версии')
    state = entry.data || entry
    await atomicWriteJson(stateFile, state)
    return json(res, 200, envelope('Версия восстановлена.', state))
  }

  if (pathname === '/api/schedule' && method === 'GET') return json(res, 200, schedule)
  if (pathname === '/api/schedule/published' && method === 'GET') return json(res, 200, publishedSchedule)
  if (pathname === '/api/schedule/publish' && method === 'POST') {
    const validation = validateFinalOutput(state, schedule, roomAllocation)
    if (!validation.ok) return json(res, 409, { success: false, message: validation.message, data: validation })
    publishedSchedule = structuredClone(schedule)
    await atomicWriteJson(publishedScheduleFile, publishedSchedule)
    return json(res, 200, { ...envelope('Тестовое расписание опубликовано для студенческой версии.'), validation })
  }
  if (pathname === '/api/schedule/rooms' && method === 'GET') {
    return json(res, 200, roomAllocation)
  }

  const scheduleGroupMatch = pathname.match(/^\/api\/schedule\/group\/(.+)$/)
  if (scheduleGroupMatch && method === 'GET') {
    const group = findScheduleGroup(schedule, scheduleGroupMatch[1])
    return group ? json(res, 200, group) : json(res, 404, { success: false, message: 'Группа не найдена' })
  }

  if (pathname === '/api/schedule/regenerate' && method === 'POST') {
    return json(res, 403, { success: false, disabled: true, message: 'Генерация отключена на публичном демонстрационном сервере.' })
  }
  if (pathname === '/api/schedule/progress' && method === 'GET') {
    return json(res, 200, { state: 'disabled', message: 'Генерация отключена', total_weeks: 0, solved_weeks: 0 })
  }
  if (pathname === '/api/schedule/cancel' && method === 'POST') {
    return json(res, 403, { success: false, disabled: true, message: 'Генерация отключена' })
  }

  if (pathname === '/api/schedule/manual') {
    if (method === 'GET') return manualSchedule ? json(res, 200, manualSchedule) : json(res, 404, { success: false, message: 'Ручное расписание пока пусто.' })
    if (method === 'POST') {
      manualSchedule = await readBody(req)
      await atomicWriteJson(manualScheduleFile, manualSchedule)
      return json(res, 200, envelope('Ручное демо-расписание сохранено.'))
    }
    if (method === 'DELETE') {
      manualSchedule = null
      if (existsSync(manualScheduleFile)) await unlink(manualScheduleFile)
      return json(res, 200, envelope('Ручное демо-расписание очищено.'))
    }
  }
  if (pathname === '/api/schedule/manual/copy-from-auto' && method === 'POST') {
    manualSchedule = structuredClone(schedule)
    await atomicWriteJson(manualScheduleFile, manualSchedule)
    return json(res, 200, envelope('Тестовое расписание скопировано в Конструктор.'))
  }

  if (pathname === '/api/settings' && method === 'GET') return json(res, 200, state.settings || {})
  if (pathname === '/api/settings' && (method === 'PATCH' || method === 'PUT')) {
    const body = await readBody(req)
    state.settings = method === 'PATCH' ? { ...(state.settings || {}), ...body } : body
    await saveState('Изменение настроек')
    return json(res, 200, envelope('Настройки демо сохранены.', state.settings))
  }
  if (pathname.startsWith('/api/settings/solver-config')) {
    return json(res, 403, { success: false, disabled: true, message: 'Параметры решателя недоступны в демонстрационном режиме.' })
  }

  const bulkMatch = pathname.match(/^\/api\/(groups|teachers)\/bulk$/)
  if (bulkMatch && method === 'PATCH') {
    const body = await readBody(req)
    const items = state[collectionNames[bulkMatch[1]]]
    const ids = new Set((body.ids || []).map(Number))
    let count = 0
    for (let index = 0; index < items.length; index += 1) {
      if (body.all || ids.has(Number(items[index].id))) {
        items[index] = { ...items[index], ...(body.patch || {}), id: items[index].id }
        count += 1
      }
    }
    await saveState(`Массовое изменение: ${bulkMatch[1]}`)
    return json(res, 200, envelope(`Обновлено записей: ${count}`, { updated_count: count }))
  }

  const collectionMatch = pathname.match(/^\/api\/(groups|teachers|lessons|unavailable|teacher-unavailable|rooms|room-types|substitutions)(?:\/(\d+))?$/)
  if (collectionMatch) {
    const [, publicName, idText] = collectionMatch
    const collection = collectionNames[publicName]
    const items = state[collection]
    if (!idText && method === 'GET') return json(res, 200, items)
    if (!idText && method === 'POST') {
      const item = await readBody(req)
      if (item.id === undefined) item.id = nextId(items)
      items.push(item)
      await saveState(`Добавление записи: ${publicName}`)
      return json(res, 201, envelope('Сохранено в демо-режиме.', item))
    }
    const id = Number(idText)
    const index = items.findIndex((item) => Number(item.id) === id)
    if (index < 0) return json(res, 404, { success: false, message: 'Запись не найдена' })
    if (method === 'GET') return json(res, 200, items[index])
    if (method === 'DELETE') {
      items.splice(index, 1)
      await saveState(`Удаление записи: ${publicName}`)
      return json(res, 200, envelope('Удалено из демо-данных.'))
    }
    if (method === 'PUT' || method === 'PATCH') {
      const body = await readBody(req)
      items[index] = method === 'PATCH' ? { ...items[index], ...body, id } : { ...body, id }
      await saveState(`Изменение записи: ${publicName}`)
      return json(res, 200, envelope('Обновлено в демо-режиме.', items[index]))
    }
  }

  if (pathname === '/api/accounting/substitutions.csv' && method === 'GET') {
    const escape = (value) => `"${String(value ?? '').replaceAll('"', '""')}"`
    const columns = ['date', 'slot', 'lesson_id', 'absent_teacher', 'substitute_teacher', 'hours', 'reason', 'status']
    const lines = [columns.join(';'), ...state.substitutions.map((item) => columns.map((key) => escape(item[key])).join(';'))]
    res.writeHead(200, {
      'Content-Type': 'text/csv; charset=utf-8',
      'Content-Disposition': 'attachment; filename="substitutions.csv"',
      'Cache-Control': 'no-store',
    })
    return res.end(`\uFEFF${lines.join('\r\n')}`)
  }

  return json(res, 404, { success: false, message: 'Демо-endpoint не найден' })
}

async function serveStatic(req, res, pathname) {
  let relative = decodeURIComponent(pathname).replace(/^\/+/, '')
  if (!relative) relative = 'index.html'
  let filePath = path.resolve(distDir, relative)
  const resolvedDist = path.resolve(distDir)
  if (filePath !== resolvedDist && !filePath.startsWith(`${resolvedDist}${path.sep}`)) return json(res, 403, { success: false, message: 'Недопустимый путь' })
  if (!existsSync(filePath) || path.extname(filePath) === '') filePath = path.join(distDir, 'index.html')
  try {
    const body = await readFile(filePath)
    const immutable = filePath.includes(`${path.sep}assets${path.sep}`)
    res.writeHead(200, {
      'Content-Type': mimeTypes[path.extname(filePath)] || 'application/octet-stream',
      'Cache-Control': filePath.endsWith('index.html') ? 'no-cache' : immutable ? 'public, max-age=31536000, immutable' : 'public, max-age=3600',
      'X-Content-Type-Options': 'nosniff',
      'X-Frame-Options': 'SAMEORIGIN',
      'Referrer-Policy': 'same-origin',
    })
    res.end(req.method === 'HEAD' ? undefined : body)
  } catch {
    json(res, 404, { success: false, message: 'Файл не найден' })
  }
}

const server = http.createServer(async (req, res) => {
  try {
    const pathname = new URL(req.url || '/', `http://${req.headers.host || 'localhost'}`).pathname
    if (pathname === '/health') return json(res, 200, { ok: true, mode: 'demo', generation_enabled: false })
    if (pathname.startsWith('/api')) return await handleApi(req, res, pathname)
    if (req.method !== 'GET' && req.method !== 'HEAD') return json(res, 405, { success: false, message: 'Метод не поддерживается' })
    return await serveStatic(req, res, pathname)
  } catch (error) {
    console.error(error)
    return json(res, 500, { success: false, message: error.message || 'Ошибка демо-сервера' })
  }
})

server.listen(port, host, () => {
  console.log(`Расписание Юность (demo): http://${host}:${port}`)
  console.log(`Групп в тестовом расписании: ${schedule.groups.length}`)
  console.log('Генерация: отключена')
})
