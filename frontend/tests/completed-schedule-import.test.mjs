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
  assert.equal(result.importedHours, 8)
  assert.equal(result.excludedClassHours, 1)
  assert.deepEqual(result.importedDates, ['2026-09-07'])
  assert.deepEqual(result.data.teaching_ledger.map(row => row.lesson_id), [10, 10, 11, 12, 13])
  const subgroupTwo = result.data.teaching_ledger.find(row => row.lesson_id === 12)
  assert.equal(subgroupTwo.actual_teacher, 4)
  assert.equal(subgroupTwo.room, '64_К')
})

test('one displayed UP slot is six hours in the teaching ledger', async () => {
  const practiceData = {
    groups: [{ id: 7, name: 'ИСП-2308' }],
    teachers: [{ id: 3, name: 'Иванова Анна Олеговна' }],
    lessons: [{ id: 30, group: 7, subgroup: 14, teacher: 3, name: 'УП.02', is_block: true, total_hours: 36 }],
    teaching_ledger: [],
  }
  const workbook = XLSX.utils.book_new()
  XLSX.utils.book_append_sheet(workbook, XLSX.utils.aoa_to_sheet([
    ['01.10.2026', '', '', 'ИСП-2308'],
    ['ЧЕТВЕРГ', '8.30-12.30', 1, 'УП.02 1 п/г 8:30-12:30\nИванова 411_Л'],
  ]), '2 курс')
  const bytes = XLSX.write(workbook, { type: 'buffer', bookType: 'xlsx' })
  const result = await parseCompletedSchedule({ name: 'up.xlsx', arrayBuffer: async () => bytes }, practiceData)
  assert.deepEqual(result.errors, [])
  assert.equal(result.imported, 1)
  assert.equal(result.importedHours, 6)
  assert.equal(result.data.teaching_ledger[0].hours, 6)
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

test('uses the weekday when a source repeats the previous date and excludes administrative cells', async () => {
  const workbook = XLSX.utils.book_new()
  XLSX.utils.book_append_sheet(workbook, XLSX.utils.aoa_to_sheet([
    ['22.09.2026', '', '', 'ИСП-2308'],
    ['ВТОРНИК', '8.30-9.10', 1, 'ВПР'],
    ['22.09.2026', '', '', 'ИСП-2308'],
    ['СР', '8.30-9.10', 1, 'Кл. час\nИванова 16_К'],
    ['', '10.00-11.30', 2, 'Математика\nИванова 16_К'],
  ]), '2 курс')
  const bytes = XLSX.write(workbook, { type: 'buffer', bookType: 'xlsx' })
  const result = await parseCompletedSchedule({ name: 'weekday.xlsx', arrayBuffer: async () => bytes }, data, {
    dateFrom: '2026-09-22', dateTo: '2026-09-23',
  })
  assert.deepEqual(result.errors, [])
  assert.deepEqual(result.importedDates, ['2026-09-23'])
  assert.equal(result.imported, 1)
  assert.equal(result.excludedClassHours, 2)
})

test('recognizes subgroup labels before a colon', async () => {
  const workbook = XLSX.utils.book_new()
  XLSX.utils.book_append_sheet(workbook, XLSX.utils.aoa_to_sheet([
    ['21.09.2026', '', '', 'ИСП-2308'],
    ['ПОНЕДЕЛЬНИК', '8.30-9.10', 1, '1 п/г: ЛПЗ Информатика\nИванова 60_К'],
    ['', '9.15-9.55', '', '2 п/г: ЛПЗ Информатика\nПетров 64_К'],
  ]), '2 курс')
  const bytes = XLSX.write(workbook, { type: 'buffer', bookType: 'xlsx' })
  const result = await parseCompletedSchedule({ name: 'subgroups.xlsx', arrayBuffer: async () => bytes }, data)
  assert.deepEqual(result.errors, [])
  assert.deepEqual(result.data.teaching_ledger.slice(1).map(row => row.lesson_id), [11, 12])
})

test('uses the dispatcher-confirmed subject mapping for ИСП-3306п', async () => {
  const aliasData = {
    groups: [{ id: 42, name: 'ИСП-3306п' }],
    teachers: [{ id: 41, name: 'Садриева Татьяна Геннадьевна' }],
    lessons: [{ id: 900, group: 42, subgroup: -1, teacher: 41, name: 'Стандартизация, серификация и техническое документоведение', total_hours: 48 }],
    teaching_ledger: [],
  }
  const workbook = XLSX.utils.book_new()
  XLSX.utils.book_append_sheet(workbook, XLSX.utils.aoa_to_sheet([
    ['21.09.2026', '', '', 'ИСП-3306п'],
    ['ПОНЕДЕЛЬНИК', '12.25-13.50', 3, 'Метрология, стандартизация и сертификация\nСадриева 207_Л'],
  ]), '3 курс')
  const bytes = XLSX.write(workbook, { type: 'buffer', bookType: 'xlsx' })
  const result = await parseCompletedSchedule({ name: 'alias.xlsx', arrayBuffer: async () => bytes }, aliasData)
  assert.deepEqual(result.errors, [])
  assert.equal(result.data.teaching_ledger[0].lesson_id, 900)
  assert.equal(result.data.teaching_ledger[0].source_subject, 'Метрология, стандартизация и сертификация')
})
