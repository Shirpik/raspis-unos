import test from 'node:test'
import assert from 'node:assert/strict'
import * as XLSX from 'xlsx'
import { importScheduleWeekFromExcel } from '../src/utils/scheduleTemplateImport.js'

const data = {
  groups: [{ id: 7, name: 'ИСП-2308' }],
  teachers: [{ id: 3, name: 'Иванова Анна Олеговна' }],
  lessons: [{ id: 10, uid: 'lesson-10', group: 7, subgroup: -1, teacher: 3, name: 'Математика', total_hours: 32 }],
  rooms: [{ id: 5, name: '57', campus: 0, room_type: 0 }],
  settings: { start_date: '2026-09-01' },
  teaching_ledger: [],
}

const workbookFile = subject => {
  const workbook = XLSX.utils.book_new()
  XLSX.utils.book_append_sheet(workbook, XLSX.utils.aoa_to_sheet([
    ['07.09.2026', '', '', 'ИСП-2308'],
    ['ПОНЕДЕЛЬНИК', '9.15-9.55', 1, `${subject}\nИванова 57_Л`],
  ]), '1 курс')
  const bytes = XLSX.write(workbook, { type: 'buffer', bookType: 'xlsx' })
  return { name: 'week.xlsx', arrayBuffer: async () => bytes }
}

const currentManual = {
  groups: [{
    group_index: 7,
    group_name: 'ИСП-2308',
    days: [
      { date: '07.09.2026', date_iso: '2026-09-07', weekday: 'ПН', slots: [{ slot: 1, text: 'Старое', lessons: [{ id: 99 }] }] },
      { date: '14.09.2026', date_iso: '2026-09-14', weekday: 'ПН', slots: [{ slot: 1, text: 'Сохранить', lessons: [{ id: 88 }] }] },
    ],
  }],
}

test('Excel week import replaces only imported dates and preserves other weeks', async () => {
  const result = await importScheduleWeekFromExcel(workbookFile('Математика'), data, currentManual)
  assert.equal(result.ok, true)
  assert.equal(result.imported, 1)
  assert.deepEqual(result.dates, ['2026-09-07'])
  const group = result.manualData.groups[0]
  assert.equal(group.days.find(day => day.date_iso === '2026-09-14').slots[0].lessons[0].id, 88)
  const restored = group.days.find(day => day.date_iso === '2026-09-07').slots[0].lessons[0]
  assert.equal(restored.id, 10)
  assert.equal(restored.room_id, 5)
  assert.equal(restored.room_name, '57')
})

test('Excel week import is atomic when a subject cannot be matched', async () => {
  const result = await importScheduleWeekFromExcel(workbookFile('Неизвестный предмет'), data, currentManual)
  assert.equal(result.ok, false)
  assert.equal(result.manualData, null)
  assert.match(result.errors[0], /не найдено|не найдена|Занятие/u)
  assert.equal(currentManual.groups[0].days[0].slots[0].lessons[0].id, 99)
})
