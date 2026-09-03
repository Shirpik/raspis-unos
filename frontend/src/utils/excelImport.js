import * as XLSX from 'xlsx'

const text = value => String(value ?? '').replace(/\s+/g, ' ').trim()
const key = value => text(value).toLocaleLowerCase('ru-RU')
const number = value => {
  const n = typeof value === 'number' ? value : Number(String(value ?? '').replace(',', '.'))
  return Number.isFinite(n) ? n : null
}

function subgroupOf(name, groupId) {
  if (/1\s*(подгруппа|п\/?г)/i.test(name)) return groupId * 2
  if (/2\s*(подгруппа|п\/?г)/i.test(name)) return groupId * 2 + 1
  return -1
}
function cleanSubject(name) {
  return text(name).replace(/\s*[12]\s*(подгруппа|п\/?г)\s*/ig, ' ').replace(/\s+/g, ' ').trim()
}
function lessonKey(l) { return [l.group, key(l.name), l.subgroup, l.teacher ?? -1].join('|') }

function findColumns(rows, semester) {
  let headerRow = -1, subject = -1, teacher = -1, index = 1
  for (let r = 0; r < Math.min(rows.length, 18); r++) {
    for (let c = 0; c < (rows[r]?.length || 0); c++) {
      const v = key(rows[r][c])
      if (v.includes('наименование')) { headerRow = r; subject = c }
      if (v.includes('преподавател')) teacher = c
      if (v === 'индекс') index = c
    }
  }
  let hours = semester === 2 ? 8 : 4
  const wantedParity = semester === 2 ? 0 : 1
  let foundSemester = false
  for (let r = 0; r < Math.min(rows.length, 12); r++) {
    for (let c = 0; c < (rows[r]?.length || 0); c++) {
      const match = key(rows[r][c]).match(/(?:^|\s)([1-8])\s*сем/)
      if (match && Number(match[1]) % 2 === wantedParity) { hours = c; foundSemester = true; break }
    }
    if (foundSemester) break
  }
  return { headerRow, subject, teacher, index, hours }
}

export async function parseVkleyki(file, current, semester = 1) {
  const workbook = XLSX.read(await file.arrayBuffer(), { type: 'array', cellDates: true })
  const errors = [], warnings = []
  const oldGroups = current.groups || [], oldTeachers = current.teachers || [], oldLessons = current.lessons || []
  const groupByName = new Map(oldGroups.map(g => [key(g.name), g]))
  const teacherByName = new Map(oldTeachers.map(t => [key(t.name), t]))
  const oldLessonByKey = new Map(oldLessons.map(l => [lessonKey(l), l]))
  let nextGroupId = Math.max(-1, ...oldGroups.map(x => x.id ?? -1)) + 1
  let nextTeacherId = Math.max(-1, ...oldTeachers.map(x => x.id ?? -1)) + 1
  let nextLessonId = Math.max(-1, ...oldLessons.map(x => x.id ?? -1)) + 1
  let nextSubjectId = Math.max(-1, ...oldLessons.map(x => x.subject_id ?? -1)) + 1
  const groups = [], teachers = [], lessons = []
  const subjectIds = new Map()
  let vacancyCount = 0, skippedRows = 0

  const getTeacher = raw => {
    const name = text(raw)
    if (!name || key(name).includes('вакансия')) { vacancyCount++; return -1 }
    let teacher = teacherByName.get(key(name))
    if (!teacher) { teacher = { id: nextTeacherId++, name }; teacherByName.set(key(name), teacher) }
    if (!teachers.some(t => t.id === teacher.id)) teachers.push({ ...teacher })
    return teacher.id
  }

  for (const sheetName of workbook.SheetNames) {
    const rows = XLSX.utils.sheet_to_json(workbook.Sheets[sheetName], { header: 1, raw: true, defval: null })
    const cols = findColumns(rows, semester)
    if (cols.headerRow < 0 || cols.subject < 0 || cols.teacher < 0) {
      errors.push({ sheet: sheetName, message: 'Не найдены колонки «Наименование» или «преподаватель»' })
      continue
    }
    const name = text(sheetName)
    const previous = groupByName.get(key(name))
    const group = previous ? { ...previous } : { id: nextGroupId++, name, parts: 2, size: 0, home_campus: 0 }
    groups.push(group)

    for (let r = cols.headerRow + 1; r < rows.length; r++) {
      const rawName = text(rows[r]?.[cols.subject])
      if (!rawName) continue
      const hours = number(rows[r]?.[cols.hours])
      if (hours === null || hours <= 0) { skippedRows++; continue }
      const nameClean = cleanSubject(rawName)
      const isBlock = /^(УП|ВУП)\./i.test(nameClean)
      const teacher = getTeacher(rows[r]?.[cols.teacher])
      const subgroup = subgroupOf(rawName, group.id)
      const indexValue = text(rows[r]?.[cols.index])
      const subjectKey = `${key(group.name)}|${key(indexValue || nameClean.replace(/^ЛПЗ[.\s]+/i, ''))}`
      if (!subjectIds.has(subjectKey)) subjectIds.set(subjectKey, nextSubjectId++)
      const draft = {
        group: group.id, subgroup, teacher, name: nameClean,
        total_hours: Math.round(hours), total_slots: Math.max(1, Math.ceil(hours / (isBlock ? 6 : 2))),
        subject_id: /^(КП|УП|ВУП)[.\s]/i.test(nameClean) ? -1 : subjectIds.get(subjectKey),
        is_lab: /^ЛПЗ[.\s]/i.test(nameClean), is_block: isBlock, is_pp: /^ПП\./i.test(nameClean),
        allowed_campuses: [0, 1], week_parity: 'all', fixed_room: -1,
        allow_room_substitution: true, required_room_type: 1,
        required_capacity: group.size || 0, required_equipment: []
      }
      const previousLesson = oldLessonByKey.get(lessonKey(draft))
      lessons.push({ ...(previousLesson || {}), ...draft, plan_active: true, id: previousLesson?.id ?? nextLessonId++ })
    }
  }

  for(const teacher of oldTeachers) if(!teachers.some(t=>t.id===teacher.id)) teachers.push({...teacher})
  for(const group of oldGroups) if(!groups.some(g=>g.id===group.id)) groups.push({...group})
  for(const lesson of oldLessons) if(!lessons.some(l=>l.id===lesson.id)) lessons.push({...lesson,plan_active:false})
  teachers.sort((a,b) => a.id - b.id); groups.sort((a,b) => a.id - b.id); lessons.sort((a,b) => a.id - b.id)
  if (vacancyCount) warnings.push(`${vacancyCount} строк без назначенного преподавателя (вакансии)`)
  if (skippedRows) warnings.push(`${skippedRows} строк без часов выбранного семестра пропущено`)
  const changes = {
    groups: { before: oldGroups.length, after: groups.length },
    teachers: { before: oldTeachers.length, after: teachers.length },
    lessons: { before: oldLessons.length, after: lessons.length }
  }
  const data = {
    ...current, schema_version: 4, groups, teachers, lessons,
    room_types: current.room_types || [], rooms: current.rooms || [], unavailable: current.unavailable || [],
    teacher_unavailable: current.teacher_unavailable || [],
    substitutions: current.substitutions || [], accounting_adjustments: current.accounting_adjustments || [],
    workload_imports: [...(current.workload_imports||[]),{id:Math.max(-1,...(current.workload_imports||[]).map(x=>x.id??-1))+1,file_name:file.name,semester,imported_at:new Date().toISOString(),groups:groups.length,teachers:teachers.length,active_lessons:lessons.filter(l=>l.plan_active!==false).length}]
  }
  return { data, errors, warnings, changes, semester, sheetCount: workbook.SheetNames.length, fileName: file.name }
}
