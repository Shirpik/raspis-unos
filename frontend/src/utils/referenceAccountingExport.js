import ExcelJS from 'exceljs'
import { subgroupOrdinal } from './schedulePresentation.js'

const COLORS = {
  border: 'FF000000', yellow: 'FFFFFF00', paleYellow: 'FFFFFFCC', green: 'FFC6F7C6',
  peach: 'FFFCE4D6', blue: 'FFB4C6E7', violet: 'FFE4DFEC', red: 'FFFF0000', white: 'FFFFFFFF',
}
const thinBorder = {
  top: { style: 'thin', color: { argb: COLORS.border } }, left: { style: 'thin', color: { argb: COLORS.border } },
  bottom: { style: 'thin', color: { argb: COLORS.border } }, right: { style: 'thin', color: { argb: COLORS.border } },
}
const fill = argb => ({ type: 'pattern', pattern: 'solid', fgColor: { argb } })
const shortDate = value => {
  if (!value) return ''
  const [, month, day] = String(value).split('-')
  return `${day}.${month}`
}
const semesterNumber = start => Number(String(start || '').slice(5, 7)) >= 7 ? 1 : 2
const safeSheetName = (name, used) => {
  const base = String(name || 'Группа').replace(/[\\/?*:[\]]/g, ' ').trim().slice(0, 31) || 'Группа'
  let candidate = base, suffix = 2
  while (used.has(candidate.toLocaleLowerCase('ru'))) {
    const tail = ` (${suffix++})`
    candidate = `${base.slice(0, 31 - tail.length)}${tail}`
  }
  used.add(candidate.toLocaleLowerCase('ru'))
  return candidate
}
const downloadBuffer = (buffer, filename) => {
  const blob = new Blob([buffer], { type: 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet' })
  const url = URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.href = url
  link.download = filename
  document.body.appendChild(link)
  link.click()
  link.remove()
  setTimeout(() => URL.revokeObjectURL(url), 1000)
}
const styleCell = (cell, { background, bold = false, vertical = false, align = 'center', color, size = 11 } = {}) => {
  cell.border = thinBorder
  cell.alignment = { horizontal: align, vertical: 'middle', wrapText: true, textRotation: vertical ? 90 : 0 }
  cell.font = { name: 'Times New Roman', size, bold, color: color ? { argb: color } : undefined }
  if (background) cell.fill = fill(background)
}
const lessonWeekHours = (lesson, weeks) => {
  const values = Array(weeks.length).fill(0)
  for (const occurrence of lesson.scheduled_occurrences || []) {
    const index = Number(occurrence.week_index || 0) - 1
    if (index >= 0 && index < values.length) values[index] += Number(occurrence.hours || 2)
  }
  return values
}
const remainingTeachingWeeks = hours => {
  const weekCount = Math.max(1, (hours.weeks || []).length)
  const latestAccountedWeek = (hours.lessons || []).reduce((latest, lesson) =>
    (lesson.scheduled_occurrences || []).reduce((value, occurrence) =>
      Math.max(value, Number(occurrence.week_index || 0)), latest), 0)
  return Math.max(1, weekCount - latestAccountedWeek)
}
const displayLessonName = (lesson, groupLessons) => {
  const related = groupLessons.filter(item => Number(item.subject_id) === Number(lesson.subject_id))
  const hasSubgroups = related.some(item => Number(item.subgroup ?? -1) >= 0)
  const ordinal = subgroupOrdinal(lesson.subgroup, lesson.group_id)
  if (Number(lesson.subgroup ?? -1) < 0 && hasSubgroups) return `${lesson.name}(т)`
  if (ordinal) return `${lesson.name} ${ordinal} п/г`
  return lesson.name
}

function addTeacherSummary(workbook, hours, semester) {
  const weeks = hours.weeks || []
  const firstWeekCol = 9
  const sheet = workbook.addWorksheet('пр', { views: [{ state: 'frozen', xSplit: 8, ySplit: 2, showGridLines: false }] })
  sheet.properties.defaultRowHeight = 22
  ;[48, 7, 5, 6, 6, 10, 6, 6].forEach((width, index) => { sheet.getColumn(index + 1).width = width })
  weeks.forEach((_, index) => { sheet.getColumn(firstWeekCol + index).width = 6 })

  const headers = ['преподаватель', 'Нагрузка', `${semester} семестр`, `выдано во ${semester} семестре`, `остаток во ${semester} семестре`, 'ср. недельная нагрузка исходный по группам', 'кор. Ср. счет по группам', 'корр. Ср. на недели']
  headers.forEach((header, index) => {
    const col = index + 1
    sheet.mergeCells(1, col, 2, col)
    const cell = sheet.getCell(1, col)
    cell.value = header
    styleCell(cell, {
      background: col === 1 ? COLORS.paleYellow : col === 3 ? COLORS.blue : col === 5 ? COLORS.peach : col >= 4 ? COLORS.green : undefined,
      bold: true, vertical: col > 1,
    })
  })
  weeks.forEach((week, index) => {
    const col = firstWeekCol + index
    sheet.getCell(1, col).value = week.index
    sheet.getCell(2, col).value = shortDate(week.from)
    styleCell(sheet.getCell(1, col), { background: COLORS.paleYellow, bold: true })
    styleCell(sheet.getCell(2, col), { background: COLORS.paleYellow, bold: true, vertical: true })
  })
  sheet.getRow(1).height = 28
  sheet.getRow(2).height = 92

  const weekCount = Math.max(1, weeks.length)
  const remainingWeeks = remainingTeachingWeeks(hours)
  const teachers = [...(hours.teachers || [])]
    .filter(teacher => Number(teacher.planned_hours || 0) !== 0 || Number(teacher.credited_hours || 0) !== 0 || Number(teacher.adjustment_hours || 0) !== 0)
    .sort((a, b) => a.teacher_name.localeCompare(b.teacher_name, 'ru'))
  teachers.forEach((teacher, index) => {
    const row = index + 3
    const planned = Number(teacher.planned_hours || 0)
    const credited = Number(teacher.credited_hours ?? teacher.scheduled_hours ?? 0)
    const adjustment = Number(teacher.adjustment_hours || 0)
    sheet.getCell(row, 1).value = teacher.teacher_name
    sheet.getCell(row, 2).value = { formula: `C${row}/${remainingWeeks}`, result: planned / remainingWeeks }
    sheet.getCell(row, 3).value = planned
    sheet.getCell(row, 4).value = credited
    sheet.getCell(row, 5).value = { formula: `C${row}-D${row}`, result: planned - credited }
    sheet.getCell(row, 6).value = { formula: `C${row}/${weekCount}`, result: planned / weekCount }
    sheet.getCell(row, 7).value = adjustment
    sheet.getCell(row, 8).value = { formula: `G${row}/4.5`, result: adjustment / 4.5 }
    weeks.forEach((_, weekIndex) => { sheet.getCell(row, firstWeekCol + weekIndex).value = teacher.weekly_hours?.[weekIndex] || 0 })
    for (let col = 1; col < firstWeekCol + weeks.length; col++) {
      const background = col === 3 ? COLORS.blue : col === 5 ? COLORS.peach : col >= 4 && col <= 7 ? COLORS.green : col >= firstWeekCol ? COLORS.paleYellow : undefined
      styleCell(sheet.getCell(row, col), { background, align: col === 1 ? 'left' : 'center' })
    }
    sheet.getCell(row, 2).numFmt = '0.0'
    sheet.getCell(row, 6).numFmt = '0.00'
    sheet.getCell(row, 8).numFmt = '0'
    if (planned - credited > 0) sheet.getCell(row, 5).font = { name: 'Times New Roman', size: 11, color: { argb: COLORS.red }, bold: true }
  })
  sheet.pageSetup = { orientation: 'landscape', fitToPage: true, fitToWidth: 1, fitToHeight: 0, paperSize: 9 }
}

function addScheduleSheet(workbook, hours) {
  const sheet = workbook.addWorksheet('расписание', { views: [{ showGridLines: true }] })
  const weeks = hours.weeks || []
  sheet.getColumn(1).width = 18
  weeks.forEach((_, index) => { sheet.getColumn(index + 2).width = 24 })
  sheet.getCell('A2').value = 'Ссылка на файл'
  sheet.getCell('A3').value = 'дата начала'
  sheet.getCell('A4').value = 'дата конца'
  for (let row = 1; row <= 4; row++) styleCell(sheet.getCell(row, 1), { background: COLORS.green, bold: row === 2, align: 'center' })
  weeks.forEach((week, index) => {
    const col = index + 2
    sheet.getCell(1, col).value = week.index
    sheet.getCell(3, col).value = shortDate(week.from)
    sheet.getCell(4, col).value = shortDate(week.to)
    styleCell(sheet.getCell(1, col), { background: COLORS.green })
    styleCell(sheet.getCell(2, col))
    styleCell(sheet.getCell(3, col))
    styleCell(sheet.getCell(4, col))
  })
  const sources = hours.accounting_source_links || []
  weeks.forEach((week, index) => {
    const relevant = sources.filter(source => String(source.from || '') <= String(week.to || '') && String(source.to || '') >= String(week.from || ''))
    if (!relevant.length) return
    const cell = sheet.getCell(2, index + 2)
    const text = relevant.map(source => source.label || source.url).join('; ')
    cell.value = relevant.length === 1 && relevant[0].url ? { text, hyperlink: relevant[0].url } : text
    cell.note = relevant.map(source => `${source.label || 'Источник'}: ${source.url || ''}`).join('\n')
    styleCell(cell, { align: 'left' })
  })
  sheet.getRow(2).height = 36
}

function addGroupSheet(workbook, hours, group, semester, usedNames) {
  const weeks = hours.weeks || []
  const firstWeekCol = 10
  const groupLessons = (hours.lessons || []).filter(lesson => Number(lesson.group_id) === Number(group.group_id))
  const lessons = [...groupLessons].sort((a, b) =>
    String(a.source_index || '').localeCompare(String(b.source_index || ''), 'ru', { numeric: true }) ||
    Number(a.subject_id ?? 0) - Number(b.subject_id ?? 0) ||
    Number(a.subgroup ?? -1) - Number(b.subgroup ?? -1) || a.name.localeCompare(b.name, 'ru'))
  const sheet = workbook.addWorksheet(safeSheetName(group.group_name, usedNames), { views: [{ state: 'frozen', xSplit: 9, ySplit: 8, showGridLines: false }] })
  sheet.properties.defaultRowHeight = 20
  sheet.getColumn(1).width = 13
  ;[13, 42, 10, 33, 7, 7, 7, 7].forEach((width, index) => { sheet.getColumn(index + 2).width = width })
  weeks.forEach((_, index) => { sheet.getColumn(firstWeekCol + index).width = 6 })
  const spacerStart = firstWeekCol + weeks.length
  for (let col = spacerStart; col < spacerStart + 3; col++) sheet.getColumn(col).width = 3
  const subgroup1Col = spacerStart + 3
  const subgroup2Col = subgroup1Col + 1
  sheet.getColumn(subgroup1Col).width = 11
  sheet.getColumn(subgroup2Col).width = 11

  sheet.mergeCells(2, 2, 3, 5)
  sheet.getCell(2, 2).value = group.accounting_title || group.group_name
  sheet.getCell(2, 2).font = { name: 'Times New Roman', size: 12, bold: true }
  sheet.getCell(2, 2).alignment = { horizontal: 'center', vertical: 'middle', wrapText: true }
  sheet.getRow(2).height = 21
  sheet.getRow(3).height = 21

  weeks.forEach((week, index) => {
    const col = firstWeekCol + index
    sheet.getCell(5, col).value = week.index
    sheet.getCell(6, col).value = shortDate(week.from)
    sheet.getCell(7, col).value = shortDate(week.to)
    sheet.getCell(8, col).value = 1
    for (let row = 5; row <= 8; row++) styleCell(sheet.getCell(row, col), { background: COLORS.peach, bold: true })
  })

  const mergedHeaders = [
    [2, 'Индекс', undefined, false],
    [3, 'Наименование циклов, разделов, дисциплин, профессиональных модулей, междисциплинарных курсов', undefined, false],
    [5, 'преподаватель', COLORS.paleYellow, false],
    [6, `выдано во ${semester} семестре`, COLORS.green, true],
    [7, `остаток во ${semester} семестре`, COLORS.violet, true],
    [8, 'ср. недельная нагрузка', COLORS.green, true],
    [9, 'кор. Ср. нагрузки', COLORS.green, true],
  ]
  mergedHeaders.forEach(([col, label, background, vertical]) => {
    sheet.mergeCells(6, col, 8, col)
    sheet.getCell(6, col).value = label
    styleCell(sheet.getCell(6, col), { background, bold: true, vertical })
  })
  sheet.mergeCells(6, 4, 7, 4)
  sheet.getCell(6, 4).value = 'Семестр'
  styleCell(sheet.getCell(6, 4), { bold: true })
  sheet.getCell(8, 4).value = `${semester} сем.`
  styleCell(sheet.getCell(8, 4), { bold: false })
  ;[[subgroup1Col, '1 подгр.'], [subgroup2Col, '2 подгр.']].forEach(([col, label]) => {
    sheet.mergeCells(6, col, 8, col)
    sheet.getCell(6, col).value = label
    styleCell(sheet.getCell(6, col), { background: COLORS.green, bold: true, vertical: true })
  })
  sheet.getRow(5).height = 20
  sheet.getRow(6).height = 42
  sheet.getRow(7).height = 28
  sheet.getRow(8).height = 28

  const weekCount = Math.max(1, weeks.length)
  lessons.forEach((lesson, index) => {
    const row = index + 9
    const planned = Number(lesson.planned_hours || 0)
    const issued = Number(lesson.scheduled_hours || 0)
    const sourceIndex = String(lesson.source_index || '').trim()
    sheet.getCell(row, 2).value = sourceIndex || `Д-${String(Number(lesson.subject_id ?? index) + 1).padStart(3, '0')}`
    sheet.getCell(row, 3).value = displayLessonName(lesson, groupLessons)
    sheet.getCell(row, 4).value = planned
    sheet.getCell(row, 5).value = lesson.teacher_name || 'вакансия'
    sheet.getCell(row, 6).value = issued
    sheet.getCell(row, 7).value = { formula: `D${row}-F${row}`, result: planned - issued }
    sheet.getCell(row, 8).value = { formula: `D${row}/${weekCount}`, result: planned / weekCount }
    sheet.getCell(row, 9).value = 0
    lessonWeekHours(lesson, weeks).forEach((value, weekIndex) => { sheet.getCell(row, firstWeekCol + weekIndex).value = value })
    const subgroup = subgroupOrdinal(lesson.subgroup, lesson.group_id)
    sheet.getCell(row, subgroup1Col).value = subgroup === 2 ? 0 : 1
    sheet.getCell(row, subgroup2Col).value = subgroup === 1 ? 0 : 1
    for (let col = 2; col <= subgroup2Col; col++) {
      if (col >= spacerStart && col < subgroup1Col) continue
      const background = col === 5 ? COLORS.paleYellow : col === 6 || col >= 8 && col <= 9 ? COLORS.green : col === 7 ? COLORS.red : col >= firstWeekCol && col < spacerStart ? COLORS.peach : undefined
      styleCell(sheet.getCell(row, col), { background, align: col === 3 || col === 5 ? 'left' : 'center' })
    }
    sheet.getCell(row, 8).numFmt = '0.0'
    sheet.getCell(row, 9).numFmt = '0.0'
  })

  for (let start = 0; start < lessons.length;) {
    const key = String(lessons[start].source_index || '').trim()
    let end = start
    while (key && end + 1 < lessons.length && String(lessons[end + 1].source_index || '').trim() === key) end++
    if (key && end > start) {
      sheet.mergeCells(start + 9, 2, end + 9, 2)
      styleCell(sheet.getCell(start + 9, 2), { bold: false })
    }
    start = end + 1
  }

  const firstDataRow = 9
  const lastDataRow = lessons.length + 8
  const totalRow = lastDataRow + 1
  for (let col = 6; col < spacerStart; col++) {
    const letter = sheet.getColumn(col).letter
    sheet.getCell(totalRow, col).value = { formula: `SUM(${letter}${firstDataRow}:${letter}${lastDataRow})` }
    styleCell(sheet.getCell(totalRow, col), { bold: false })
  }
  const physicalWeekHours = (ordinal, weekIndex) => groupLessons.reduce((sum, lesson) => {
    const subgroup = subgroupOrdinal(lesson.subgroup, lesson.group_id)
    if (subgroup && subgroup !== ordinal) return sum
    return sum + lessonWeekHours(lesson, weeks)[weekIndex]
  }, 0)
  for (const ordinal of [1, 2]) {
    const row = totalRow + ordinal
    const relevant = groupLessons.filter(lesson => {
      const subgroup = subgroupOrdinal(lesson.subgroup, lesson.group_id)
      return !subgroup || subgroup === ordinal
    })
    const planned = relevant.reduce((sum, lesson) => sum + Number(lesson.planned_hours || 0), 0)
    const issued = relevant.reduce((sum, lesson) => sum + Number(lesson.scheduled_hours || 0), 0)
    sheet.getCell(row, 7).value = planned - issued
    sheet.getCell(row, 8).value = planned / weekCount
    sheet.getCell(row, 9).value = 0
    weeks.forEach((_, weekIndex) => { sheet.getCell(row, firstWeekCol + weekIndex).value = physicalWeekHours(ordinal, weekIndex) })
    for (let col = 7; col < spacerStart; col++) styleCell(sheet.getCell(row, col))
    sheet.getCell(row, 7).numFmt = '0.0'
    sheet.getCell(row, 8).numFmt = '0.0'
    sheet.getCell(row, 9).numFmt = '0.0'
  }
  sheet.pageSetup = { orientation: 'landscape', fitToPage: true, fitToWidth: 1, fitToHeight: 0, paperSize: 9, repeatRows: '1:8' }
}

export function buildReferenceAccountingWorkbook(hours) {
  const workbook = new ExcelJS.Workbook()
  workbook.creator = 'Генератор расписания техникума'
  workbook.created = new Date()
  workbook.calcProperties.fullCalcOnLoad = true
  const semester = semesterNumber(hours.semester_start || hours.weeks?.[0]?.from)
  addTeacherSummary(workbook, hours, semester)
  addScheduleSheet(workbook, hours)
  const usedNames = new Set(['пр', 'расписание'])
  const groups = [...(hours.groups || [])].sort((a, b) => Number(a.group_id) - Number(b.group_id))
  groups.forEach(group => addGroupSheet(workbook, hours, group, semester, usedNames))
  return { workbook, groups, semester, remainingWeeks: remainingTeachingWeeks(hours) }
}

export async function exportReferenceAccountingWorkbook({ hours }) {
  const { workbook, groups, semester } = buildReferenceAccountingWorkbook(hours)
  const buffer = await workbook.xlsx.writeBuffer()
  const filename = `Учет_часов_по_образцу_${semester}_семестр_${new Date().toISOString().slice(0, 10)}.xlsx`
  downloadBuffer(buffer, filename)
  return { filename, sheets: workbook.worksheets.length, groups: groups.length, semester }
}
