import test from 'node:test'
import assert from 'node:assert/strict'
import { mergeConfirmedSchedule } from '../src/utils/scheduleLedger.js'

test('confirmed lessons replace stale generated cells and show loaded weeks', () => {
  const schedule = { groups: [{ group_index: 1, group_name: 'Группа', days: [{
    date_iso: '2026-09-07', date: '07.09.2026', slots: [{ slot: 2, text: 'Старое', lessons: [{ id: 3, name: 'Старое' }] }],
  }] }] }
  const data = {
    groups: [{ id: 1, name: 'Группа' }],
    lessons: [{ id: 4, uid: 'lesson-4', name: 'Математика', teacher: 8, subgroup: -1 }],
    teachers: [{ id: 8, name: 'Преподаватель' }], rooms: [],
    teaching_ledger: [
      { status: 'confirmed', group_id: 1, lesson_id: 4, date: '2026-09-07', slot: 2, hours: 2 },
      { status: 'confirmed', group_id: 1, lesson_id: 4, date: '2026-09-14', slot: 3, hours: 2 },
    ],
  }
  const merged = mergeConfirmedSchedule(schedule, data)
  assert.deepEqual(merged.groups[0].days.map(day => day.date_iso), ['2026-09-07', '2026-09-14'])
  assert.deepEqual(merged.groups[0].days[0].slots[0].lessons.map(lesson => lesson.id), [4])
  assert.equal(merged.groups[0].days[1].slots[0].lessons[0].confirmed, true)
  assert.equal(merged.groups[0].days[1].slots[0].lessons[0].teacher_name, 'Преподаватель')
  assert.equal(merged.confirmed_count, 2)
  assert.equal(schedule.groups[0].days[0].slots[0].lessons[0].id, 3)
})
