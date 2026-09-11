import test from 'node:test'
import assert from 'node:assert/strict'
import { buildReferenceAccountingWorkbook } from '../src/utils/referenceAccountingExport.js'

const hours = {
  semester_start: '2026-09-02',
  weeks: [{ index: 1, from: '2026-09-02', to: '2026-09-08' }, { index: 2, from: '2026-09-09', to: '2026-09-15' }],
  teachers: [{ teacher_name: 'Круглова Юлия Петровна', planned_hours: 58, scheduled_hours: 10, credited_hours: 10, remaining_hours: 48, adjustment_hours: 0, weekly_hours: [10, 0] }],
  groups: [{ group_id: 4, group_name: 'СП-Пф-1605', accounting_title: '15.02.19 Сварочное производство - СП-Пф-1605' }],
  lessons: [
    { lesson_id: 84, group_id: 4, group_name: 'СП-Пф-1605', teacher_name: 'Круглова Юлия Петровна', source_index: 'ОУП.08', subject_id: 71, subgroup: -1, name: 'информатика', planned_hours: 10, scheduled_hours: 10, scheduled_occurrences: [{ week_index: 1, hours: 2 }, { week_index: 1, hours: 2 }, { week_index: 1, hours: 2 }, { week_index: 1, hours: 2 }, { week_index: 1, hours: 2 }] },
    { lesson_id: 85, group_id: 4, group_name: 'СП-Пф-1605', teacher_name: 'Круглова Юлия Петровна', source_index: 'ОУП.08', subject_id: 71, subgroup: 8, name: 'информатика', planned_hours: 24, scheduled_hours: 0, scheduled_occurrences: [] },
    { lesson_id: 86, group_id: 4, group_name: 'СП-Пф-1605', teacher_name: 'Круглова Юлия Петровна', source_index: 'ОУП.08', subject_id: 71, subgroup: 9, name: 'информатика', planned_hours: 24, scheduled_hours: 0, scheduled_occurrences: [] },
  ],
}

test('reference accounting export follows the supplied sheet order and uses confirmed hours', () => {
  const { workbook, remainingWeeks } = buildReferenceAccountingWorkbook(hours)
  assert.equal(remainingWeeks, 1)
  assert.deepEqual(workbook.worksheets.map(sheet => sheet.name), ['пр', 'расписание', 'СП-Пф-1605'])
  const teachers = workbook.getWorksheet('пр')
  assert.equal(teachers.getCell('B3').value.formula, 'C3/1')
  assert.equal(teachers.getCell('B3').value.result, 58)
  assert.equal(teachers.getCell('C3').value, 58)
  assert.equal(teachers.getCell('D3').value, 10)
  assert.equal(teachers.getCell('E3').value.result, 48)
  assert.equal(teachers.getCell('I3').value, 10)
  const group = workbook.getWorksheet('СП-Пф-1605')
  assert.equal(group.getCell('B2').value, '15.02.19 Сварочное производство - СП-Пф-1605')
  assert.equal(group.getCell('B9').value, 'ОУП.08')
  assert.equal(group.getCell('C9').value, 'информатика(т)')
  assert.equal(group.getCell('F9').value, 10)
  assert.equal(group.getCell('G9').value.formula, 'D9-F9')
  assert.equal(group.getCell('D9').value - group.getCell('F9').value, 0)
  assert.equal(group.getCell('J9').value, 10)
})
