// Defaults may arrive after the user has already entered a range (bootstrap,
// polling, or a week change). Never replace an explicit selection with a week.
export function generationDateDefaults(current, dates) {
  return {
    day: dates.includes(current.day) ? current.day : (dates[0] || ''),
    from: current.from || dates[0] || '',
    to: current.to || dates.at(-1) || '',
  }
}

export function generationScopeFromSchedule(schedule) {
  if (schedule?.generation_scope !== 'selected_dates' || schedule?.status !== 'scoped') return null
  const dates = (schedule.groups || []).flatMap(group => (group.days || []).map(day => day.date_iso))
    .filter(date => /^\d{4}-\d{2}-\d{2}$/.test(date || '')).sort()
  const from = schedule.scope_from || dates[0]
  const to = schedule.scope_to || dates.at(-1)
  return from && to && from <= to ? { from, to } : null
}

// Keep historical weeks available; only the week containing this result is
// narrowed to the requested dates instead of adding six empty day columns.
export function datesInGenerationScope(dates, scope) {
  if (!scope || !dates.some(date => scope.from <= date && date <= scope.to)) return dates
  return dates.filter(date => scope.from <= date && date <= scope.to)
}
