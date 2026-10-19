import test from 'node:test'
import assert from 'node:assert/strict'
import ExcelJS from 'exceljs'
import JSZip from 'jszip'
import { buildReferenceAccountingWorkbook } from '../src/utils/referenceAccountingExport.js'
import { writeReferenceAccountingBuffer } from '../src/utils/referenceAccountingNotes.js'
import { referenceGroupWeeks, remainingTeachingWeeks, referenceTeacherConductedNote, referenceTeacherLoad, referenceWeeklyHours } from '../src/utils/referenceAccountingMetrics.js'

const near = (actual, expected) => assert.ok(Math.abs(actual - expected) < 1e-9, `${actual} is not close to ${expected}`)

test('reference weeks follow dated journal events instead of stale solver week indices', () => {
  const weeks=[{index:1,from:'2026-09-01',to:'2026-09-05'},{index:2,from:'2026-09-07',to:'2026-09-12'},{index:3,from:'2026-09-14',to:'2026-09-19'}]
  assert.deepEqual(referenceWeeklyHours({occurrences:[{date:'2026-09-05',week_index:1,hours:2},{date:'2026-09-07',week_index:1,hours:6},{date:'2026-09-12',week_index:2,hours:2}]},weeks),[2,8,0])
  assert.equal(remainingTeachingWeeks({weeks,as_of_date:'2026-09-05'}),2)
  assert.equal(remainingTeachingWeeks({weeks,as_of_date:'2026-09-07'}),1)
})

const hours = {
  semester_start: '2026-09-02',
  weeks: [{ index: 1, from: '2026-09-02', to: '2026-09-08' }, { index: 2, from: '2026-09-09', to: '2026-09-15' }],
  teachers: [{ teacher_id: 0, teacher_name: 'Круглова Юлия Петровна', planned_hours: 58, scheduled_hours: 10, credited_hours: 10, remaining_hours: 48, adjustment_hours: 0, weekly_hours: [10, 0] }],
  groups: [{ group_id: 4, group_name: 'СП-Пф-1605', accounting_title: '15.02.19 Сварочное производство' }],
  lessons: [
    { lesson_id: 84, group_id: 4, teacher_id: 0, group_name: 'СП-Пф-1605', teacher_name: 'Круглова Юлия Петровна', source_index: 'ОУП.08', subject_id: 71, subgroup: -1, name: 'информатика', planned_hours: 10, scheduled_hours: 10, scheduled_occurrences: [{ week_index: 1, hours: 2 }, { week_index: 1, hours: 2 }, { week_index: 1, hours: 2 }, { week_index: 1, hours: 2 }, { week_index: 1, hours: 2 }] },
    { lesson_id: 85, group_id: 4, teacher_id: 0, group_name: 'СП-Пф-1605', teacher_name: 'Круглова Юлия Петровна', source_index: 'ОУП.08', subject_id: 71, subgroup: 8, name: 'информатика', planned_hours: 24, scheduled_hours: 0, scheduled_occurrences: [] },
    { lesson_id: 86, group_id: 4, teacher_id: 0, group_name: 'СП-Пф-1605', teacher_name: 'Круглова Юлия Петровна', source_index: 'ОУП.08', subject_id: 71, subgroup: 9, name: 'информатика', planned_hours: 24, scheduled_hours: 0, scheduled_occurrences: [] },
  ],
}

test('reference accounting export follows the supplied sheet order and uses confirmed hours', async () => {
  const { workbook, remainingWeeks } = buildReferenceAccountingWorkbook(hours)
  assert.equal(remainingWeeks, 1)
  assert.deepEqual(workbook.worksheets.map(sheet => sheet.name), ['Расписание', 'пр', 'СП-Пф-1605', 'Даты занятий'])
  const teachers = workbook.getWorksheet('пр')
  assert.equal(teachers.getCell('B3').value.formula, 'G3')
  assert.equal(teachers.getCell('B3').value.result, 48)
  assert.equal(referenceTeacherLoad(hours.teachers[0], hours), teachers.getCell('B3').value.result)
  assert.equal(teachers.getCell('C3').value.result, 58)
  assert.equal(teachers.getCell('D3').value.result, 10)
  assert.equal(teachers.getCell('E3').value.result, 48)
  assert.match(teachers.getCell('F3').value.formula, /MAX\(0,E3\)\/\$[A-Z]+\$2/)
  assert.equal(teachers.getCell('F3').value.result, 48)
  assert.equal(teachers.getCell('I3').value.result, 10)
  const group = workbook.getWorksheet('СП-Пф-1605')
  assert.equal(group.getCell('B2').value, '15.02.19 Сварочное производство')
  assert.equal(group.getCell('B10').value, 'ОУП.08')
  assert.equal(group.getCell('C10').text, 'информатика(т)')
  assert.equal(group.getCell('F10').value.formula, 'SUM(J10:K10)')
  assert.equal(group.getCell('G10').value.formula, 'D10-F10')
  assert.equal(group.getCell('G10').value.result, '0')

  const zeroResultZip = await JSZip.loadAsync(await writeReferenceAccountingBuffer(workbook))
  const zeroGroupXml = await zeroResultZip.file(`xl/worksheets/sheet${group.id}.xml`).async('string')
  assert.match(zeroGroupXml, /<c\b[^>]*r="G10"[^>]*><f>D10-F10<\/f><v>0<\/v><\/c>/)

  const dated = structuredClone(hours)
  dated.lessons[0].scheduled_occurrences = [{ date: '2026-09-03', week_index: 1, slot: 1, hours: 2 }]
  const datedWorkbook = buildReferenceAccountingWorkbook(dated).workbook
  const datedGroup = datedWorkbook.getWorksheet('СП-Пф-1605')
  const dateSheet = datedWorkbook.getWorksheet('Даты занятий')
  assert.deepEqual(datedWorkbook.worksheets.map(sheet => sheet.name), ['Расписание', 'пр', 'СП-Пф-1605', 'Даты занятий'])
  assert.equal(dateSheet.getCell('D2').value.toISOString(), '2026-09-03T12:00:00.000Z')
  assert.match(datedGroup.getCell('C10').hyperlink, /Даты занятий/)
  assert.equal(datedGroup.getCell('J6').value.result.toISOString(), '2026-09-02T12:00:00.000Z')

  const restored = new ExcelJS.Workbook()
  await restored.xlsx.load(await writeReferenceAccountingBuffer(datedWorkbook))
  assert.equal(restored.getWorksheet('Даты занятий').getCell('D2').value.toISOString(), '2026-09-03T12:00:00.000Z')
  assert.equal(restored.getWorksheet('СП-Пф-1605').getCell('J6').value.result.toISOString(), '2026-09-02T12:00:00.000Z')
  assert.equal(restored.getWorksheet('СП-Пф-1605').getCell('G10').value.result, 8)
})

test('reference export follows workload tab and row order instead of IDs or alphabetical indexes', () => {
  const ordered = {
    ...hours,
    teachers: [],
    groups: [
      { group_id: 9, group_name: 'G2', accounting_title: 'G2', accounting_order: 1 },
      { group_id: 1, group_name: 'G1', accounting_title: 'G1', accounting_order: 0 },
    ],
    lessons: [
      { lesson_id: 92, group_id: 9, group_name: 'G2', teacher_id: 0, teacher_name: 'Teacher', source_index: 'A.01', subject_id: 2, subgroup: -1, name: 'Later row', planned_hours: 2, scheduled_occurrences: [], workload_order: 20 },
      { lesson_id: 91, group_id: 9, group_name: 'G2', teacher_id: 0, teacher_name: 'Teacher', source_index: 'Z.01', subject_id: 1, subgroup: -1, name: 'Earlier row', planned_hours: 2, scheduled_occurrences: [], workload_order: 10 },
      { lesson_id: 11, group_id: 1, group_name: 'G1', teacher_id: 0, teacher_name: 'Teacher', source_index: 'A.01', subject_id: 1, subgroup: -1, name: 'Only row', planned_hours: 2, scheduled_occurrences: [], workload_order: 10 },
    ],
  }
  const { workbook } = buildReferenceAccountingWorkbook(ordered)
  assert.deepEqual(workbook.worksheets.map(sheet => sheet.name), ['Расписание', 'пр', 'G1', 'G2', 'Даты занятий'])
  assert.equal(workbook.getWorksheet('G2').getCell('C10').value.text, 'Earlier row')
  assert.equal(workbook.getWorksheet('G2').getCell('C11').value.text, 'Later row')
})

test('teacher weekly load uses remaining semester hours divided by global remaining weeks', () => {
  const teacher = { ...hours.teachers[0], required_weekly_hours: 24, remaining_weeks: 2 }
  assert.equal(remainingTeachingWeeks(hours), 1)
  assert.equal(referenceTeacherLoad(teacher, hours), 48)
  const untouched = { ...hours, lessons: [] }
  assert.equal(referenceTeacherLoad(teacher, untouched), 24)
})

test('weekly group averages use calendar weeks when the group has an empty work period', () => {
  const datedHours = {
    as_of_date: '2026-09-08', semester_start: '2026-09-01', semester_end: '2026-09-21',
    weeks: [
      { index: 1, from: '2026-09-01', to: '2026-09-07' },
      { index: 2, from: '2026-09-08', to: '2026-09-14' },
      { index: 3, from: '2026-09-15', to: '2026-09-21' },
    ],
    teachers: [{ teacher_id: 7, teacher_name: 'Иванова', planned_hours: 100, scheduled_hours: 20,
      credited_hours: 20, remaining_hours: 80, adjustment_hours: 0, weekly_hours: [20, 0] }],
    groups: [{ group_id: 4, group_name: 'ГР-104', accounting_title: 'ГР-104',
      work_period: { from: '', to: '' },
      work_days: [1, 2, 3, 4, 5, 6, 7].map(day => ({ day, enabled: day !== 7 })),
      academic_calendar: [
        { from: '2026-09-01', to: '2026-09-07', theory_hours: 24, pp_hours: 0, vacation: false },
        { from: '2026-09-08', to: '2026-09-14', theory_hours: 24, pp_hours: 0, vacation: false },
      ] }],
    lessons: [{ lesson_id: 11, group_id: 4, group_name: 'ГР-104', teacher_id: 7,
      teacher_name: 'Иванова', source_index: '1', subject_id: 11, subgroup: -1, name: 'Предмет',
      planned_hours: 100, scheduled_hours: 20, scheduled_occurrences: [
        ...Array.from({ length: 10 }, (_, index) => ({ date: '2026-09-04', week_index: 1, hours: 2 })),
        ...Array.from({ length: 20 }, (_, index) => ({ date: '2026-09-10', week_index: 2, hours: 2 })),
      ] }],
  }
  const metrics = referenceGroupWeeks(datedHours.groups[0], datedHours)
  assert.deepEqual(metrics.weights, [1, 1, 0])
  near(metrics.accountedWeights[0], 1)
  near(metrics.accountedWeights[1], 1 / 6)
  near(metrics.total, 2)
  near(metrics.accounted, 7 / 6)
  near(metrics.remaining, 5 / 6)
  near(referenceTeacherLoad(datedHours.teachers[0], datedHours), 80)

  const { workbook } = buildReferenceAccountingWorkbook(datedHours)
  const group = workbook.worksheets[2]
  const summary = workbook.worksheets[1]
  assert.equal(group.getCell('F10').value.result, 20)
  assert.equal(group.getCell('H10').value.result, 50)
  near(group.getCell('I10').value.result, 96)
  near(summary.getCell('F3').value.result, 80)
  near(summary.getCell('G3').value.result, 80)
})

test('teacher weekly load ignores different group deadlines and uses the shared semester denominator', () => {
  const data = {
    as_of_date: '2026-09-08', semester_start: '2026-09-01', semester_end: '2026-09-21',
    weeks: [
      { index: 1, from: '2026-09-01', to: '2026-09-07' },
      { index: 2, from: '2026-09-08', to: '2026-09-14' },
      { index: 3, from: '2026-09-15', to: '2026-09-21' },
    ],
    teachers: [{ teacher_id: 7, planned_hours: 100, credited_hours: 20, remaining_hours: 80 }],
    groups: [
      { group_id: 1, group_name: 'G1', teaching_deadline: '2026-09-07' },
      { group_id: 2, group_name: 'G2', teaching_deadline: '2026-09-14' },
    ],
    lessons: [
      { group_id: 1, teacher_id: 7, planned_hours: 20, scheduled_hours: 20,
        scheduled_occurrences: [{ date: '2026-09-07', week_index: 1, hours: 20 }] },
      { group_id: 2, teacher_id: 7, planned_hours: 80, scheduled_hours: 0, scheduled_occurrences: [] },
    ],
  }
  assert.equal(referenceGroupWeeks(data.groups[0], data).remaining, 0)
  near(referenceGroupWeeks(data.groups[1], data).remaining, 5 / 6)
  near(referenceTeacherLoad(data.teachers[0], data), 80)
})

test('a 544-hour semester balance over 9 complete future weeks is about 60.44 hours per week', async () => {
  const weeks = Array.from({ length: 17 }, (_, index) => {
    const from = new Date(Date.UTC(2026, 8, 1 + index * 7))
    const to = new Date(from)
    to.setUTCDate(to.getUTCDate() + 6)
    if (index === 16) to.setUTCDate(to.getUTCDate() - 2)
    return { index: index + 1, from: from.toISOString().slice(0, 10), to: to.toISOString().slice(0, 10) }
  })
  const data = {
    as_of_date: '2026-10-15', semester_start: '2026-09-01', semester_end: '2026-12-26', weeks,
    teachers: [{ teacher_id: 42, teacher_name: 'Вальдиянов Ян Мансурович', planned_hours: 1000,
      credited_hours: 456, remaining_hours: 544, adjustment_hours: 0 }],
    groups: [{ group_id: 10, group_name: 'ГР-104' }],
    lessons: [{ group_id: 10, teacher_id: 42, teacher_name: 'Вальдиянов Ян Мансурович',
      planned_hours: 1000, scheduled_hours: 456,
      scheduled_occurrences: Array.from({ length: 228 }, () => ({ date: '2026-10-14', week_index: 6, hours: 2 })) }],
  }

  const { workbook, remainingWeeks } = buildReferenceAccountingWorkbook(data)
  const summary = workbook.getWorksheet('пр')
  assert.equal(remainingWeeks, 9)
  near(referenceTeacherLoad(data.teachers[0], data), 544 / 9)
  near(summary.getCell('F3').value.result, 544 / 9)
  assert.match(summary.getCell('F3').value.formula, /MAX\(0,E3\)\/\$Z\$2/)

  const reloaded = new ExcelJS.Workbook()
  await reloaded.xlsx.load(await workbook.xlsx.writeBuffer())
  const savedCell = reloaded.getWorksheet('пр').getCell('F3').value
  assert.match(savedCell.formula, /MAX\(0,E3\)\/\$Z\$2/)
  near(savedCell.result, 544 / 9)
})

test('teacher hover notes show confirmed conducted dates and survive XLSX export', async () => {
  const data = structuredClone(hours)
  data.as_of_date = '2026-09-08'
  data.teachers[0].credited_occurrences = [
    { date: '2026-09-03', hours: 2 },
    { date: '2026-09-03', hours: 4 },
    { date: '2026-09-09', hours: 2 },
    { date: '2026-09-04', hours: 2 },
    { date: '2026-09-05', hours: 2, lesson_name: 'ПП.01 Практика' },
  ]

  const note = referenceTeacherConductedNote(data.teachers[0], data)
  assert.match(note, /03\.09\.2026 — 6 ч/)
  assert.match(note, /04\.09\.2026/)
  assert.doesNotMatch(note, /09\.09\.2026/)
  assert.doesNotMatch(note, /05\.09\.2026/)

  const { workbook } = buildReferenceAccountingWorkbook(data)
  assert.match(workbook.getWorksheet('пр').getCell('A3').note, /03\.09\.2026 — 6 ч/)
  const reloaded = new ExcelJS.Workbook()
  await reloaded.xlsx.load(await workbook.xlsx.writeBuffer())
  const exportedNote = reloaded.getWorksheet('пр').getCell('A3').note
  assert.ok(exportedNote)
  assert.match(typeof exportedNote === 'string' ? exportedNote : JSON.stringify(exportedNote), /03\.09\.2026/)
})

test('remaining group weeks are anchored to the report date, not the last lesson record', () => {
  const data = {
    as_of_date: '2026-09-10', semester_start: '2026-09-01', semester_end: '2026-09-21',
    weeks: [
      { index: 1, from: '2026-09-01', to: '2026-09-07' },
      { index: 2, from: '2026-09-08', to: '2026-09-14' },
      { index: 3, from: '2026-09-15', to: '2026-09-21' },
    ],
    teachers: [{ teacher_id: 7, teacher_name: 'Иванова', planned_hours: 100, credited_hours: 20, remaining_hours: 80 }],
    groups: [{ group_id: 4, group_name: 'ГР-104', academic_calendar: [
      { from: '2026-09-01', to: '2026-09-07', theory_hours: 24, pp_hours: 0, vacation: false },
      { from: '2026-09-08', to: '2026-09-14', theory_hours: 24, pp_hours: 0, vacation: false },
      { from: '2026-09-15', to: '2026-09-21', theory_hours: 24, pp_hours: 0, vacation: false },
    ] }],
    lessons: [{ group_id: 4, teacher_id: 7, planned_hours: 100, scheduled_hours: 20,
      scheduled_occurrences: [{ date: '2026-09-04', week_index: 1, hours: 20 }] }],
  }
  const metrics = referenceGroupWeeks(data.groups[0], data)
  assert.deepEqual(metrics.weights, [1, 1, 1])
  assert.deepEqual(metrics.accountedWeights, [1, 0.5, 0])
  near(metrics.remaining, 1.5)
  near(referenceTeacherLoad(data.teachers[0], data), 80)
})

test('teacher average weekly load is zero after all 114 planned hours are issued', () => {
  const completed = structuredClone(hours)
  completed.teachers[0] = { ...completed.teachers[0], planned_hours: 114, scheduled_hours: 114,
    credited_hours: 114, remaining_hours: 0 }
  completed.lessons = [{ ...completed.lessons[0], planned_hours: 114, scheduled_hours: 114,
    scheduled_occurrences: [{ week_index: 1, hours: 114 }] }]

  const { workbook } = buildReferenceAccountingWorkbook(completed)
  const summary = workbook.getWorksheet('пр')
  assert.equal(summary.getCell('F1').value, 'ср. недельная нагрузка по остатку за семестр')
  assert.match(summary.getCell('F3').value.formula, /MAX\(0,E3\)\/\$[A-Z]+\$2/)
  assert.equal(referenceTeacherLoad(completed.teachers[0], completed), 0)
})

test('remaining weekly average never goes negative when delivered hours exceed a teacher plan', () => {
  const overrun = {
    as_of_date: '2026-09-08', semester_start: '2026-09-01', semester_end: '2026-09-21',
    weeks: [{ index: 1, from: '2026-09-01', to: '2026-09-07' }, { index: 2, from: '2026-09-08', to: '2026-09-14' }, { index: 3, from: '2026-09-15', to: '2026-09-21' }],
    teachers: [{ teacher_id: 8, teacher_name: 'Кальчевская', planned_hours: 0, credited_hours: 6, remaining_hours: -6 }],
    groups: [{ group_id: 9, group_name: 'ГР-104', academic_calendar: [
      { from: '2026-09-01', to: '2026-09-07', theory_hours: 24, pp_hours: 0, vacation: false },
      { from: '2026-09-08', to: '2026-09-14', theory_hours: 24, pp_hours: 0, vacation: false },
      { from: '2026-09-15', to: '2026-09-21', theory_hours: 24, pp_hours: 0, vacation: false },
    ] }],
    lessons: [{ group_id: 9, teacher_id: 8, teacher_name: 'Кальчевская', planned_hours: 0, scheduled_hours: 6,
      scheduled_occurrences: [{ date: '2026-09-07', week_index: 1, hours: 6 }] }],
  }
  const { workbook } = buildReferenceAccountingWorkbook(overrun)
  assert.match(workbook.getWorksheet('пр').getCell('F3').value.formula, /MAX\(0,E3\)\/\$[A-Z]+\$2/)
  assert.match(workbook.worksheets[2].getCell('I10').value.formula, /MAX\(0,G10\)/)
  assert.equal(referenceTeacherLoad(overrun.teachers[0], overrun), 0)
})

test('remaining balance with no future group weeks is reported instead of silently averaged away', () => {
  const expired = {
    as_of_date: '2026-09-15', semester_start: '2026-09-01', semester_end: '2026-09-14',
    weeks: [{ index: 1, from: '2026-09-01', to: '2026-09-07' }, { index: 2, from: '2026-09-08', to: '2026-09-14' }],
    teachers: [{ teacher_id: 8, teacher_name: 'Преподаватель', planned_hours: 20, credited_hours: 10, remaining_hours: 10 }],
    groups: [{ group_id: 9, group_name: 'ГР-104', academic_calendar: [
      { from: '2026-09-01', to: '2026-09-07', theory_hours: 24, pp_hours: 0, vacation: false },
      { from: '2026-09-08', to: '2026-09-14', theory_hours: 24, pp_hours: 0, vacation: false },
    ] }],
    lessons: [{ group_id: 9, teacher_id: 8, teacher_name: 'Преподаватель', planned_hours: 20, scheduled_hours: 10,
      scheduled_occurrences: [{ date: '2026-09-07', week_index: 1, hours: 10 }] }],
  }
  const { workbook } = buildReferenceAccountingWorkbook(expired)
  assert.equal(workbook.getWorksheet('пр').getCell('F3').value.result, 'нет недель')
  assert.equal(referenceTeacherLoad(expired.teachers[0], expired), null)
})

test('transferred-to-PP teacher buckets and lessons are omitted from the workbook', () => {
  const transferred = structuredClone(hours)
  transferred.teachers.push({ teacher_id: 88, teacher_name: 'вынесена на ПП', planned_hours: 756,
    credited_hours: 0, remaining_hours: 756 })
  transferred.lessons.push({ lesson_id: 99, group_id: 4, teacher_id: 88, teacher_name: 'вынесена на ПП',
    source_index: 'УП.02', name: 'УП.02', planned_hours: 756, scheduled_hours: 0, scheduled_occurrences: [] })

  const { workbook } = buildReferenceAccountingWorkbook(transferred)
  const summary = workbook.getWorksheet('пр')
  const group = workbook.getWorksheet('СП-Пф-1605')
  assert.equal(summary.getCell('A4').value, null)
  assert.equal(group.getCell('B13').value, null)
})

test('teacher weekly formulas stay within Excel formula-length limits across many groups', () => {
  const groups = Array.from({ length: 46 }, (_, group_id) => ({ group_id, group_name: `Группа-${group_id + 1}` }))
  const lessons = groups.map(group => ({ group_id: group.group_id, teacher_id: 7, teacher_name: 'Иванова',
    planned_hours: 2, scheduled_hours: 0, scheduled_occurrences: [] }))
  const { workbook } = buildReferenceAccountingWorkbook({
    semester_start: '2026-09-01',
    weeks: [{ index: 1, from: '2026-09-01', to: '2026-09-07' }, { index: 2, from: '2026-09-08', to: '2026-09-14' }],
    teachers: [{ teacher_id: 7, teacher_name: 'Иванова', planned_hours: 92, credited_hours: 0, remaining_hours: 92 }],
    groups, lessons,
  })
  const formula = workbook.getWorksheet('пр').getCell('F3').value.formula
  assert.ok(formula.length <= 8192, `Excel formula is ${formula.length} characters`)
  assert.match(formula, /MAX\(0,E3\)\/\$[A-Z]+\$2/)
})
