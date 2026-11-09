export function replacementPeriodsError(periods, originalTeacher, teachers) {
  const ids = new Set(teachers.map(t => t.id))
  const ordered = [...periods].sort((a, b) => String(a.from).localeCompare(String(b.from)))
  for (let i = 0; i < ordered.length; i++) {
    const p = ordered[i]
    if (!p.from || !p.to || p.to < p.from) return 'Укажите корректные даты начала и окончания замены'
    if (!ids.has(p.teacher) || p.teacher === originalTeacher) return 'Выберите другого преподавателя для замены'
    if (i && p.from <= ordered[i-1].to) return 'Периоды замен одного предмета не должны пересекаться'
  }
  return ''
}
