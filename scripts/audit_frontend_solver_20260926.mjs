// Isolated audit probes. Uses a fresh temporary database, never the working one.
import fs from 'node:fs'
import path from 'node:path'
import os from 'node:os'
import net from 'node:net'
import vm from 'node:vm'
import { spawn, spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { teacherFormFromEntity, teacherPayloadFromForm, roomFormFromEntity, roomPayloadFromForm, lessonFormFromEntity, lessonPayloadFromForm } from '../frontend/src/utils/entityPayloads.js'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const solver = path.resolve(process.argv[2] || path.join(root, '.tmp/audit-build-clean/Release/timetable_solver.exe'))
const workspace = fs.mkdtempSync(path.join(root, '.tmp/audit-contract-'))
fs.mkdirSync(path.join(workspace, 'data'))
fs.writeFileSync(path.join(workspace, 'data/auth_config.json'), JSON.stringify({
  username: 'fake_grid', password_salt: '00112233445566778899aabbccddeeff',
  password_hash: 'b866e38c02150905dcadf54bf7eddfd833b09c541192b974ea5ea48bd64680d8', iterations: 150000,
}))
const port = await new Promise(resolve => { const s = net.createServer(); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => resolve(p)) }) })
const log = fs.openSync(path.join(workspace, 'server.log'), 'w')
const child = spawn(solver, [String(port)], { cwd: workspace, windowsHide: true, stdio: ['ignore', log, log] })
const nativeFetch = globalThis.fetch
let cookie = ''
globalThis.window = { dispatchEvent() {} }
globalThis.fetch = (url, options = {}) => nativeFetch(typeof url === 'string' && url.startsWith('/api') ? `http://127.0.0.1:${port}${url}` : url,
  { ...options, headers: { ...options.headers, ...(cookie ? { Cookie: cookie } : {}) }, signal: AbortSignal.timeout(30000) })
const { api } = await import('../frontend/src/api/index.js')
const results = []
const record = (name, observed) => { results.push({ name, ...observed }); console.log(JSON.stringify(results.at(-1))) }
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms))
const workDays = () => Array.from({ length: 7 }, (_, i) => ({ day: i + 1, enabled: i < 6, start_slot: 1, end_slot: 7 }))
const base = () => ({
  settings: { start_date: '2026-09-15', end_date: '2026-09-15', automatic_period_quotas: false,
    solver_config: { week_time_limit_seconds: 5, solver_workers: 1, quality_improvement_seconds: 0,
      min_student_pairs_per_study_day: 1, max_student_pairs_per_day: 5 } },
  groups: [{ id: 0, name: 'TEST-201', parts: 1, home_campus: 0, class_hour_enabled: false, work_days: workDays() }],
  teachers: [{ id: 0, name: 'Test Teacher', work_days: workDays() }],
  rooms: [{ id: 0, name: '101', campus: 0, active: true }],
  lessons: [{ id: 0, name: 'Test Subject', group: 0, subgroup: -1, teacher: 0, subject_id: 0,
    total_hours: 2, total_slots: 1, allowed_campuses: [0], is_lab: false, is_block: false, is_pp: false }],
  teacher_unavailable: [], unavailable: [], teaching_ledger: [],
})
async function reset(data) {
  const res = await api.data.replace(data)
  if (!res.ok) throw new Error(`Cannot reset fixture: ${JSON.stringify(res)}`)
}
async function generate(options = {}) {
  const start = await api.schedule.regenerate(options)
  if (!start.ok) return { http: start.status, response: start.data }
  for (let i = 0; i < 300; i++) {
    const res = await api.schedule.progress()
    if (['done', 'failed', 'cancelled'].includes(res.data?.state)) {
      let events = []
      if (res.data.state === 'done') {
        const schedule = await api.schedule.get()
        events = (schedule.data?.groups || []).flatMap(g => g.days.flatMap(d => d.slots.flatMap(s => (s.lessons || []).map(l => ({ date: d.date_iso, slot: s.slot, ...l })))))
      }
      return { http: start.status, state: res.data.state, message: res.data.message, events }
    }
    await sleep(100)
  }
  throw new Error('Generation timeout')
}
try {
  for (let i = 0; i < 100; i++) { try { const r = await api.auth.status(); if (r.ok) break } catch {} await sleep(100) }
  const login = await nativeFetch(`http://127.0.0.1:${port}/api/auth/login`, { method: 'POST', body: JSON.stringify({ username: 'fake_grid', password: 'fake-grid-pass' }) })
  cookie = login.headers.get('set-cookie')?.split(';')[0]
  if (!cookie) throw new Error('Test login failed')

  const groupData = base()
  Object.assign(groupData.groups[0], { course_year: 2, class_hour_room: 0, academic_calendar: [{ from: '2026-09-14', to: '2026-09-20', theory_hours: 20, up_hours: 0, pp_hours: 0, exam_hours: 0 }] })
  await reset(groupData)
  const before = (await api.groups.list()).data[0]
  const groupSource = fs.readFileSync(path.join(root, 'frontend/src/views/GroupsView.vue'), 'utf8')
  const groupSave = groupSource.slice(groupSource.indexOf('async function save()'), groupSource.indexOf('async function saveBulk()'))
  const context = { form: { value: structuredClone(before) }, editItem: { value: before }, saving: { value: false }, modalOpen: { value: true }, readoutRevision: { value: 0 }, toast: { success() {}, error() {} }, store: { updateGroup: api.groups.update } }
  await vm.runInNewContext(`${groupSave}\nsave()`, context)
  const after = (await api.groups.list()).data[0]
  record('actual_group_save_loses_fields', { missing: ['academic_calendar', 'course_year', 'class_hour_room'].filter(k => !(k in after)), calendarBefore: before.academic_calendar.length })

  await reset(base())
  const bulkSave = groupSource.slice(groupSource.indexOf('async function saveBulk()'), groupSource.indexOf('async function doDelete()'))
  await vm.runInNewContext(`${bulkSave}\nsaveBulk()`, { saving: { value: false }, selected: { value: [0] }, bulkForm: { value: { work_period: { from: '', to: '' }, work_days: workDays(), date_slot_overrides: [{ date: '2026-09-15', slots: [] }] } }, bulkModal: { value: true }, store: { bulkUpdateGroups: api.groups.bulkUpdate }, toast: { success() {}, error() {} } })
  record('actual_group_bulk_save_loses_date_override', { savedOverrides: (await api.groups.list()).data[0].date_slot_overrides ?? null })

  await reset(base())
  const teacher = { ...(await api.teachers.list()).data[0], allowed_campuses: [0], max_pairs_per_day: 3, max_work_days_per_week: 2, date_load_targets: [{ date: '2026-09-15', minimum_pairs: 1 }], desired_load_rules: [{ minimum_pairs_per_week: 1 }], date_slot_overrides: [{ date: '2026-09-15', slots: [2, 3] }] }
  await api.teachers.update(0, teacherPayloadFromForm(teacherFormFromEntity(teacher)))
  const teacherSaved = (await api.teachers.list()).data[0]
  record('teacher_payload_round_trip', { equal: ['allowed_campuses','max_pairs_per_day','max_work_days_per_week','date_load_targets','desired_load_rules','date_slot_overrides'].every(k => JSON.stringify(teacher[k]) === JSON.stringify(teacherSaved[k])) })
  const room = { ...(await api.rooms.list()).data[0], capacity: 20, equipment: ['projector'], available_slots: [3], date_slot_overrides: [{ date: '2026-09-15', slots: [3] }] }
  await api.rooms.update(0, roomPayloadFromForm(roomFormFromEntity(room)))
  const savedRoom = (await api.rooms.list()).data[0]
  record('room_payload_round_trip', { equal: ['capacity', 'equipment', 'available_slots', 'date_slot_overrides'].every(k => JSON.stringify(room[k]) === JSON.stringify(savedRoom[k])) })
  const lesson = { ...(await api.lessons.list()).data[0], required_equipment: ['projector'], required_capacity: 10, required_room_type: 0, allow_room_substitution: false, fixed_room: 0 }
  await api.lessons.update(0, lessonPayloadFromForm(lessonFormFromEntity(lesson)))
  record('constraint_to_solver_slot3', await generate())

  const unavailable = base()
  await reset(unavailable)
  record('no_unavailability_baseline', await generate())
  unavailable.teacher_unavailable = [{ id: 0, teacher: 0, from: '2026-09-15', to: '2026-09-15', time_from: '10:00', time_to: '12:00' }]
  await reset(unavailable)
  const partialUnavailable = await generate()
  record('partial_teacher_unavailability_reaches_slots', partialUnavailable)
  if (partialUnavailable.state !== 'done' || partialUnavailable.events.some(event => event.slot === 2)) {
    throw new Error('Partial teacher unavailability was not applied to the overlapping pair')
  }
  unavailable.teacher_unavailable = [{ id: 0, teacher: 0, from_date: '2026-09-15', to_date: '2026-09-15' }]
  await reset(unavailable)
  record('legacy_unavailability_normalized', { saved: (await api.teacherUnavailable.list()).data[0], auditOk: (await api.data.audit()).data.ok, generation: await generate() })
  unavailable.teacher_unavailable = [{ id: 0, teacher: 0, from: '2026-09-15', to: '2026-09-15' }]
  await reset(unavailable)
  record('canonical_unavailability_blocks', await generate())

  const campus = base()
  campus.teachers[0].allowed_campuses = [0]
  campus.lessons[0].allowed_campuses = [1]
  await reset(campus)
  record('lesson_campus_overridden_by_teacher', await generate())

  const { createPinia, setActivePinia } = await import('../frontend/node_modules/pinia/dist/pinia.mjs')
  const { useScheduleStore } = await import('../frontend/src/stores/schedule.js')
  setActivePinia(createPinia())
  const scheduleStore = useScheduleStore()
  await scheduleStore.fetchSchedule()
  const hadSchedule = Boolean(scheduleStore.scheduleData)
  await api.groups.update(0, { ...(await api.groups.list()).data[0], name: 'CHANGED-AFTER-GENERATION' })
  await scheduleStore.fetchSchedule()
  record('stale_schedule_retained_in_frontend_store', { hadSchedule, stillDisplayed: Boolean(scheduleStore.scheduleData), error: scheduleStore.error })

  const quotas = base()
  Object.assign(quotas.settings, { automatic_period_quotas: true, semester_start_date: '2026-09-01', semester_end_date: '2026-12-19' })
  quotas.lessons[0].total_hours = 20
  quotas.lessons[0].total_slots = 10
  await reset(quotas)
  record('automatic_quota_readout', { readout: (await api.data.semesterReadout()).data })
  record('automatic_quota_api', await generate())
  const cli = spawnSync(solver, ['--generate', '--output', 'cli-automatic-quota'], { cwd: workspace, encoding: 'utf8', timeout: 30000, windowsHide: true })
  record('automatic_quota_cli', { exit: cli.status, stderr: cli.stderr, stdoutTail: cli.stdout?.slice(-700) })

  const parity = base()
  Object.assign(parity.settings, { semester_start_date: '2026-09-07', semester_end_date: '2026-12-19' })
  parity.lessons[0].week_parity = 'odd'
  await reset(parity)
  record('odd_parity_on_second_semester_week', await generate())

  // Invoke the real SFC action with an API error: request() resolves {ok:false}.
  const portal = fs.readFileSync(path.join(root, 'frontend/src/views/TeacherPortalView.vue'), 'utf8')
  const submitFn = portal.slice(portal.indexOf('async function submitNotification()'), portal.indexOf('\n</script>', portal.indexOf('async function submitNotification()')))
  const portalContext = { selectedNotifyTeacher: { value: { id: 0, name: 'Test' } }, notification: { value: { dateFrom: '2026-09-15', dateTo: '2026-09-15' } }, submitting: { value: false }, submitError: { value: '' }, submitSuccess: { value: false }, password: { value: 'fixture' }, notifyTeacherSearch: { value: '' }, api: { teacher: { submitNotification: async () => ({ ok: false, status: 500, data: { message: 'Test failure' } }) } } }
  await vm.runInNewContext(`${submitFn}\nsubmitNotification()`, portalContext)
  record('notification_false_success_on_api_error', { successShown: portalContext.submitSuccess.value, error: portalContext.submitError.value, formCleared: portalContext.notification.value.dateFrom === '' })
} finally {
  child.kill()
  globalThis.fetch = nativeFetch
  fs.writeFileSync(path.join(workspace, 'results.json'), JSON.stringify(results, null, 2))
  fs.writeFileSync(path.join(root, '.tmp/audit-contract-latest.json'), JSON.stringify({ solver, workspace, results }, null, 2))
  console.log(`Audit evidence: ${workspace}`)
}
