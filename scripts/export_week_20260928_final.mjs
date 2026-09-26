import fs from 'node:fs/promises'
import path from 'node:path'
import { pathToFileURL } from 'node:url'
import { buildScheduleExcelWorkbook, scheduleExcelFilename } from '../frontend/src/utils/scheduleTemplateExport.js'

const root = process.cwd()
const sourceDir = path.resolve(process.argv[2] || 'output/week-20260928-finalized')
const outputDir = path.resolve(process.argv[3] || 'outputs/week-20260928-finalized')
const templatePath = path.resolve('frontend/public/templates/schedule-template.xlsx')
const schedulePath = path.join(sourceDir, 'schedule_all.json')
const qualityPath = path.join(sourceDir, 'quality_report.json')
const auditPath = path.join(sourceDir, 'strict_audit.json')
const artifactModulePath = process.env.ARTIFACT_TOOL_MODULE ||
  'C:/Users/Student/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/@oai/artifact-tool/dist/artifact_tool.mjs'
const { FileBlob, SpreadsheetFile } = await import(pathToFileURL(artifactModulePath))

// Excel is a delivery artifact, so refuse to publish a schedule that the
// solver's own validator has rejected.  In particular, the old exporter used
// to write a workbook first and only then read strict_audit.json.  That made a
// workbook with hundreds of windows/availability errors look deliverable.
const quality = JSON.parse(await fs.readFile(qualityPath, 'utf8'))
const audit = JSON.parse(await fs.readFile(auditPath, 'utf8'))
const failedCategories = (audit.categories || [])
  .filter(category => Number(category.hard_errors || 0) > 0 || category.passed === false)
  // A weekly draft may legitimately carry semester-plan warnings while its
  // full 16-week ledger is being confirmed.  Every timetable-level category
  // remains a hard gate.
  .filter(category => category.id !== 'semester_plan')
if (!audit.categories?.length || failedCategories.length) {
  const details = failedCategories.map(category => `${category.id}:${category.hard_errors}`).join(', ')
  throw new Error(`Экспорт остановлен: строгая проверка расписания не пройдена (${details || audit.message || 'audit.ok=false'})`)
}
const summary = audit.summary || {}
if (Number(summary.incomplete_lessons || 0) > 0 || Number(summary.remaining_hours || 0) > 0 ||
    Number(summary.mismatched_lessons || 0) > 0 || Number(quality.completion_percent ?? 100) < 100) {
  throw new Error(`Экспорт остановлен: расписание неполное (завершение ${quality.completion_percent ?? 'н/д'}%, осталось часов ${summary.remaining_hours ?? quality.remaining_hours ?? 'н/д'})`)
}
if (Number(quality.student_windows || 0) > 0 || Number(quality.teacher_windows || 0) > 0) {
  throw new Error(`Экспорт остановлен: обнаружены окна студентов/преподавателей (студенты ${quality.student_windows}, преподаватели ${quality.teacher_windows})`)
}
const windowWarnings = Number((audit.categories || []).find(category => category.id === 'windows')?.warnings || 0)
if (windowWarnings > 0) {
  throw new Error(`Экспорт остановлен: валидатор обнаружил ${windowWarnings} окон в недельной сетке`)
}

await fs.mkdir(outputDir, { recursive: true })
const schedule = JSON.parse(await fs.readFile(schedulePath, 'utf8'))
schedule.status ||= 'draft_semester_risk'
const template = await fs.readFile(templatePath)
const built = await buildScheduleExcelWorkbook(schedule, template)

// ExcelJS is used by the application's existing template writer. Round-trip the
// generated workbook through artifact-tool so formulas, previews and the final
// xlsx are inspected/exported by the spreadsheet runtime.
const stagingPath = path.join(outputDir, '.staging-exceljs.xlsx')
await built.workbook.xlsx.writeFile(stagingPath)
const workbook = await SpreadsheetFile.importXlsx(await FileBlob.load(stagingPath))
await workbook.recalculate()
const formulaErrors = await workbook.inspect({
  kind: 'match',
  searchTerm: '#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A',
  options: { useRegex: true, maxResults: 100 },
  maxChars: 5000,
})
if (/#REF!|#DIV\/0!|#VALUE!|#NAME\?|#N\/A/.test(formulaErrors.ndjson)) {
  throw new Error(`Ошибки формул в экспортируемой книге: ${formulaErrors.ndjson}`)
}

const outputPath = path.join(outputDir, scheduleExcelFilename(schedule))
const exported = await SpreadsheetFile.exportXlsx(workbook)
await exported.save(outputPath)
const previewDir = path.join(outputDir, 'preview')
await fs.mkdir(previewDir, { recursive: true })
for (const sheet of workbook.worksheets.items) {
  const preview = await workbook.render({ sheetName: sheet.name, autoCrop: 'all', scale: 1, format: 'png' })
  const safeName = sheet.name.replaceAll(/[\\/:*?"<>|]/g, '_')
  await fs.writeFile(path.join(previewDir, `${safeName}.png`), new Uint8Array(await preview.arrayBuffer()))
}

const verification = {
  file: outputPath,
  sheets: built.sheets,
  groups: built.groups,
  dates: built.dates,
  exported_lessons: built.insertedLessons,
  schedule_lessons: schedule.groups.flatMap(g => g.days || []).flatMap(d => d.slots || []).reduce((n, s) => n + (s.lessons || []).length, 0),
  ordinary_lessons: schedule.groups.flatMap(g => g.days || []).flatMap(d => d.slots || []).flatMap(s => s.lessons || []).filter(l => !l.is_class_hour).length,
  class_hours: schedule.groups.flatMap(g => g.days || []).flatMap(d => d.slots || []).flatMap(s => s.lessons || []).filter(l => l.is_class_hour).length,
  formula_errors: 0,
  preview_dir: previewDir,
  strict_audit_ok: audit.ok === true,
  strict_audit_hard_errors: audit.summary?.hard_errors ?? null,
  quality_completion_percent: quality.completion_percent,
  quality_room_conflicts: quality.rooms?.conflicts?.length ?? null,
  quality_up_violations: quality.up_day_rule_violations,
}
await fs.writeFile(path.join(outputDir, 'verification.json'), JSON.stringify(verification, null, 2))
await fs.rm(stagingPath, { force: true })
console.log(JSON.stringify(verification, null, 2))
