import * as XLSX from 'xlsx'

const text = value => String(value ?? '').replace(/\r/g, '').trim()
const normalized = value => text(value)
  .toLocaleLowerCase('ru')
  .replaceAll('ё', 'е')
  // Common typo in the source workbook.  Treat it as the canonical subject
  // name so a completed lesson reduces the correct curriculum balance.
  .replaceAll('жизнидеятельности', 'жизнедеятельности')
  .replace(/лпз/giu, ' ')
  .replace(/(?:^|\s)(?:1|2)\s*(?:п\s*\/?\s*г|подгрупп[а-яё]*)(?=\s|$)/giu, ' ')
  .replace(/[^a-zа-я0-9]+/giu, ' ')
  .replace(/\s+/g, ' ')
  .trim()

// Class hours and practice/administrative markers occupy timetable cells but
// are not teaching hours.  They must remain visible in the source workbook,
// while being excluded from the teaching ledger used by hours accounting.
const isClassHour = value => /(?:класс[а-яё]*|кл\.?)[\s.]*час/iu.test(text(value))
const isNonTeachingMarker = value => /^впр$/iu.test(text(value))

const parseExcelDate = (value, fallbackYear = 2026) => {
  if (value instanceof Date && !Number.isNaN(value.valueOf())) {
    return `${value.getFullYear()}-${String(value.getMonth() + 1).padStart(2, '0')}-${String(value.getDate()).padStart(2, '0')}`
  }
  if (typeof value === 'number') {
    const parsed = XLSX.SSF.parse_date_code(value)
    if (parsed) return `${parsed.y}-${String(parsed.m).padStart(2, '0')}-${String(parsed.d).padStart(2, '0')}`
  }
  const source = text(value)
  const iso = source.match(/^(\d{4})-(\d{2})-(\d{2})$/)
  if (iso) return source
  const short = source.match(/^(\d{1,2})[.\/-](\d{1,2})(?:[.\/-](\d{2,4}))?$/)
  if (!short) return ''
  let year = Number(short[3] || fallbackYear)
  if (year < 100) year += 2000
  return `${year}-${String(Number(short[2])).padStart(2, '0')}-${String(Number(short[1])).padStart(2, '0')}`
}

const nextIsoDate = iso => {
  const date = new Date(`${iso}T12:00:00Z`)
  date.setUTCDate(date.getUTCDate() + 1)
  return date.toISOString().slice(0, 10)
}

const weekdayIndex = value => {
  const key = normalized(value)
  if (/^пон/u.test(key)) return 1
  if (/^втор/u.test(key)) return 2
  if (/^ср/u.test(key)) return 3
  if (/^чет/u.test(key)) return 4
  if (/^пят/u.test(key)) return 5
  if (/^суб/u.test(key)) return 6
  if (/^воск/u.test(key)) return 0
  return -1
}

const isoWeekday = iso => new Date(`${iso}T12:00:00Z`).getUTCDay()

const subgroupOrdinal = value => {
  const match = text(value).match(/(?:^|\s)([12])\s*(?:п\s*\/?\s*г|подгрупп[а-яё]*)(?=\s|:|$)/iu)
  return match ? Number(match[1]) : 0
}

const teacherSurname = value => {
  const match = text(value).match(/^\s*([А-ЯЁA-Z][А-Яа-яЁёA-Za-z-]+)/u)
  return match ? normalized(match[1]) : ''
}

const roomFromLine = value => {
  const source = text(value)
  const dot = source.split(/[·•]/u).map(part => part.trim()).filter(Boolean)
  if (dot.length > 1) return dot.at(-1)
  const match = source.match(/(?:^|\s)((?:каб\.?\s*)?[0-9]{1,3}[А-ЯA-Zа-яa-z/_-]*|[0-9]{1,3}[_-][КЛ])\s*$/u)
  return match ? match[1] : ''
}

const parseCell = raw => {
  const source = text(raw)
  if (!source || source === '1' || source === '-' || source.startsWith('#')) return null
  const lines = source.split('\n').map(line => line.trim()).filter(Boolean)
  if (!lines.length) return null
  if (isClassHour(lines.join(' '))) return { excludedClassHour: true }
  if (lines.length === 1 && isNonTeachingMarker(lines[0])) return { excludedNonTeaching: true }
  const last = lines.at(-1)
  const hasTeacherLine = /^[А-ЯЁA-Z][А-Яа-яЁёA-Za-z-]+(?:\s+[А-ЯЁA-Z]\.)?/u.test(last)
  const subject = (hasTeacherLine && lines.length > 1 ? lines.slice(0, -1) : lines).join(' ')
  return {
    raw: source,
    subject,
    subjectKey: normalized(subject),
    subgroup: subgroupOrdinal(subject),
    teacherSurname: hasTeacherLine ? teacherSurname(last) : '',
    teacherLine: hasTeacherLine ? last : '',
    room: hasTeacherLine ? roomFromLine(last) : '',
    isLab: /лпз/iu.test(subject),
  }
}

const tokens = value => new Set(normalized(value).split(' ').filter(token => token.length > 1))
const nameScore = (a, b) => {
  const left = normalized(a), right = normalized(b)
  if (!left || !right) return 0
  if (left === right) return 100
  if (left.includes(right) || right.includes(left)) return 82
  const aa = tokens(left), bb = tokens(right)
  const intersection = [...aa].filter(token => bb.has(token)).length
  const union = new Set([...aa, ...bb]).size
  return union ? Math.round(70 * intersection / union) : 0
}

// Confirmed by the dispatcher for the 21 September source: this timetable
// title is the same workload row for ИСП-3306п, despite its different wording.
const confirmedSubjectAliases = [
  {
    group: 'ИСП-3306п',
    source: 'Метрология, стандартизация и сертификация',
    target: 'Стандартизация, серификация и техническое документоведение',
  },
]

function chooseLesson(cell, groupId, inferredSubgroup, current, teachersById) {
  const groupName = (current.groups || []).find(group => Number(group.id) === Number(groupId))?.name || ''
  const alias = confirmedSubjectAliases.find(item =>
    normalized(item.group) === normalized(groupName) && normalized(item.source) === cell.subjectKey)
  if (alias) {
    const lesson = (current.lessons || []).find(item =>
      Number(item.group) === Number(groupId) && item.curriculum_active !== false && normalized(item.name) === normalized(alias.target))
    if (lesson) return { lesson, teacherMismatch: Boolean(cell.teacherSurname && !normalized(teachersById.get(Number(lesson.teacher))).startsWith(cell.teacherSurname)) }
  }
  const candidates = (current.lessons || [])
    .filter(lesson => Number(lesson.group) === Number(groupId) && lesson.curriculum_active !== false)
    .map(lesson => {
      let score = nameScore(cell.subject, lesson.name)
      if (score < 35) return null
      const lessonTeacher = teachersById.get(Number(lesson.teacher)) || ''
      const surnameMatches = cell.teacherSurname && normalized(lessonTeacher).startsWith(cell.teacherSurname)
      if (surnameMatches) score += 32
      else if (cell.teacherSurname) score -= 12
      const ordinal = cell.subgroup || inferredSubgroup
      const subgroup = Number(lesson.subgroup ?? -1)
      if (cell.subgroup) score += subgroup >= 0 && subgroup % 2 === cell.subgroup - 1 ? 24 : subgroup < 0 ? -18 : -30
      else if (ordinal && subgroup >= 0) score += subgroup % 2 === ordinal - 1 ? 10 : -10
      else if (subgroup < 0) score += 8
      if (cell.isLab === (lesson.is_lab === true || /лпз/iu.test(lesson.name || ''))) score += 5
      return { lesson, score, surnameMatches }
    })
    .filter(Boolean)
    .sort((a, b) => b.score - a.score || Number(a.lesson.id) - Number(b.lesson.id))
  if (!candidates.length || candidates[0].score < 55) return { error: 'Занятие не найдено в нагрузке' }
  if (candidates[1] && candidates[0].score === candidates[1].score) {
    return { error: `Неоднозначное сопоставление: ${candidates[0].lesson.name} / ${candidates[1].lesson.name}` }
  }
  return { lesson: candidates[0].lesson, teacherMismatch: Boolean(cell.teacherSurname && !candidates[0].surnameMatches) }
}

function inferredSubgroupForCell(rows, rowIndex, column, half, blockEnd) {
  if (half === 2) return 2
  if (rowIndex + 1 >= blockEnd) return 0
  const next = rows[rowIndex + 1] || []
  if (text(next[2]) !== '') return 0
  const continuation = parseCell(next[column])
  return continuation && !continuation.excludedClassHour && !continuation.excludedNonTeaching ? 1 : 0
}

function sheetBlocks(rows, knownGroups) {
  const blocks = []
  let previousDate = ''
  for (let rowIndex = 0; rowIndex < rows.length; rowIndex++) {
    const row = rows[rowIndex] || []
    const columns = []
    row.forEach((value, column) => {
      const key = normalized(value)
      if (knownGroups.has(key)) columns.push({ column, group: knownGroups.get(key) })
    })
    if (!columns.length) continue
    let date = parseExcelDate(row[0])
    const labelDay = weekdayIndex(rows[rowIndex + 1]?.[0])
    // Some exported sheets repeat the previous date in the next block's
    // header.  Prefer the visible weekday label when it disagrees, so the
    // completed ledger keeps Wednesday/Thursday on their actual calendar day.
    if (previousDate && labelDay >= 0 && isoWeekday(date || previousDate) !== labelDay) {
      let candidate = nextIsoDate(previousDate)
      for (let step = 0; step < 7 && isoWeekday(candidate) !== labelDay; step++) candidate = nextIsoDate(candidate)
      date = candidate
    }
    if (!date && previousDate) date = nextIsoDate(previousDate)
    const nextHeader = rows.findIndex((candidate, index) => index > rowIndex && (candidate || []).some(value => knownGroups.has(normalized(value))))
    blocks.push({ header: rowIndex, from: rowIndex + 1, to: nextHeader < 0 ? rows.length : nextHeader, date, columns })
    if (date) previousDate = date
    if (nextHeader >= 0) rowIndex = nextHeader - 1
  }
  return blocks
}

export async function parseCompletedSchedule(file, current, options = {}) {
  const bytes = await file.arrayBuffer()
  const workbook = XLSX.read(bytes, { type: 'array', cellDates: true, raw: true })
  const knownGroups = new Map((current.groups || []).map(group => [normalized(group.name), group]))
  const teachersById = new Map((current.teachers || []).map(teacher => [Number(teacher.id), teacher.name || '']))
  const teacherIdBySurname = new Map()
  for (const [id, name] of teachersById) {
    const surname = normalized(name).split(' ')[0]
    if (!surname) continue
    teacherIdBySurname.set(surname, teacherIdBySurname.has(surname) ? -1 : id)
  }
  const status = options.status === 'planned' ? 'planned' : 'confirmed'
  const rowsToImport = []
  const errors = [], warnings = []
  let excludedClassHours = 0, ignoredCells = 0

  for (const sheetName of workbook.SheetNames) {
    const sheet = workbook.Sheets[sheetName]
    const rows = XLSX.utils.sheet_to_json(sheet, { header: 1, defval: '', raw: true })
    for (const block of sheetBlocks(rows, knownGroups)) {
      if (!block.date) {
        errors.push(`${sheetName}: не удалось определить дату блока со строки ${block.header + 1}`)
        continue
      }
      if (options.dateFrom && block.date < options.dateFrom) continue
      if (options.dateTo && block.date > options.dateTo) continue
      let slot = 0
      let half = 1
      for (let rowIndex = block.from; rowIndex < block.to; rowIndex++) {
        const row = rows[rowIndex] || []
        const rawSlot = text(row[2]) === '' ? Number.NaN : Number(row[2])
        if (Number.isInteger(rawSlot) && rawSlot >= 0 && rawSlot <= 7) {
          slot = rawSlot
          half = 1
        } else if (slot > 0) half = 2
        if (slot <= 0) {
          for (const entry of block.columns) {
            const cell = parseCell(row[entry.column])
            if (cell?.excludedClassHour || cell?.excludedNonTeaching) excludedClassHours++
          }
          continue
        }
        for (const entry of block.columns) {
          const cell = parseCell(row[entry.column])
          if (!cell) { ignoredCells++; continue }
          if (cell.excludedClassHour || cell.excludedNonTeaching) { excludedClassHours++; continue }
          // A numbered row is also the first visual half of a split pair, but it
          // must not automatically mean "1 subgroup".  Whole-group lessons use
          // that same row.  Infer subgroup 1 only when the following half-row
          // actually contains another lesson for this group; subgroup 2 is the
          // continuation row itself.
          const inferredSubgroup = cell.subgroup ? 0 : inferredSubgroupForCell(rows, rowIndex, entry.column, half, block.to)
          const match = chooseLesson(cell, entry.group.id, inferredSubgroup, current, teachersById)
          if (!match.lesson) {
            errors.push(`${sheetName}, ${block.date}, ${entry.group.name}, пара ${slot}: ${match.error} — «${cell.subject}»`)
            continue
          }
          const plannedTeacher = teachersById.get(Number(match.lesson.teacher)) || ''
          const actualTeacher = teacherIdBySurname.get(cell.teacherSurname)
          if (match.teacherMismatch) warnings.push(`${sheetName}, ${block.date}, ${entry.group.name}, пара ${slot}: в таблице «${cell.teacherLine}», в нагрузке «${plannedTeacher}»`)
          rowsToImport.push({
            lesson_id: Number(match.lesson.id),
            date: block.date,
            slot,
            hours: 2,
            status,
            source_file: file.name,
            source_sheet: sheetName,
            group_id: Number(entry.group.id),
            source_subject: cell.subject,
            source_teacher: cell.teacherLine,
            room: cell.room,
            ...(Number.isInteger(actualTeacher) && actualTeacher >= 0 ? { actual_teacher: actualTeacher } : {}),
          })
        }
      }
    }
  }

  const unique = new Map()
  let duplicates = 0
  for (const item of rowsToImport) {
    const key = `${item.lesson_id}|${item.date}|${item.slot}`
    if (unique.has(key)) { duplicates++; continue }
    unique.set(key, item)
  }
  const imported = [...unique.values()]
  const importedDates = new Set(imported.map(item => item.date))
  const retained = (current.teaching_ledger || []).filter(item =>
    !importedDates.has(String(item.date || '')) || item.status === 'void')
  let nextId = Math.max(-1, ...retained.map(item => Number(item.id ?? -1))) + 1
  const importedAt = new Date().toISOString()
  const ledger = [...retained, ...imported.map(item => ({ id: nextId++, imported_at: importedAt, ...item }))]
    .sort((a, b) => String(a.date).localeCompare(String(b.date)) || Number(a.slot) - Number(b.slot) || Number(a.lesson_id) - Number(b.lesson_id))

  return {
    data: { ...current, teaching_ledger: ledger },
    errors,
    warnings,
    fileName: file.name,
    sheetCount: workbook.SheetNames.length,
    importedDates: [...importedDates].sort(),
    imported: imported.length,
    replaced: (current.teaching_ledger || []).length - retained.length,
    duplicates,
    excludedClassHours,
    ignoredCells,
  }
}
