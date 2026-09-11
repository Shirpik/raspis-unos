#!/usr/bin/env node
import fs from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { parseVkleyki } from '../frontend/src/utils/excelImport.js'
import { parseCompletedSchedule } from '../frontend/src/utils/completedScheduleImport.js'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const args = new Map()
for (let index = 2; index < process.argv.length; index += 2) args.set(process.argv[index], process.argv[index + 1])
const workloadPath = args.get('--workload')
const previousPath = args.get('--previous')
const currentPath = args.get('--current')
const commit = process.argv.includes('--commit')
if (!workloadPath || !previousPath || !currentPath) {
  throw new Error('Usage: node scripts/import_final_sources.mjs --workload <xlsx> --previous <xlsx> --current <xlsx> [--commit yes]')
}

const fileLike = async filePath => {
  const buffer = await fs.readFile(filePath)
  return {
    name: path.basename(filePath),
    arrayBuffer: async () => buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength),
  }
}

const dataPath = path.join(root, 'data', 'timetable_data.json')
let data = JSON.parse(await fs.readFile(dataPath, 'utf8'))
const workload = await parseVkleyki(await fileLike(workloadPath), data, 1)
if (workload.errors.length) throw new Error(`Workload import errors: ${JSON.stringify(workload.errors.slice(0, 20))}`)
data = workload.data

const previousFrom = args.get('--previous-from') || '2026-09-02'
const previousTo = args.get('--previous-to') || '2026-09-05'
const currentFrom = args.get('--current-from') || '2026-09-07'
const currentTo = args.get('--current-to') || '2026-09-11'
const previous = await parseCompletedSchedule(await fileLike(previousPath), data, {
  dateFrom: previousFrom, dateTo: previousTo,
})
if (previous.errors.length) throw new Error(`Previous schedule import errors: ${JSON.stringify(previous.errors.slice(0, 20))}`)
data = previous.data

const current = await parseCompletedSchedule(await fileLike(currentPath), data, {
  dateFrom: currentFrom, dateTo: currentTo,
})
if (current.errors.length) throw new Error(`Current schedule import errors: ${JSON.stringify(current.errors.slice(0, 20))}`)
data = current.data
const confirmedByDate = {}
for (const entry of data.teaching_ledger || []) {
  if (entry.status !== 'confirmed') continue
  const date = String(entry.date || '')
  if (!date) continue
  const currentValue = confirmedByDate[date] || { pairs: 0, hours: 0 }
  currentValue.pairs += 1
  currentValue.hours += Number(entry.hours || 0)
  confirmedByDate[date] = currentValue
}
data.settings = {
  ...(data.settings || {}),
  semester_start_date: '2026-09-02',
  semester_end_date: '2026-12-19',
  semester_weeks: 16,
  enforce_semester_readout: true,
  automatic_period_quotas: true,
  accounting_source_links: [
    {
      label: 'Расписание занятий 02.09-05.09',
      from: previousFrom,
      to: previousTo,
      url: 'https://docs.google.com/spreadsheets/d/1I_7Pv07cp2A829x_Rw5aJ3rA6SM-3ICaLOLAFTUXxAM/edit?usp=sharing',
    },
    {
      label: 'Расписание занятий 07.09-11.09',
      from: currentFrom,
      to: currentTo,
      url: 'https://docs.google.com/spreadsheets/d/1gHRTdbbjte1tqb4AFm91AQiHig3wO1HDoMevYXNvlX8/edit?usp=sharing',
    },
  ],
}

const report = {
  generated_at: new Date().toISOString(),
  commit,
  workload: {
    file: path.resolve(workloadPath), changes: workload.changes,
    warnings: workload.warnings, teachers: data.teachers.length, lessons: data.lessons.length,
  },
  completed_schedule: {
    previous: { file: path.resolve(previousPath), dates: previous.importedDates, pairs: previous.imported, duplicates: previous.duplicates, class_hours_excluded: previous.excludedClassHours, warnings: previous.warnings },
    current: { file: path.resolve(currentPath), dates: current.importedDates, pairs: current.imported, duplicates: current.duplicates, class_hours_excluded: current.excludedClassHours, warnings: current.warnings },
    confirmed_pairs: data.teaching_ledger.length,
    confirmed_hours: data.teaching_ledger.filter(row => row.status === 'confirmed').reduce((sum, row) => sum + Number(row.hours || 0), 0),
    class_hours_excluded: previous.excludedClassHours + current.excludedClassHours,
    by_date: confirmedByDate,
  },
  deadline: data.settings.semester_end_date,
}

const outputDir = path.join(root, 'outputs', 'finalization-2026-09-08')
await fs.mkdir(outputDir, { recursive: true })
await fs.writeFile(path.join(outputDir, 'import-report.json'), `${JSON.stringify(report, null, 2)}\n`, 'utf8')
if (commit) {
  const stamp = new Date().toISOString().replaceAll(':', '-').replaceAll('.', '-')
  await fs.copyFile(dataPath, path.join(root, 'data', `timetable_data.before-final-import-${stamp}.json`))
  const temporary = `${dataPath}.tmp-final-import`
  await fs.writeFile(temporary, `${JSON.stringify(data, null, 2)}\n`, 'utf8')
  await fs.rename(temporary, dataPath)
}
console.log(JSON.stringify(report, null, 2))
