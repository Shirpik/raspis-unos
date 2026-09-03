import test from 'node:test'
import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'

import {
  collectSlotNumbers,
  scheduleRowHeight,
  slotLessonEntries,
  subgroupOrdinal,
} from '../src/utils/schedulePresentation.js'
import {
  lessonFormFromEntity,
  lessonPayloadFromForm,
  roomFormFromEntity,
  roomPayloadFromForm,
  teacherFormFromEntity,
  teacherPayloadFromForm,
} from '../src/utils/entityPayloads.js'

test('schedule presentation keeps slots 6/7 and all parallel subgroup events', () => {
  const groupIndex = 5
  const days = [3, 4, 5].map((dayOfMonth, index) => {
    const slot = index + 1
    return {
    date: `0${dayOfMonth}.09.2026`,
    slots: [{
      slot,
      text: `Первая ${slot} — 1 п/г | Вторая ${slot} — 2 п/г`,
      lessons: [
        { id: slot * 10, name: `Первая ${slot}`, subgroup: groupIndex * 2 },
        { id: slot * 10 + 1, name: `Вторая ${slot}`, subgroup: groupIndex * 2 + 1 },
      ],
    }],
  }
  })
  days.at(-1).slots.push({ slot: 6, text: '-', lessons: [] }, { slot: 7, text: '-', lessons: [] })
  const group = { group_index: groupIndex, days }

  assert.deepEqual(collectSlotNumbers([group]), [1, 2, 3, 6, 7])
  const events = days.flatMap(day => day.slots.flatMap(slot => slotLessonEntries(slot, groupIndex)))
    .filter(entry => entry.lesson)
  assert.equal(events.length, 6, '3+3 subgroup events must remain six distinct entries')
  assert.deepEqual(events.map(entry => entry.subgroupOrdinal), [1, 2, 1, 2, 1, 2])
  assert.equal(subgroupOrdinal(11, groupIndex), 2)
})

test('teacher edit round-trip preserves every solver availability limit', () => {
  const original = {
    id: 31,
    uid: 'teacher-31',
    name: 'Ланитина Елена Сергеевна',
    default_room: -1,
    campus_priority: [1, 0],
    allowed_campuses: [1],
    max_work_days_per_week: 3,
    max_pairs_per_day: 4,
    work_period: { from: '2026-09-01', to: '2026-12-31' },
    work_days: [{ day: 4, enabled: true, slots: [2, 4], start_slot: 2, end_slot: 4 }],
    date_slot_overrides: [{ date: '2026-09-07', slots: [] }],
    future_server_field: { preserve: true },
  }
  const payload = teacherPayloadFromForm(teacherFormFromEntity(original))

  assert.equal(payload.max_work_days_per_week, 3)
  assert.equal(payload.max_pairs_per_day, 4)
  assert.deepEqual(payload.date_slot_overrides, original.date_slot_overrides)
  assert.deepEqual(payload.work_days, original.work_days)
  assert.deepEqual(payload.allowed_campuses, [1])
  assert.deepEqual(payload.future_server_field, { preserve: true })
})

test('teacher edit preserves a single-campus priority exactly', () => {
  const original = {
    id: 31,
    name: 'Ланитина Елена Сергеевна',
    campus_priority: [1],
    allowed_campuses: [1],
  }
  const payload = teacherPayloadFromForm(teacherFormFromEntity(original))
  assert.deepEqual(payload.campus_priority, [1])
  assert.deepEqual(payload.allowed_campuses, [1])
})

test('legacy parallel text is split and tall Excel rows are not clipped', () => {
  const entries = slotLessonEntries({ text: 'Первая — 1 п/г | Вторая — 2 п/г' }, 0)
  assert.equal(entries.length, 2)
  assert.ok(scheduleRowHeight(['a\nb\n\nc\nd']) > 58)
})

test('room edit round-trip preserves calendar constraints and equipment', () => {
  const original = {
    id: 7,
    uid: 'room-7',
    name: '307',
    room_type: 3,
    equipment: ['ПК', 'проектор'],
    work_period: { from: '2026-09-03', to: '2026-09-05' },
    work_days: [{ day: 4, enabled: true, slots: [2, 4], start_slot: 2, end_slot: 4 }],
    date_slot_overrides: [{ date: '2026-09-04', slots: [3] }],
  }
  const form = roomFormFromEntity(original)
  form.name = '  307А  '
  const payload = roomPayloadFromForm(form)

  assert.equal(payload.name, '307А')
  assert.equal(payload.room_type, 3)
  assert.deepEqual(payload.equipment, ['ПК', 'проектор'])
  assert.deepEqual(payload.work_days, original.work_days)
  assert.deepEqual(payload.date_slot_overrides, original.date_slot_overrides)
  assert.deepEqual(payload.work_period, original.work_period)
})

test('lesson edit round-trip preserves every room requirement', () => {
  const original = {
    id: 42,
    name: 'Информатика',
    group: 5,
    subgroup: 10,
    teacher: 2,
    total_hours: 6,
    total_slots: 3,
    required_room_type: 3,
    required_capacity: 24,
    required_equipment: ['ПК', 'проектор'],
    required_room_purpose: 'computer_lab',
    allowed_campuses: [0],
  }
  const payload = lessonPayloadFromForm(lessonFormFromEntity(original))

  assert.equal(payload.required_room_type, 3)
  assert.equal(payload.required_capacity, 24)
  assert.deepEqual(payload.required_equipment, ['ПК', 'проектор'])
  assert.equal(payload.required_room_purpose, 'computer_lab')
  assert.equal(payload.total_slots, 3)
})

test('lesson plan hours do not overwrite the current generation quota', () => {
  const original = {
    name: 'ЛПЗ', group: 17, subgroup: 34, teacher: 10,
    total_hours: 30, total_slots: 2, is_lab: true,
    consecutive_pairs: 2, avoid_lunch_split: true,
  }
  const form = lessonFormFromEntity(original)
  form.total_hours = 80
  const payload = lessonPayloadFromForm(form)

  assert.equal(payload.total_hours, 80)
  assert.equal(payload.total_slots, 2)
  assert.equal(payload.consecutive_pairs, 2)
  assert.equal(payload.avoid_lunch_split, true)
})

test('validation and solver profiles are wired into the visible application flow', async () => {
  const apiSource = await readFile(new URL('../src/api/index.js', import.meta.url), 'utf8')
  const scheduleSource = await readFile(new URL('../src/views/ScheduleView.vue', import.meta.url), 'utf8')
  const constructorSource = await readFile(new URL('../src/views/ConstructorView.vue', import.meta.url), 'utf8')
  const settingsSource = await readFile(new URL('../src/views/SettingsView.vue', import.meta.url), 'utf8')

  assert.match(apiSource, /\/schedule\/validate/)
  assert.match(apiSource, /\/settings\/solver-profiles/)
  assert.match(scheduleSource, /runValidation/)
  assert.match(constructorSource, /source:\s*'payload'/)
  assert.match(settingsSource, /Все параметры/)
  assert.match(settingsSource, /generationMode/)
})

test('schedule Excel button uses the supplied template export and reports failures', async () => {
  const scheduleSource = await readFile(new URL('../src/views/ScheduleView.vue', import.meta.url), 'utf8')
  const exportSource = await readFile(new URL('../src/utils/scheduleTemplateExport.js', import.meta.url), 'utf8')
  const viteSource = await readFile(new URL('../vite.config.js', import.meta.url), 'utf8')

  assert.match(scheduleSource, /Excel по образцу/)
  assert.match(scheduleSource, /scheduleTemplateExport/)
  assert.match(scheduleSource, /toast\.error/)
  assert.match(exportSource, /templates\/schedule-template\.xlsx/)
  assert.match(exportSource, /insertedLessons !== expected/)
  assert.match(viteSource, /templates\/schedule-template\.xlsx/)
})
