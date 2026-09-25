#!/usr/bin/env node
/* Refresh the teaching ledger from the four user-supplied Google exports.
 * The selected dates are confirmed by the dispatcher. One explicitly
 * confirmed fact-only Biology record is maintained below because its source
 * workload row was omitted from the supplied вклейки.
 */
import fs from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { parseCompletedSchedule } from '../frontend/src/utils/completedScheduleImport.js'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const input = new Map()
for (let index = 2; index < process.argv.length; index += 2) input.set(process.argv[index], process.argv[index + 1])
const required = ['--previous', '--current', '--third', '--fourth']
if (required.some(flag => !input.get(flag))) throw new Error('Usage: node scripts/refresh_completed_schedules.mjs --previous <xlsx> --current <xlsx> --third <xlsx> --fourth <xlsx> [--commit]')
const commit = process.argv.includes('--commit')
const dataPath = path.join(root, 'data', 'timetable_data.json')
const fileLike = async filePath => {
  const bytes = await fs.readFile(filePath)
  return { name: path.basename(filePath), arrayBuffer: async () => bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) }
}
const jobs = [
  { flag: '--previous', from: '2026-09-02', to: '2026-09-05', status: 'confirmed', label: 'Расписание занятий 02.09–05.09', url: 'https://docs.google.com/spreadsheets/d/1I_7Pv07cp2A829x_Rw5aJ3rA6SM-3ICaLOLAFTUXxAM/edit?usp=sharing' },
  { flag: '--current', from: '2026-09-07', to: '2026-09-12', status: 'confirmed', label: 'Расписание занятий 07.09–12.09', url: 'https://docs.google.com/spreadsheets/d/1gHRTdbbjte1tqb4AFm91AQiHig3wO1HDoMevYXNvlX8/edit?usp=sharing' },
  { flag: '--third', from: '2026-09-14', to: '2026-09-14', status: 'confirmed', label: 'Расписание занятий 14.09', url: 'https://docs.google.com/spreadsheets/d/1vlRcwNR0gbhoNCmBEfp-5cIv3U9dxRtxXAP1n53v-to/edit?usp=sharing' },
  { flag: '--third', from: '2026-09-15', to: '2026-09-19', status: 'confirmed', label: 'Расписание занятий 15.09–19.09', url: 'https://docs.google.com/spreadsheets/d/1vlRcwNR0gbhoNCmBEfp-5cIv3U9dxRtxXAP1n53v-to/edit?usp=sharing' },
  { flag: '--fourth', from: '2026-09-21', to: '2026-09-24', status: 'confirmed', label: 'Расписание занятий 21.09–24.09', url: 'https://docs.google.com/spreadsheets/d/1MikCATSfxd4IWwvfUOJb9Cp5Gxxu98Vnlt9IXOYb7_o/edit?usp=sharing' },
]
let data = JSON.parse(await fs.readFile(dataPath, 'utf8'))
const ensureConfirmedBiology = value => {
  const group = value.groups.find(item => item.name === 'ТЭиРП-2901')
  const teacher = value.teachers.find(item => item.name.startsWith('Соболева '))
  if (!group || !teacher) throw new Error('Не найдена группа или преподаватель для подтверждённой Биологии ТЭиРП-2901')
  const existing = value.lessons.find(item => item.group === group.id && item.name === 'Биология')
  if (existing) {
    existing.total_hours = 36
    existing.plan_active = true
    existing.curriculum_active = true
    existing.generation_active = false
    existing.source_index = 'Факт: 05.09 и 11.09; пользователь подтвердил продолжающийся предмет'
    return
  }
  const template = value.lessons.find(item => item.group === group.id && item.name === 'Химия')
  if (!template) throw new Error('Не найден шаблон предмета для подтверждённой Биологии ТЭиРП-2901')
  const lesson = structuredClone(template)
  lesson.id = Math.max(...value.lessons.map(item => Number(item.id) || 0)) + 1
  lesson.uid = 'lesson-confirmed-biology-teirp-2901'
  lesson.name = 'Биология'
  lesson.subject_id = 10001
  lesson.teacher = teacher.id
  // The two imported lessons are confirmed, but the subject continues this
  // semester.  Its omitted вклейка row follows the standard 36-hour natural
  // science allocation used by the neighbouring subject in this group.
  lesson.total_hours = 36
  lesson.total_slots = 0
  lesson.generation_active = false
  lesson.plan_active = false
  lesson.curriculum_active = true
  lesson.source_index = 'Факт: 05.09 и 11.09; пользователь подтвердил предмет'
  value.lessons.push(lesson)
}
ensureConfirmedBiology(data)
const reports = []
const errors = []
for (const job of jobs) {
  const result = await parseCompletedSchedule(await fileLike(input.get(job.flag)), data, { dateFrom: job.from, dateTo: job.to, status: job.status })
  errors.push(...result.errors.map(message => `${job.label}: ${message}`))
  data = result.data
  reports.push({ ...job, imported: result.imported, replaced: result.replaced, duplicates: result.duplicates, class_hours_excluded: result.excludedClassHours, warnings: result.warnings })
}
data.settings = {
  ...data.settings,
  accounting_source_links: jobs.map(({ label, from, to, url, status }) => ({ label, from, to, url, status })),
  accounting_import_warnings: errors,
}
const ledger = data.teaching_ledger || []
const summary = {
  generated_at: new Date().toISOString(), commit, jobs: reports, errors,
  ledger: {
    confirmed_pairs: ledger.filter(row => row.status === 'confirmed').length,
    confirmed_hours: ledger.filter(row => row.status === 'confirmed').reduce((total, row) => total + Number(row.hours || 0), 0),
    planned_pairs: ledger.filter(row => row.status === 'planned').length,
    planned_hours: ledger.filter(row => row.status === 'planned').reduce((total, row) => total + Number(row.hours || 0), 0),
  },
}
const outputDir = path.join(root, 'outputs', 'refresh-20260924')
await fs.mkdir(outputDir, { recursive: true })
await fs.writeFile(path.join(outputDir, 'ledger-refresh-report.json'), `${JSON.stringify(summary, null, 2)}\n`, 'utf8')
if (commit) {
  const backup = path.join(root, 'data', 'timetable_data.before-ledger-refresh-20260924.json')
  await fs.copyFile(dataPath, backup)
  const temporary = `${dataPath}.tmp-ledger-refresh`
  await fs.writeFile(temporary, `${JSON.stringify(data, null, 2)}\n`, 'utf8')
  await fs.rename(temporary, dataPath)
}
console.log(JSON.stringify(summary, null, 2))
