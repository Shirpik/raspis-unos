import pdfMake from 'pdfmake/build/pdfmake.js'
import pdfFonts from 'pdfmake/build/vfs_fonts.js'
import { collectSlotNumbers, scheduleRowHeight, slotLessonEntries, splitSlotText, subgroupOrdinal } from './schedulePresentation.js'

pdfMake.vfs = pdfFonts?.pdfMake?.vfs || pdfFonts?.vfs || pdfFonts

const DAYS = {
  ПН: 'ПОНЕДЕЛЬНИК',
  ВТ: 'ВТОРНИК',
  СР: 'СРЕДА',
  ЧТ: 'ЧЕТВЕРГ',
  ПТ: 'ПЯТНИЦА',
  СБ: 'СУББОТА',
  ВС: 'ВОСКРЕСЕНЬЕ',
}

const courseLabel = course => `${course} курс`

const dateShort = value => {
  const [d, m] = String(value || '').split('.')
  return d && m ? `${d}.${m}` : String(value || '')
}

const parseDMY = value => {
  const [d, m, y] = String(value || '').split('.').map(Number)
  return new Date(y || 2000, (m || 1) - 1, d || 1)
}

const compareDMY = (a, b) => parseDMY(a) - parseDMY(b)

const timeText = value => {
  const match = String(value || '').match(/\(([^)]+)\)/)
  return (match ? match[1] : String(value || '')).replace(/:/g, '.').replace(/\s*-\s*/g, '-')
}

const detectCourseYear = name => {
  const runs = String(name || '').match(/\d{3,4}/g) || []
  const firstDigit = Number(runs[0]?.[0])
  if (firstDigit >= 1 && firstDigit <= 4) return firstDigit
  return 1
}

const weekdayFull = day => DAYS[String(day?.weekday || '').toUpperCase()] || String(day?.weekday || '').toUpperCase()

const normalizeCampus = value => {
  if (/кривоус/i.test(value || '')) return 'Кривоусова'
  if (/лесн/i.test(value || '')) return 'Лесная'
  return ''
}

const campusSuffix = campus => {
  if (/кривоус/i.test(campus || '')) return '_К'
  if (/лесн/i.test(campus || '')) return '_Л'
  return campus ? `_${campus}` : ''
}

const shortTeacher = name => {
  const parts = String(name || '').trim().split(/\s+/).filter(Boolean)
  if (parts.length < 2) return parts[0] || ''
  const [surname, first, patronymic] = parts
  const initials = [first, patronymic].filter(Boolean).map(part => `${part[0]}.`).join('')
  return `${surname} ${initials}`
}

const lessonSubjectFromSegment = segment => {
  const idx = String(segment || '').indexOf(' — ')
  return idx >= 0 ? segment.slice(0, idx) : segment
}

const lessonDetailsFromSegment = segment => {
  const idx = String(segment || '').indexOf(' — ')
  return idx >= 0 ? segment.slice(idx + 3).split(', ').filter(Boolean) : []
}

const teacherFromDetails = details => [...details].reverse().find(item => {
  const value = String(item || '').trim()
  if (!value || normalizeCampus(value)) return false
  if (/^(вся группа|[12]-?я?\s*(подгруппа|п\/?г)|подгруппа)/i.test(value)) return false
  return /^[А-ЯЁ][А-Яа-яЁё-]+(?:\s+[А-ЯЁ][А-Яа-яЁё-]+){1,2}$/.test(value)
}) || ''

const slotCampus = slot => {
  for (const segment of splitSlotText(slot?.text)) {
    const details = lessonDetailsFromSegment(segment)
    const campus = normalizeCampus(details.at(-1) || '')
    if (campus) return campus
  }
  return ''
}

const roomLabel = (lesson, details) => {
  const room = lesson?.room_name !== null && lesson?.room_name !== undefined && String(lesson.room_name).trim() !== ''
    ? String(lesson.room_name).trim()
    : ''
  const campus = normalizeCampus(details.at(-1) || '')
  if (!room) return campus || ''
  return `${room}${campusSuffix(campus)}`
}

const lessonCellText = (lesson, segment, groupIndex) => {
  const details = lessonDetailsFromSegment(segment)
  const subject = lesson?.name || lessonSubjectFromSegment(segment)
  const teacher = shortTeacher(lesson?.teacher_name || teacherFromDetails(details))
  const room = roomLabel(lesson, details)
  const ordinal = subgroupOrdinal(lesson?.subgroup, groupIndex)
  const subgroup = ordinal ? `${ordinal} п/г: ` : ''
  const meta = [teacher, room].filter(Boolean).join(' · ')
  return [subgroup + subject, meta].filter(Boolean).join('\n')
}

const slotCellText = (slot, groupIndex) => {
  if (!slot) return ''
  return slotLessonEntries(slot, groupIndex)
    .map(entry => entry.lesson
      ? lessonCellText(entry.lesson, entry.segment, groupIndex)
      : entry.segment)
    .filter(Boolean)
    .join('\n\n')
}

const collectDates = groups => {
  const dates = new Map()
  for (const group of groups) {
    for (const day of group.days || []) {
      if (!dates.has(day.date)) dates.set(day.date, day)
    }
  }
  return [...dates.values()].sort((a, b) => compareDMY(a.date, b.date))
}

const findDay = (group, date) => (group.days || []).find(day => day.date === date)

const findSlot = (group, date, slotNo) => {
  const day = findDay(group, date)
  return (day?.slots || []).find(slot => Number(slot.slot) === slotNo)
}

const groupByCourse = schedule => {
  const byCourse = new Map([[1, []], [2, []], [3, []], [4, []]])
  for (const group of schedule?.groups || []) byCourse.get(detectCourseYear(group.group_name)).push(group)
  return byCourse
}

const buildCourseMatrix = groups => {
  const dates = collectDates(groups)
  const slots = collectSlotNumbers(groups)
  const rows = []
  const campuses = []
  const merges = []

  for (const day of dates) {
    const startRow = rows.length
    rows.push([dateShort(day.date), '', '', ...groups.map(group => group.group_name)])
    campuses.push(Array(3 + groups.length).fill(''))
    for (const slotNo of slots) {
      const slotForTime = groups.map(group => findSlot(group, day.date, slotNo)).find(Boolean)
      rows.push([
        slotNo === slots[0] ? weekdayFull(day) : '',
        timeText(slotForTime?.time),
        slotNo,
        ...groups.map(group => slotCellText(findSlot(group, day.date, slotNo), group.group_index)),
      ])
      campuses.push([
        '', '', '',
        ...groups.map(group => slotCampus(findSlot(group, day.date, slotNo))),
      ])
    }
    merges.push({ s: { r: startRow + 1, c: 0 }, e: { r: startRow + slots.length, c: 0 } })
  }

  return { rows, campuses, merges, blockSize: slots.length + 1 }
}

const filenameDateRange = schedule => {
  const dates = collectDates(schedule?.groups || []).map(day => day.date)
  if (!dates.length) return new Date().toISOString().slice(0, 10)
  return `${dateShort(dates[0])}-${dateShort(dates.at(-1))}`.replace(/\./g, '-')
}

export const schedulePdfFilename = schedule =>
  `Расписание_${filenameDateRange(schedule)}_по_образцу.pdf`

const pdfTableLayout = {
  hLineWidth: () => 0.45,
  vLineWidth: () => 0.45,
  hLineColor: () => '#111111',
  vLineColor: () => '#111111',
  paddingLeft: () => 2,
  paddingRight: () => 2,
  paddingTop: () => 2,
  paddingBottom: () => 2,
}

const pdfCell = (text, options = {}) => ({
  text: String(text ?? ''),
  alignment: 'center',
  margin: [0, 1, 0, 1],
  ...options,
})

const buildPdfCourseTable = (course, groups) => {
  const { rows, campuses, blockSize } = buildCourseMatrix(groups)
  const body = rows.map((row, rowIndex) => {
    const isHeader = rowIndex % blockSize === 0
    return row.map((cell, colIndex) => {
      const isLesnaya = colIndex > 2 && campuses[rowIndex]?.[colIndex] === 'Лесная'
      return pdfCell(cell, {
      bold: isHeader || colIndex <= 2,
      fillColor: isLesnaya ? '#808080' : (isHeader || colIndex <= 2 ? '#BDD7EE' : '#FFFFFF'),
      color: isLesnaya ? '#FFFFFF' : '#000000',
      fontSize: isHeader ? 5.4 : 4.8,
      })
    })
  })
  const widths = [28, 44, 16, ...Array.from({ length: groups.length }, () => '*')]
  return [
    { text: courseLabel(course), bold: true, fontSize: 11, margin: [0, 0, 0, 4] },
    { table: { widths, body, dontBreakRows: false }, layout: pdfTableLayout, pageBreak: 'after' },
  ]
}

export function buildSchedulePdfDefinition(schedule) {
  const content = []
  for (const [course, groups] of groupByCourse(schedule).entries()) {
    if (!groups.length) continue
    content.push(...buildPdfCourseTable(course, groups))
  }
  if (content.at(-1)?.pageBreak) delete content.at(-1).pageBreak

  return {
    pageOrientation: 'landscape',
    // A2 keeps the dispatcher grid for each course on one page even when
    // cells contain long discipline names. When printed on A3/A4 the PDF
    // viewer can scale it down without splitting the last study day.
    pageSize: 'A2',
    pageMargins: [16, 24, 16, 20],
    defaultStyle: { font: 'Roboto', fontSize: 5, lineHeight: 1.05 },
    header: { text: 'Расписание занятий', alignment: 'center', margin: [0, 8, 0, 0], bold: true, fontSize: 10 },
    footer: (current, count) => ({ text: `${current} / ${count}`, alignment: 'center', fontSize: 7, margin: [0, 0, 0, 6] }),
    content,
  }
}

export function createSchedulePdf(schedule) {
  return pdfMake.createPdf(buildSchedulePdfDefinition(schedule))
}

export function exportSchedulePdf(schedule) {
  createSchedulePdf(schedule).download(schedulePdfFilename(schedule))
}
