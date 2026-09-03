import * as XLSX from 'xlsx'

const safe = value => value ?? ''
const widths = rows => (rows[0] || []).map((_, column) => ({
  wch: Math.min(42, Math.max(10, ...rows.map(row => String(row[column] ?? '').length + 2)))
}))

function sheetFromRows(rows) {
  const sheet = XLSX.utils.aoa_to_sheet(rows)
  sheet['!cols'] = widths(rows)
  sheet['!freeze'] = { xSplit: 1, ySplit: 1 }
  sheet['!autofilter'] = { ref: sheet['!ref'] }
  return sheet
}

function workloadRows(rows, weeks, nameKey, withSubstitutions = false) {
  const headers = ['Наименование', 'План, ч', 'Поставлено, ч', 'Зачтено, ч']
  if (withSubstitutions) headers.push('Замены +', 'Замены −', 'Корректировка')
  headers.push('Осталось, ч', ...weeks.map(w => `Неделя ${w.index}\n${w.from} — ${w.to}`))
  return [headers, ...rows.map(row => {
    const result = [safe(row[nameKey]), row.planned_hours || 0, row.scheduled_hours || 0,
      row.credited_hours ?? row.scheduled_hours ?? 0]
    if (withSubstitutions) result.push(row.substitution_in_hours || 0,
      row.substitution_out_hours || 0, row.adjustment_hours || 0)
    result.push(row.remaining_hours || 0, ...(row.weekly_hours || []))
    return result
  })]
}

export function exportAccountingWorkbook({ hours, substitutions, occupancy, teacherName, lessonName }) {
  const book = XLSX.utils.book_new()
  XLSX.utils.book_append_sheet(book,
    sheetFromRows(workloadRows(hours.teachers || [], hours.weeks || [], 'teacher_name', true)),
    'Преподаватели')
  XLSX.utils.book_append_sheet(book,
    sheetFromRows(workloadRows(hours.groups || [], hours.weeks || [], 'group_name', false)),
    'Группы')
  const placedRows = [['Преподаватель', 'Дата', 'Неделя', 'Пара', 'Группа', 'Дисциплина', 'Кабинет', 'Фактически ведёт', 'Замена'],
    ...(hours.teachers || []).flatMap(teacher => (teacher.scheduled_occurrences || []).map(row => [
      teacher.teacher_name, row.date, row.week_index, row.slot, row.group_name,
      row.lesson_name, row.room, row.actual_teacher_name,
      row.is_substitution ? 'Да' : ''
    ]))]
  XLSX.utils.book_append_sheet(book, sheetFromRows(placedRows), 'Поставленные даты')
  const replacementRows = [['Дата', 'Пара', 'Дисциплина', 'Отсутствующий', 'Заменяющий', 'Часы', 'Причина', 'Комментарий', 'Статус'],
    ...(substitutions || []).map(row => [row.date, row.slot, lessonName(row.lesson_id),
      teacherName(row.absent_teacher), teacherName(row.substitute_teacher), row.hours,
      row.reason, row.comment, row.status])]
  XLSX.utils.book_append_sheet(book, sheetFromRows(replacementRows), 'Замены')
  const occupancyRows = [['Преподаватель', 'Дата', 'День', 'Пара', 'Группа', 'Дисциплина', 'Кабинет', 'Замена'],
    ...((occupancy?.entries) || []).map(row => [row.teacher_name, row.date, row.weekday,
      row.slot, row.group_name, row.lesson_name, row.room, row.is_substitution ? 'Да' : ''])]
  XLSX.utils.book_append_sheet(book, sheetFromRows(occupancyRows), 'Занятость')
  XLSX.writeFile(book, `Учет_часов_${new Date().toISOString().slice(0, 10)}.xlsx`, { compression: true })
}
