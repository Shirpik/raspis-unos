import test from 'node:test'
import assert from 'node:assert/strict'
import { calendarWeekDates, reconcileSelectedDays } from '../src/utils/scheduleDayActions.js'

test('clear week covers six actual dates even when the generated range contains only two days', () => {
  assert.deepEqual(calendarWeekDates('2026-10-26'), ['2026-10-26', '2026-10-27', '2026-10-28', '2026-10-29', '2026-10-30', '2026-10-31'])
  assert.deepEqual(calendarWeekDates('2026-12-28'), ['2026-12-28', '2026-12-29', '2026-12-30', '2026-12-31', '2027-01-01', '2027-01-02'])
})
test('changing weeks cannot leave invisible days selected for an action', () => {
  assert.deepEqual(reconcileSelectedDays(['2026-10-26', '2026-11-02'], ['2026-11-02', '2026-11-03']), ['2026-11-02'])
  assert.deepEqual(calendarWeekDates(''), [])
})
