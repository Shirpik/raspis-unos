export function calendarWeekDates(monday) {
  if (!/^\d{4}-\d{2}-\d{2}$/.test(monday || '')) return []
  const first = Date.parse(`${monday}T12:00:00Z`)
  return Array.from({ length: 6 }, (_, offset) => new Date(first + offset * 86400000).toISOString().slice(0, 10))
}

export function reconcileSelectedDays(selected, available) {
  return selected.filter(date => available.includes(date))
}
