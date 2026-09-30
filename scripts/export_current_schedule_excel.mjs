import fs from 'node:fs/promises'
import path from 'node:path'
import { buildScheduleExcelWorkbook, scheduleExcelFilename } from '../frontend/src/utils/scheduleTemplateExport.js'

const sourceDir = path.resolve(process.argv[2] || 'output/latest')
const destination = path.resolve(process.argv[3] || 'outputs/excel')
const schedule = JSON.parse(await fs.readFile(path.join(sourceDir, 'schedule_all.json'), 'utf8'))
const audit = JSON.parse(await fs.readFile(path.join(sourceDir, 'strict_audit.json'), 'utf8'))

if (!audit.ok || Number(audit.summary?.hard_errors || 0) > 0) {
  throw new Error(`Расписание не прошло строгую проверку: ${audit.message || 'есть ошибки'}`)
}
if (Number(audit.summary?.unassigned_rooms || 0) > 0) {
  throw new Error(`Не назначены кабинеты: ${audit.summary.unassigned_rooms}`)
}

const template = await fs.readFile(path.resolve('frontend/public/templates/schedule-template.xlsx'))
const result = await buildScheduleExcelWorkbook(schedule, template)
await fs.mkdir(destination, { recursive: true })
const outputPath = path.join(destination, scheduleExcelFilename(schedule))
await result.workbook.xlsx.writeFile(outputPath)

console.log(JSON.stringify({
  file: outputPath,
  dates: result.dates,
  groups: result.groups,
  sheets: result.sheets,
  lessons: result.insertedLessons,
}, null, 2))
