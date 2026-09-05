import test from 'node:test'
import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { buildScheduleExcelWorkbook } from '../src/utils/scheduleTemplateExport.js'
import { teacherBulkPayload, teacherFormFromEntity, teacherPayloadFromForm } from '../src/utils/entityPayloads.js'
import ExcelJS from 'exceljs'
import * as XLSX from 'xlsx'
import { parseVkleyki } from '../src/utils/excelImport.js'

test('workload reimport updates hours without replacing period quotas and room rules', async () => {
  const current = { groups: [{ id: 0, name: 'ТЕСТ-2202', parts: 2 }], teachers: [{ id: 0, name: 'Иванов' }], lessons: [{
    id: 0, name: 'Математика', teacher: 0, group: 0, subgroup: -1, source_index: 'МДК.01', subject_id: 42,
    total_hours: 10, total_slots: 3, generation_active: true, fixed_room: 6, allow_room_substitution: false,
    allowed_campuses: [1], required_room_type: 3,
  }] }
  const makeFile = teacher => {
    const wb = XLSX.utils.book_new()
    XLSX.utils.book_append_sheet(wb, XLSX.utils.aoa_to_sheet([
      ['Индекс', 'Наименование', 'Преподаватель', '', '1 сем'],
      ['МДК.01', 'Математика', teacher, '', 12],
      ['МДК.01', 'ЛПЗ Математика', teacher, '', 4],
    ]), 'ТЕСТ-2202')
    const bytes = XLSX.write(wb, { type: 'buffer', bookType: 'xlsx' })
    return { name: 'fixture.xlsx', arrayBuffer: async () => bytes }
  }
  const result = await parseVkleyki(makeFile('Иванов'), current)
  assert.deepEqual(result.errors, [])
  assert.deepEqual(result.data.lessons[0], { ...current.lessons[0], is_lab: false, is_block: false, is_pp: false,
    week_parity: 'all', required_capacity: 0, required_equipment: [], total_hours: 12, curriculum_active: true })
  assert.equal(result.data.lessons[1].total_slots, 0)
  assert.equal(result.data.lessons[1].generation_active, false)
  assert.equal(result.data.lessons[1].subject_id, 42)
  const transfer = await parseVkleyki(makeFile('Петров'), current)
  assert.ok(transfer.errors.some(error => /изменился преподаватель/.test(error.message)))
})

test('teacher deadlines and bulk date overrides survive frontend payloads', () => {
  const teacher = { name: 'Тимеров', desired_load_rules: [{ group_ids: [42], course_year: 3, deadline: '2026-10-31', minimum_pairs_per_week: 7 }], date_slot_overrides: [{ date: '2026-09-07', slots: [3, 1, 3] }] }
  const payload = teacherPayloadFromForm(teacherFormFromEntity(teacher))
  assert.deepEqual(payload.desired_load_rules, teacher.desired_load_rules)
  assert.deepEqual(teacherBulkPayload(payload, { overrides: true }), { date_slot_overrides: [{ date: '2026-09-07', slots: [1, 3] }] })
  assert.deepEqual(teacherBulkPayload(payload, { days: false }), {})
})

const template = await readFile(new URL('../public/templates/schedule-template.xlsx', import.meta.url))
const book = new ExcelJS.Workbook()
await book.xlsx.load(template)
const sheet = book.worksheets[0]
const name = sheet.getCell(62, 4).text.trim()
const classHour = { id: -1, name: 'Классный час', teacher_name: 'Иванов Иван Иванович', room_name: '57', subgroup: -1, is_class_hour: true }
const makeSchedule = slots => ({ groups: [{ group_index: 0, group_name: name, days: [{ date: '07.09.2026', date_iso: '2026-09-07', weekday: 'ПН', slots }] }] })

test('draft exports safely when the supplied template has no header/footer', async () => {
  const schedule = { ...makeSchedule([]), status: 'draft_semester_risk' }
  const result = await buildScheduleExcelWorkbook(schedule, template)
  assert.ok(result.workbook.worksheets.every(sheet => /ПРОЕКТ/.test(sheet.headerFooter.oddHeader)))
  const roundTrip = new ExcelJS.Workbook()
  await roundTrip.xlsx.load(await result.workbook.xlsx.writeBuffer())
  assert.equal(roundTrip.worksheets.length, 4)
})

test('template writes real zero class hour and exact Monday bells', async () => {
  const result = await buildScheduleExcelWorkbook(makeSchedule([{ slot: 0, lessons: [{ ...classHour, half: 0 }] }]), template)
  assert.equal(result.insertedLessons, 1)
  const output = result.workbook.worksheets[0]
  assert.match(output.getCell(2, 4).text, /Классный час/)
  assert.equal(output.getCell(2, 2).text, '8.15-8.55')
  assert.equal(output.getCell(3, 2).text, '9.15-9.55')
  assert.equal(output.getCell(6, 2).text, '11.35-12.15')
})

test('late class hour leaves first half empty and clears obsolete zero', async () => {
  const result = await buildScheduleExcelWorkbook(makeSchedule([{ slot: 2, lessons: [{ ...classHour, half: 2 }] }]), template)
  const output = result.workbook.worksheets[0]
  assert.equal(result.insertedLessons, 1)
  assert.equal(output.getCell(5, 4).value, null)
  assert.match(output.getCell(6, 4).text, /Классный час/)
  assert.equal(output.getCell(5, 4).isMerged, false)
  assert.equal(output.getCell(2, 4).value, null)
})

test('two Mondays cannot overwrite one another in single-week template', async () => {
  const schedule = makeSchedule([])
  schedule.groups[0].days.push({ date: '14.09.2026', date_iso: '2026-09-14', weekday: 'ПН', slots: [] })
  await assert.rejects(() => buildScheduleExcelWorkbook(schedule, template), /одной недели/)
})
