import test from 'node:test'
import assert from 'node:assert/strict'
import * as XLSX from 'xlsx'
import { parseCompletedSchedule } from '../src/utils/completedScheduleImport.js'

const data = {
  groups: [{ id: 7, name: 'ИСП-2308' }],
  teachers: [{ id: 3, name: 'Иванова Анна Олеговна' }, { id: 4, name: 'Петров Пётр Петрович' }],
  lessons: [
    { id: 10, group: 7, subgroup: -1, teacher: 3, name: 'Математика', total_hours: 32 },
    { id: 11, group: 7, subgroup: 14, teacher: 3, name: 'ЛПЗ Информатика', is_lab: true, total_hours: 32 },
    { id: 12, group: 7, subgroup: 15, teacher: 4, name: 'ЛПЗ Информатика', is_lab: true, total_hours: 32 },
    { id: 13, group: 7, subgroup: -1, teacher: 3, name: 'Информатика', total_hours: 10 },
    { id: 14, group: 7, subgroup: 14, teacher: 3, name: 'Информатика', total_hours: 24 },
    { id: 15, group: 7, subgroup: 15, teacher: 3, name: 'Информатика', total_hours: 24 },
  ],
  teaching_ledger: [{ id: 0, lesson_id: 10, date: '2026-09-01', slot: 1, hours: 2, status: 'confirmed' }],
}

const workbookFile = () => {
  const workbook = XLSX.utils.book_new()
  XLSX.utils.book_append_sheet(workbook, XLSX.utils.aoa_to_sheet([
    ['07.09.2026', '', '', 'ИСП-2308'],
    ['ПОНЕДЕЛЬНИК', '8.00-8.55', 0, 'Классный час\nИванова 16_К'],
    ['', '9.15-9.55', 1, 'Математика\nИванова 16_К'],
    ['', '10.00-10.40', '', ''],
    ['', '10.50-11.30', 2, 'ЛПЗ Информатика 1 п/г\nИванова 60_К'],
    ['', '11.35-12.15', '', 'ЛПЗ Информатика 2 п/г\nПетров 64_К'],
    ['', '13.10-14.30', 3, 'Информатика\nИванова 60_К'],
    ['', '', '', ''],
    ['08.09.2026', '', '', 'ИСП-2308'],
    ['ВТОРНИК', '8.30-9.10', 1, 'Математика\nИванова 16_К'],
  ]), '1 курс')
  const bytes = XLSX.write(workbook, { type: 'buffer', bookType: 'xlsx' })
  return { name: 'fact.xlsx', arrayBuffer: async () => bytes }
}

test('completed schedule import filters dates, excludes class hours and keeps subgroup facts', async () => {
  const result = await parseCompletedSchedule(workbookFile(), data, { dateFrom: '2026-09-07', dateTo: '2026-09-07' })
  assert.deepEqual(result.errors, [])
  assert.equal(result.imported, 4)
  assert.equal(result.excludedClassHours, 1)
  assert.deepEqual(result.importedDates, ['2026-09-07'])
  assert.deepEqual(result.data.teaching_ledger.map(row => row.lesson_id), [10, 10, 11, 12, 13])
  const subgroupTwo = result.data.teaching_ledger.find(row => row.lesson_id === 12)
  assert.equal(subgroupTwo.actual_teacher, 4)
  assert.equal(subgroupTwo.room, '64_К')
})

test('reimport replaces facts for the selected dates without duplicating old periods', async () => {
  const first = await parseCompletedSchedule(workbookFile(), data, { dateFrom: '2026-09-07', dateTo: '2026-09-07' })
  const second = await parseCompletedSchedule(workbookFile(), first.data, { dateFrom: '2026-09-07', dateTo: '2026-09-07' })
  assert.equal(second.replaced, 4)
  assert.equal(second.data.teaching_ledger.length, 5)
})

test('an unmarked whole-group lesson is not assigned to subgroup one', async () => {
  const result = await parseCompletedSchedule(workbookFile(), data, { dateFrom: '2026-09-07', dateTo: '2026-09-07' })
  const informatics = result.data.teaching_ledger.find(row => row.date === '2026-09-07' && row.slot === 3)
  assert.equal(informatics.lesson_id, 13)
})

test('normalizes the common safety-subject spelling typo from source workbooks', async () => {
  const safetyData = {
    groups: [{ id: 7, name: 'ИСП-2308' }],
    teachers: [{ id: 3, name: 'Иванова Анна Олеговна' }],
    lessons: [{ id: 20, group: 7, subgroup: -1, teacher: 3, name: 'Безопасность жизнедеятельности', total_hours: 16 }],
    teaching_ledger: [],
  }
  const workbook = XLSX.utils.book_new()
  XLSX.utils.book_append_sheet(workbook, XLSX.utils.aoa_to_sheet([
    ['07.09.2026', '', '', 'ИСП-2308'],
    ['ПОНЕДЕЛЬНИК', '8.30-9.10', 1, 'Безопасность жизнидеятельности\nИванова 16_К'],
  ]), '1 курс')
  const bytes = XLSX.write(workbook, { type: 'buffer', bookType: 'xlsx' })
  const result = await parseCompletedSchedule({ name: 'typo.xlsx', arrayBuffer: async () => bytes }, safetyData, {
    dateFrom: '2026-09-07', dateTo: '2026-09-07',
  })
  assert.equal(result.imported, 1)
  assert.deepEqual(result.errors, [])
  assert.equal(result.data.teaching_ledger[0].lesson_id, 20)
})
