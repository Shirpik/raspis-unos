import ExcelJS from 'exceljs'
import { slotLessonEntries, splitSlotText, subgroupOrdinal } from './schedulePresentation.js'

const TEMPLATE_URL = '/templates/schedule-template.xlsx'
const TEMPLATE_GROUP_HEADER_ROW = 62
const FIRST_GROUP_COLUMN = 4
const MAX_TEMPLATE_SLOT = 7

const DAY_LAYOUT = {
  ПН: { headerRow: 1, firstLessonRow: 3 },
  ВТ: { headerRow: 17, firstLessonRow: 18 },
  СР: { headerRow: 32, firstLessonRow: 33 },
  ЧТ: { headerRow: 47, firstLessonRow: 48 },
  ПТ: { headerRow: 62, firstLessonRow: 63 },
  СБ: { headerRow: 77, firstLessonRow: 78 },
}

const normalizeCampus = value => {
  if (/кривоус/i.test(value || '')) return 'Кривоусова'
  if (/лесн/i.test(value || '')) return 'Лесная'
  return ''
}

const campusSuffix = campus => campus === 'Лесная' ? 'Л' : campus === 'Кривоусова' ? 'К' : ''

const lessonSubjectFromSegment = segment => {
  const index = String(segment || '').indexOf(' — ')
  return index >= 0 ? segment.slice(0, index) : segment
}

const lessonDetailsFromSegment = segment => {
  const index = String(segment || '').indexOf(' — ')
  return index >= 0 ? segment.slice(index + 3).split(', ').filter(Boolean) : []
}

const teacherFromDetails = details => [...details].reverse().find(item => {
  const value = String(item || '').trim()
  if (!value || normalizeCampus(value)) return false
  if (/^(вся группа|[12]-?я?\s*(подгруппа|п\/?г)|подгруппа)/i.test(value)) return false
  return /^[А-ЯЁ][А-Яа-яЁё-]+(?:\s+[А-ЯЁ][А-Яа-яЁё-]+){1,2}$/.test(value)
}) || ''

const dateParts = value => {
  const [day, month, year] = String(value || '').split('.').map(Number)
  return { day, month, year }
}

const compareDMY = (left, right) => {
  const a = dateParts(left)
  const b = dateParts(right)
  return Date.UTC(a.year, a.month - 1, a.day) - Date.UTC(b.year, b.month - 1, b.day)
}

const dateIso = day => {
  if (day?.date_iso) return day.date_iso
  const parts = dateParts(day?.date)
  if (!parts.day || !parts.month || !parts.year) return ''
  return `${parts.year}-${String(parts.month).padStart(2, '0')}-${String(parts.day).padStart(2, '0')}`
}

const excelDate = day => {
  const iso = dateIso(day)
  const [year, month, date] = iso.split('-').map(Number)
  return new Date(Date.UTC(year, month - 1, date))
}

const collectDates = groups => {
  const dates = new Map()
  for (const group of groups || []) {
    for (const day of group?.days || []) {
      const key = dateIso(day) || day.date
      if (key && !dates.has(key)) dates.set(key, day)
    }
  }
  return [...dates.values()].sort((a, b) => compareDMY(a.date, b.date))
}

const filenameDateRange = schedule => {
  const dates = collectDates(schedule?.groups).map(day => dateParts(day.date))
  if (!dates.length) return new Date().toISOString().slice(0, 10)
  const first = dates[0]
  const last = dates.at(-1)
  const pad = value => String(value).padStart(2, '0')
  if (dates.length === 1) return `${pad(first.day)}.${pad(first.month)}.${first.year}`
  if (first.month === last.month && first.year === last.year) {
    return `${pad(first.day)}-${pad(last.day)}.${pad(first.month)}.${first.year}`
  }
  if (first.year === last.year) {
    return `${pad(first.day)}.${pad(first.month)}-${pad(last.day)}.${pad(last.month)}.${first.year}`
  }
  return `${pad(first.day)}.${pad(first.month)}.${first.year}-${pad(last.day)}.${pad(last.month)}.${last.year}`
}

const templateGroupColumns = sheet => {
  const groups = []
  for (let column = FIRST_GROUP_COLUMN; column <= sheet.columnCount; column += 1) {
    const name = sheet.getCell(TEMPLATE_GROUP_HEADER_ROW, column).text.trim()
    if (!name) {
      if (groups.length) break
      continue
    }
    groups.push({ name, column })
  }
  if (!groups.length) throw new Error(`В листе «${sheet.name}» не найдены колонки групп`)
  return groups
}

const cellPairAddress = (sheet, column, firstRow, secondRow) =>
  `${sheet.getCell(firstRow, column).address}:${sheet.getCell(secondRow, column).address}`

const prepareCellPair = (sheet, column, firstRow, secondRow) => {
  const first = sheet.getCell(firstRow, column)
  const second = sheet.getCell(secondRow, column)
  if (first.isMerged || second.isMerged) sheet.unMergeCells(cellPairAddress(sheet, column, firstRow, secondRow))
  first.value = null
  second.value = null
  for (const cell of [first, second]) {
    cell.alignment = { ...cell.alignment, horizontal: 'center', vertical: 'middle', wrapText: true }
    cell.fill = { type: 'pattern', pattern: 'solid', fgColor: { argb: 'FFFFFFFF' } }
  }
  return { first, second }
}

const mergeCellPair = (sheet, column, firstRow, secondRow) => {
  sheet.mergeCells(cellPairAddress(sheet, column, firstRow, secondRow))
}

const lessonTemplateValue = (lesson, segment, groupIndex) => {
  const details = lessonDetailsFromSegment(segment)
  const subject = String(lesson?.name || lessonSubjectFromSegment(segment) || '').trim()
  const teacher = String(lesson?.teacher_name || teacherFromDetails(details) || '').trim()
  const surname = teacher.split(/\s+/).filter(Boolean)[0] || ''
  const room = lesson?.room_name === null || lesson?.room_name === undefined
    ? ''
    : String(lesson.room_name).trim()
  const campus = normalizeCampus(details.at(-1))
  const ordinal = subgroupOrdinal(lesson?.subgroup, groupIndex)
  const subjectLine = ordinal ? `${subject} ${ordinal} п/г` : subject
  const roomLine = room ? `${room}${campusSuffix(campus) ? `_${campusSuffix(campus)}` : ''}` : ''
  return {
    text: [subjectLine, [surname, roomLine].filter(Boolean).join(' ')].filter(Boolean).join('\n'),
    campus,
  }
}

const applyCampusFill = (cell, campus) => {
  cell.fill = {
    type: 'pattern',
    pattern: 'solid',
    fgColor: { argb: campus === 'Лесная' ? 'FFEFEFEF' : 'FFFFFFFF' },
  }
}

const findDay = (group, targetDate) => (group?.days || []).find(day =>
  dateIso(day) === dateIso(targetDate) || day.date === targetDate.date)

const expectedLessonCount = schedule => (schedule?.groups || []).reduce((groupTotal, group) =>
  groupTotal + (group.days || []).reduce((dayTotal, day) =>
    dayTotal + (day.slots || []).reduce((slotTotal, slot) =>
      slotTotal + (Array.isArray(slot.lessons) ? slot.lessons.length : splitSlotText(slot.text).length), 0), 0), 0)

export const scheduleExcelFilename = schedule => `${schedule?.status === 'draft_semester_risk' ? 'ПРОЕКТ_' : ''}Расписание_${filenameDateRange(schedule)}.xlsx`

export async function buildScheduleExcelWorkbook(schedule, templateBuffer) {
  if (!schedule?.groups?.length) throw new Error('Нет данных расписания для экспорта')
  if (!templateBuffer) throw new Error('Не загружен Excel-образец')

  const workbook = new ExcelJS.Workbook()
  await workbook.xlsx.load(templateBuffer)
  workbook.creator = 'Генератор расписания УСПО'
  workbook.modified = new Date()
  workbook.calcProperties.fullCalcOnLoad = true

  const groupsByName = new Map()
  for (const group of schedule.groups) {
    if (groupsByName.has(group.group_name)) throw new Error(`Группа «${group.group_name}» повторяется в расписании`)
    groupsByName.set(group.group_name, group)
  }

  const scheduleDates = collectDates(schedule.groups)
  const datesByWeekday = new Map()
  for (const day of scheduleDates) {
    const weekday = String(day.weekday || '').trim().toUpperCase()
    if (datesByWeekday.has(weekday)) {
      throw new Error('В образец одной недели нельзя записать несколько одинаковых дней недели. Выберите одну неделю для экспорта.')
    }
    datesByWeekday.set(weekday, dateIso(day))
  }
  const templateGroupNames = new Set()
  let insertedLessons = 0

  for (const sheet of workbook.worksheets) {
    if (schedule.status === 'draft_semester_risk') {
      sheet.headerFooter ||= {}
      sheet.headerFooter.oddHeader = '&CПРОЕКТ: недельная сетка; вычитка часов за 16 недель пока не подтверждена'
      sheet.headerFooter.oddFooter = '&LПроверьте отдельный отчёт рисков вычитки&R&P / &N'
      sheet.getCell('A1').note = 'ПРОЕКТ. Проверки недельной сетки выполнены отдельно от проверки полного срока вычитки. Есть нерешённые семестровые риски.'
    }
    // ExcelJS otherwise removes the escaped dot from the custom date format.
    // Keeping it makes the exported headers render exactly like the supplied
    // workbook (31.8, 1.9, ...), including in non-Microsoft viewers.
    for (const { headerRow } of Object.values(DAY_LAYOUT)) sheet.getCell(headerRow, 1).numFmt = 'd\\.m'
    const groups = templateGroupColumns(sheet)
    groups.forEach(group => templateGroupNames.add(group.name))

    for (const targetDay of scheduleDates) {
      const weekday = String(targetDay.weekday || '').trim().toUpperCase()
      const layout = DAY_LAYOUT[weekday]
      if (!layout) throw new Error(`День «${targetDay.weekday || targetDay.date}» не поддерживается Excel-образцом`)
      sheet.getCell(layout.headerRow, 1).value = excelDate(targetDay)
      if (weekday === 'ПН') {
        sheet.getCell(2, 2).value = '8.15-8.55'
        sheet.getCell(2, 3).value = 0
        sheet.getCell(2, 2).note = '07:55 — поднятие флага. 08:15–08:55 — классный час.'
        const bells = ['9.15-9.55', '10.00-10.40', '10.50-11.30', '11.35-12.15',
          '13.10-13.50', '13.55-14.35', '14.45-15.25', '15.30-16.10',
          '16.20-17.40', '', '17.50-19.10', '', '19.20-20.40', '']
        bells.forEach((value, index) => { sheet.getCell(index + 3, 2).value = value || null })
      }

      for (const { name, column } of groups) {
        const group = groupsByName.get(name)
        const day = findDay(group, targetDay)

        if (weekday === 'ПН') {
          const cell = sheet.getCell(2, column)
          cell.value = null
          const zero = (day?.slots || []).find(item => Number(item.slot) === 0)
          const entries = slotLessonEntries(zero, group?.group_index)
          if (entries.length > 1 || entries.some(entry => !entry.lesson?.is_class_hour))
            throw new Error(`${name}: в нулевой строке допустим только один классный час`)
          if (entries.length) {
            const value = lessonTemplateValue(entries[0].lesson, entries[0].segment, group?.group_index)
            cell.value = value.text
            cell.alignment = { horizontal: 'center', vertical: 'middle', wrapText: true }
            applyCampusFill(cell, value.campus)
            insertedLessons++
          }
        }

        for (let slotNumber = 1; slotNumber <= MAX_TEMPLATE_SLOT; slotNumber += 1) {
          const firstRow = layout.firstLessonRow + (slotNumber - 1) * 2
          const secondRow = firstRow + 1
          const { first, second } = prepareCellPair(sheet, column, firstRow, secondRow)
          const slot = (day?.slots || []).find(item => Number(item.slot) === slotNumber)
          const entries = slotLessonEntries(slot, group?.group_index)

          if (!entries.length) {
            mergeCellPair(sheet, column, firstRow, secondRow)
            continue
          }
          if (entries.length > 2) throw new Error(`${name}, ${targetDay.date}, ${slotNumber} пара: более двух занятий`)

          const wholeGroup = entries.filter(entry => subgroupOrdinal(entry.lesson?.subgroup, group?.group_index) === null)
          const subgroupLessons = entries.filter(entry => subgroupOrdinal(entry.lesson?.subgroup, group?.group_index) !== null)
          if (wholeGroup.length > 1 || (wholeGroup.length && subgroupLessons.length)) {
            throw new Error(`${name}, ${targetDay.date}, ${slotNumber} пара: несовместимый набор занятий`)
          }

          if (wholeGroup.length === 1) {
            const value = lessonTemplateValue(wholeGroup[0].lesson, wholeGroup[0].segment, group?.group_index)
            if (wholeGroup[0].lesson?.is_class_hour) {
              if (weekday !== 'ПН' || slotNumber < 2 || wholeGroup[0].lesson.half !== 2)
                throw new Error(`${name}: поздний классный час должен занимать вторую половину пары 2–7 понедельника`)
              second.value = value.text
              applyCampusFill(second, value.campus)
            } else {
              mergeCellPair(sheet, column, firstRow, secondRow)
              first.value = value.text
              applyCampusFill(first, value.campus)
            }
            insertedLessons += 1
            continue
          }

          const seenOrdinals = new Set()
          for (const entry of subgroupLessons) {
            const ordinal = subgroupOrdinal(entry.lesson?.subgroup, group?.group_index)
            if ((ordinal !== 1 && ordinal !== 2) || seenOrdinals.has(ordinal)) {
              throw new Error(`${name}, ${targetDay.date}, ${slotNumber} пара: неверно заданы подгруппы`)
            }
            seenOrdinals.add(ordinal)
            const cell = ordinal === 1 ? first : second
            const value = lessonTemplateValue(entry.lesson, entry.segment, group?.group_index)
            cell.value = value.text
            applyCampusFill(cell, value.campus)
            insertedLessons += 1
          }
        }
      }
    }
  }

  const extraGroups = [...groupsByName.keys()].filter(name => !templateGroupNames.has(name))
  if (extraGroups.length) throw new Error(`В Excel-образце нет групп: ${extraGroups.join(', ')}`)

  const expected = expectedLessonCount(schedule)
  if (insertedLessons !== expected) {
    throw new Error(`В Excel перенесено ${insertedLessons} из ${expected} занятий`)
  }

  return {
    workbook,
    insertedLessons,
    groups: groupsByName.size,
    sheets: workbook.worksheets.length,
    dates: scheduleDates.length,
  }
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

export async function exportScheduleExcel(schedule) {
  const response = await fetch(TEMPLATE_URL, { cache: 'no-store' })
  if (!response.ok) throw new Error(`Excel-образец не загрузился (${response.status})`)
  const result = await buildScheduleExcelWorkbook(schedule, await response.arrayBuffer())
  const filename = scheduleExcelFilename(schedule)
  downloadBuffer(await result.workbook.xlsx.writeBuffer(), filename)
  return { filename, ...result, workbook: undefined }
}
