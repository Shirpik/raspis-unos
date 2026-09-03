import ExcelJS from 'exceljs'
import { subgroupOrdinal } from './schedulePresentation.js'

const COLORS = {
  border: 'FF1F2937', yellow: 'FFFFFF99', paleYellow: 'FFFFFFCC', green: 'FFC6F7C6',
  peach: 'FFFFD9C2', blue: 'FFC6D9F1', red: 'FFFF0000', white: 'FFFFFFFF',
}
const thinBorder = {
  top: { style: 'thin', color: { argb: COLORS.border } }, left: { style: 'thin', color: { argb: COLORS.border } },
  bottom: { style: 'thin', color: { argb: COLORS.border } }, right: { style: 'thin', color: { argb: COLORS.border } },
}
const fill = argb => ({ type: 'pattern', pattern: 'solid', fgColor: { argb } })
const shortDate = value => {
  if (!value) return ''
  const [, month, day] = value.split('-')
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
const styleCell = (cell, { background, bold = false, vertical = false, align = 'center', color } = {}) => {
  cell.border = thinBorder
  cell.alignment = { horizontal: align, vertical: 'middle', wrapText: true, textRotation: vertical ? 90 : 0 }
  cell.font = { name: 'Times New Roman', size: 11, bold, color: color ? { argb: color } : undefined }
  if (background) cell.fill = fill(background)
}

function addScheduleSheet(workbook, hours, semester) {
  const sheet = workbook.addWorksheet('расписание', { views: [{ state: 'frozen', ySplit: 8, showGridLines: false }] })
  sheet.properties.defaultRowHeight = 20
  sheet.getColumn(1).width = 30
  sheet.getColumn(2).width = 16
  sheet.getColumn(3).width = 16
  sheet.mergeCells('A1:C1')
  sheet.getCell('A1').value = `Параметры учёта часов — ${semester} семестр`
  sheet.getCell('A1').font = { name: 'Times New Roman', size: 14, bold: true }
  sheet.getCell('A1').alignment = { horizontal: 'center' }
  sheet.getCell('A2').value = 'Дата начала'
  sheet.getCell('B2').value = hours.semester_start || hours.weeks?.[0]?.from || ''
  sheet.getCell('A3').value = 'Дата окончания'
  sheet.getCell('B3').value = hours.semester_end || hours.weeks?.at(-1)?.to || ''
  sheet.getCell('A4').value = 'Количество недель'
  sheet.getCell('B4').value = Math.max(1, hours.weeks?.length || 0)
  sheet.getCell('A5').value = 'Учебных месяцев в семестре'
  sheet.getCell('B5').value = 4.5
  sheet.getCell('A7').value = 'Неделя'
  sheet.getCell('B7').value = 'Дата начала'
  sheet.getCell('C7').value = 'Дата окончания'
  ;['A7', 'B7', 'C7'].forEach(address => styleCell(sheet.getCell(address), { background: COLORS.yellow, bold: true }))
  ;(hours.weeks || []).forEach((week, index) => {
    const row = index + 8
    sheet.getCell(row, 1).value = week.index
    sheet.getCell(row, 2).value = week.from
    sheet.getCell(row, 3).value = week.to
    for (let col = 1; col <= 3; col++) styleCell(sheet.getCell(row, col), { align: col === 1 ? 'center' : 'left' })
  })
  sheet.pageSetup = { orientation: 'portrait', fitToPage: true, fitToWidth: 1, fitToHeight: 0 }
}

function addTeacherSummary(workbook, hours, semester) {
  const weeks = hours.weeks || []
  const firstWeekCol = 9
  const sheet = workbook.addWorksheet('пр', { views: [{ state: 'frozen', xSplit: 8, ySplit: 2, showGridLines: false }] })
  sheet.properties.defaultRowHeight = 22
  ;[38, 12, 12, 15, 14, 16, 12, 14].forEach((width, index) => { sheet.getColumn(index + 1).width = width })
  weeks.forEach((_, index) => { sheet.getColumn(firstWeekCol + index).width = 8 })
  weeks.forEach((week, index) => {
    const col = firstWeekCol + index
    sheet.getCell(1, col).value = week.index
    sheet.getCell(2, col).value = shortDate(week.from)
    styleCell(sheet.getCell(1, col), { background: COLORS.yellow, bold: true })
    styleCell(sheet.getCell(2, col), { background: COLORS.yellow, bold: true, vertical: true })
  })
  const headers = ['преподаватель', 'Нагрузка', `${semester} семестр`, `выдано в ${semester} семестре`, `остаток в ${semester} семестре`, 'ср. недельная нагрузка', 'корректировка', 'корр. ср. на неделю']
  headers.forEach((header, index) => {
    const cell = sheet.getCell(2, index + 1)
    cell.value = header
    styleCell(cell, { background: index === 2 ? COLORS.blue : index >= 3 && index <= 7 ? COLORS.green : COLORS.paleYellow, bold: true, vertical: index > 0 })
  })
  sheet.getRow(2).height = 116
  const teachers = [...(hours.teachers || [])].sort((a, b) => a.teacher_name.localeCompare(b.teacher_name, 'ru'))
  teachers.forEach((teacher, index) => {
    const row = index + 3
    sheet.getCell(row, 1).value = teacher.teacher_name
    sheet.getCell(row, 2).value = { formula: `C${row}/'расписание'!$B$5` }
    sheet.getCell(row, 3).value = teacher.planned_hours || 0
    sheet.getCell(row, 4).value = teacher.planned_hours || 0
    sheet.getCell(row, 5).value = { formula: `C${row}-D${row}` }
    sheet.getCell(row, 6).value = { formula: `D${row}/'расписание'!$B$4` }
    sheet.getCell(row, 7).value = teacher.adjustment_hours || 0
    sheet.getCell(row, 8).value = { formula: `G${row}/'расписание'!$B$4` }
    weeks.forEach((_, weekIndex) => { sheet.getCell(row, firstWeekCol + weekIndex).value = teacher.weekly_hours?.[weekIndex] || 0 })
    for (let col = 1; col < firstWeekCol + weeks.length; col++) {
      const background = col === 6 ? COLORS.yellow : col >= 7 && col <= 8 ? COLORS.green : col >= firstWeekCol ? COLORS.paleYellow : undefined
      styleCell(sheet.getCell(row, col), { background, align: col === 1 ? 'left' : 'center' })
    }
    sheet.getCell(row, 2).numFmt = '0.0'
    sheet.getCell(row, 6).numFmt = '0.00'
    sheet.getCell(row, 8).numFmt = '0.00'
    if ((teacher.remaining_hours || 0) > 0) sheet.getCell(row, 5).font = { name: 'Times New Roman', size: 11, color: { argb: COLORS.red }, bold: true }
  })
  sheet.autoFilter = { from: { row: 2, column: 1 }, to: { row: Math.max(2, teachers.length + 2), column: firstWeekCol + weeks.length - 1 } }
  sheet.pageSetup = { orientation: 'landscape', fitToPage: true, fitToWidth: 1, fitToHeight: 0, paperSize: 9 }
}

function lessonWeekHours(lesson, weeks) {
  const values = Array(weeks.length).fill(0)
  for (const occurrence of lesson.scheduled_occurrences || []) {
    const index = Number(occurrence.week_index || 0) - 1
    if (index >= 0 && index < values.length) values[index] += occurrence.hours || 2
  }
  return values
}

function addGroupSheet(workbook, hours, group, semester, usedNames) {
  const weeks = hours.weeks || []
  const firstWeekCol = 10
  const lessons = (hours.lessons || []).filter(lesson => lesson.group_id === group.group_id)
    .sort((a, b) => (a.subject_id ?? 0) - (b.subject_id ?? 0) || (a.subgroup ?? -1) - (b.subgroup ?? -1) || a.name.localeCompare(b.name, 'ru'))
  const sheet = workbook.addWorksheet(safeSheetName(group.group_name, usedNames), { views: [{ state: 'frozen', xSplit: 9, ySplit: 5, showGridLines: false }] })
  sheet.properties.defaultRowHeight = 20
  ;[3, 14, 44, 12, 32, 12, 12, 14, 14].forEach((width, index) => { sheet.getColumn(index + 1).width = width })
  weeks.forEach((_, index) => { sheet.getColumn(firstWeekCol + index).width = 8 })
  const subgroup1Col = firstWeekCol + weeks.length
  const subgroup2Col = subgroup1Col + 1
  sheet.getColumn(subgroup1Col).width = 11
  sheet.getColumn(subgroup2Col).width = 11
  sheet.mergeCells(1, 2, 1, subgroup2Col)
  sheet.getCell(1, 2).value = `Учёт часов группы ${group.group_name} — ${semester} семестр`
  sheet.getCell(1, 2).font = { name: 'Times New Roman', size: 14, bold: true }
  sheet.getCell(1, 2).alignment = { horizontal: 'center', vertical: 'middle' }
  sheet.getRow(1).height = 30
  const headers = ['Индекс', 'Наименование циклов, разделов, дисциплин, профессиональных модулей, междисциплинарных курсов', `${semester} семестр`, 'преподаватель', `выдано в ${semester} семестре`, `остаток в ${semester} семестре`, 'ср. недельная нагрузка', 'корр. ср. нагрузка']
  headers.forEach((header, index) => {
    const col = index + 2
    sheet.mergeCells(2, col, 5, col)
    const cell = sheet.getCell(2, col)
    cell.value = header
    styleCell(cell, { background: col === 4 ? COLORS.blue : col >= 6 ? COLORS.green : col === 5 ? COLORS.paleYellow : undefined, bold: true, vertical: col >= 6 })
  })
  weeks.forEach((week, index) => {
    const col = firstWeekCol + index
    sheet.getCell(2, col).value = week.index
    sheet.getCell(3, col).value = shortDate(week.from)
    sheet.getCell(4, col).value = shortDate(week.to)
    sheet.getCell(5, col).value = 'часы'
    for (let row = 2; row <= 5; row++) styleCell(sheet.getCell(row, col), { background: COLORS.peach, bold: true })
  })
  ;[[subgroup1Col, '1 подгр.'], [subgroup2Col, '2 подгр.']].forEach(([col, label]) => {
    sheet.mergeCells(2, col, 5, col)
    sheet.getCell(2, col).value = label
    styleCell(sheet.getCell(2, col), { background: COLORS.green, bold: true, vertical: true })
  })
  ;[2, 3, 4, 5].forEach(row => { sheet.getRow(row).height = 28 })
  lessons.forEach((lesson, index) => {
    const row = index + 6
    const subjectIndex = Number.isFinite(Number(lesson.subject_id)) ? Number(lesson.subject_id) + 1 : index + 1
    sheet.getCell(row, 2).value = `Д-${String(subjectIndex).padStart(3, '0')}`
    sheet.getCell(row, 3).value = lesson.name
    sheet.getCell(row, 4).value = lesson.planned_hours || 0
    sheet.getCell(row, 5).value = lesson.teacher_name || 'вакансия'
    sheet.getCell(row, 6).value = lesson.planned_hours || 0
    sheet.getCell(row, 7).value = { formula: `D${row}-F${row}` }
    sheet.getCell(row, 8).value = { formula: `F${row}/'расписание'!$B$4` }
    sheet.getCell(row, 9).value = 0
    lessonWeekHours(lesson, weeks).forEach((value, weekIndex) => { sheet.getCell(row, firstWeekCol + weekIndex).value = value })
    const subgroup = subgroupOrdinal(lesson.subgroup, lesson.group_id)
    sheet.getCell(row, subgroup1Col).value = subgroup === 2 ? 0 : 1
    sheet.getCell(row, subgroup2Col).value = subgroup === 1 ? 0 : 1
    for (let col = 2; col <= subgroup2Col; col++) {
      const background = col === 7 ? ((lesson.planned_hours || 0) > (lesson.scheduled_hours || 0) ? COLORS.red : undefined) : col >= 8 && col <= 9 ? COLORS.green : col >= firstWeekCol && col < subgroup1Col ? COLORS.peach : undefined
      styleCell(sheet.getCell(row, col), { background, align: col === 3 || col === 5 ? 'left' : 'center', color: col === 7 && background === COLORS.red ? COLORS.white : undefined })
    }
    sheet.getCell(row, 8).numFmt = '0.0'
  })
  const totalRow = lessons.length + 6
  sheet.getCell(totalRow, 3).value = 'ИТОГО'
  for (let col = 4; col <= subgroup2Col; col++) {
    const letter = sheet.getColumn(col).letter
    sheet.getCell(totalRow, col).value = { formula: `SUM(${letter}6:${letter}${totalRow - 1})` }
    styleCell(sheet.getCell(totalRow, col), { background: COLORS.yellow, bold: true })
  }
  styleCell(sheet.getCell(totalRow, 3), { background: COLORS.yellow, bold: true, align: 'right' })
  sheet.pageSetup = { orientation: 'landscape', fitToPage: true, fitToWidth: 1, fitToHeight: 0, paperSize: 9, repeatRows: '1:5' }
}

export async function exportReferenceAccountingWorkbook({ hours }) {
  const workbook = new ExcelJS.Workbook()
  workbook.creator = 'Генератор расписания техникума'
  workbook.created = new Date()
  workbook.calcProperties.fullCalcOnLoad = true
  const semester = semesterNumber(hours.semester_start || hours.weeks?.[0]?.from)
  addScheduleSheet(workbook, hours, semester)
  addTeacherSummary(workbook, hours, semester)
  const usedNames = new Set(['расписание', 'пр'])
  const groups = [...(hours.groups || [])].sort((a, b) => a.group_name.localeCompare(b.group_name, 'ru'))
  groups.forEach(group => addGroupSheet(workbook, hours, group, semester, usedNames))
  const buffer = await workbook.xlsx.writeBuffer()
  const filename = `Учет_часов_по_образцу_${semester}_семестр_${new Date().toISOString().slice(0, 10)}.xlsx`
  downloadBuffer(buffer, filename)
  return { filename, sheets: workbook.worksheets.length, groups: groups.length, semester }
}
