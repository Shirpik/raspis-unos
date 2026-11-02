import test from 'node:test'
import assert from 'node:assert/strict'
import { generationDateDefaults, generationScopeFromSchedule, datesInGenerationScope } from '../src/utils/generationDates.js'

const week = ['2026-11-02', '2026-11-03', '2026-11-04', '2026-11-05', '2026-11-06', '2026-11-07']
test('initial generation dates default to the displayed week', () => {
  assert.deepEqual(generationDateDefaults({}, week), { day: week[0], from: week[0], to: week[5] })
})
test('bootstrap and polling preserve an explicitly entered partial week', () => {
  const selection = { day: week[2], from: week[2], to: week[3] }
  assert.deepEqual(generationDateDefaults(selection, [...week]), selection)
  assert.deepEqual(generationDateDefaults(selection, []), { ...selection, day: '' })
})
test('switching display weeks does not replace a custom generation range', () => {
  const selection = { day: week[2], from: week[2], to: week[3] }
  const next = ['2026-11-09', '2026-11-10']
  assert.deepEqual(generationDateDefaults(selection, next), { ...selection, day: next[0] })
})

test('reload restores the generated range and keeps the result grid to those dates', () => {
  const scope = generationScopeFromSchedule({ generation_scope: 'selected_dates', status: 'scoped',
    scope_from: week[2], scope_to: week[3] })
  assert.deepEqual(scope, { from: week[2], to: week[3] })
  assert.deepEqual(datesInGenerationScope(week, scope), week.slice(2, 4))
  const previous = ['2026-10-26', '2026-10-27']
  assert.deepEqual(datesInGenerationScope(previous, scope), previous)
  assert.equal(generationScopeFromSchedule({ groups: [] }), null)
})
test('older scoped snapshots infer their exact range from their own output dates', () => {
  assert.deepEqual(generationScopeFromSchedule({ generation_scope: 'selected_dates', status: 'scoped',
    groups: [{ days: [{ date_iso: week[3] }, { date_iso: week[2] }] }] }), { from: week[2], to: week[3] })
})
