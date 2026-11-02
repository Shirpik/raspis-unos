import { subgroupOrdinal } from './schedulePresentation.js'

const day = value => new Date(`${value}T12:00:00Z`)
const iso = value => value.toISOString().slice(0, 10)
const finite = value => Number.isFinite(Number(value)) ? Number(value) : 0
const limitNote = text => text.length <= 32000 ? text : `${text.slice(0, 31970)}\n… список сокращён`
const accountingCutoff = hours => hours.as_of_date || hours.accounting_imported_through || ''
const accounted = (item, hours) => item.status !== 'planned' && (!accountingCutoff(hours) || !item.date || item.date <= accountingCutoff(hours))
const isProductionPractice = lesson => Boolean(lesson.is_pp) || [lesson.source_index, lesson.name].some(value => {
  const normalized = String(value || '').trim().toLocaleLowerCase('ru')
  return normalized.startsWith('пп.') || normalized.startsWith('пп ')
})
const isTransferredToPpTeacher = name => {
  const normalized = String(name || '').trim().toLocaleLowerCase('ru').replaceAll('ё', 'е')
  return normalized.includes('вынес') && normalized.includes('пп')
}
export function withReferenceContext(hours, data = {}) {
  const sourceLessons=new Map((data.lessons||[]).map(lesson=>[Number(lesson.id),lesson]))
  const lessons=(hours.lessons||[]).map(lesson=>({
    workload_retired:sourceLessons.get(Number(lesson.lesson_id))?.workload_retired,...lesson,
  })).filter(lesson=>!(Number.isInteger(lesson.workload_retired?.canonical_lesson_id) &&
    !finite(lesson.planned_hours) && !finite(lesson.scheduled_hours) && !finite(lesson.credited_hours) && !finite(lesson.projected_hours)))
  return { ...hours, lessons, settings: data.settings || hours.settings || {}, groups: (hours.groups || []).map(group => ({
    ...(data.groups || []).find(item => Number(item.id) === Number(group.group_id)), ...group,
  })) }
}
export function latestAccountedWeek(hours) {
  return (hours.lessons || []).reduce((last, lesson) => (lesson.scheduled_occurrences || [])
    .filter(item => accounted(item, hours))
    .reduce((value, item) => Math.max(value, finite(item.week_index)), last), 0)
}
export function referenceGroupWeeks(group, hours) {
  const weeks = hours.weeks || [], calendar = group.academic_calendar || [], settings = hours.settings || {}
  const course = Number(group.course_year) || Number(String(group.group_name || group.name || '').match(/-(\d)/)?.[1])
  const semesterEnd=hours.semester_end || settings.semester_end_date || '9999-12-31'
  const graphEnd=calendar.filter(p=>p.from<=semesterEnd && p.to>=hours.semester_start && !p.vacation &&
    finite(p.theory_hours)+finite(p.up_hours)>0).reduce((last,p)=>{
      const end=p.to<semesterEnd?p.to:semesterEnd
      return end>last?end:last
    },'')
  const accountingEnd=group.accounting_teaching_end_date
  const deadline = (accountingEnd && accountingEnd>=hours.semester_start && accountingEnd<=semesterEnd ? accountingEnd : '') || graphEnd || group.teaching_deadline || (course === 1 ? settings.first_course_semester_end_date : '') || settings.semester_end_date || semesterEnd
  const cutoff = accountingCutoff(hours)
  const latest = cutoff ? 0 : latestAccountedWeek(hours)
  const weights = [], accountedWeights = [], dailyWeights = []
  for (const week of weeks) {
    let weight = 0
    let remainingWeight = 0
    const daily = []
    for (let date = day(week.from); iso(date) <= week.to; date.setUTCDate(date.getUTCDate()+1)) {
      const value = iso(date), weekday = date.getUTCDay() || 7
      if (weekday === 7 || value > deadline || value < hours.semester_start) continue
      const period = group.work_period || {}
      if ((period.from && value < period.from) || (period.to && value > period.to)) continue
      if (group.work_days?.length && !group.work_days.some(rule => Number(rule.day) === weekday && rule.enabled !== false)) continue
      if ((group.practice_periods || []).some(p => p.kind === 'industrial' && p.from <= value && value <= p.to)) continue
      const entry = calendar.find(p => p.from <= value && value <= p.to)
      if (calendar.length && (!entry || entry.vacation || !(finite(entry.theory_hours)+finite(entry.up_hours)))) continue
      let dailyWeight = 1/6
      if (entry) {
        // The academic graph assigns hours to the first teaching week even
        // when the semester starts on Tuesday. Do not lose that Monday twice.
        let days = 0
        for (let d=day(entry.from); iso(d)<=entry.to; d.setUTCDate(d.getUTCDate()+1)) {
          if (d.getUTCDay() && iso(d)>=hours.semester_start && iso(d)<=(hours.semester_end || deadline)) days++
        }
        dailyWeight = days ? (finite(entry.theory_hours)+finite(entry.up_hours))/36/days : 0
      }
      weight += dailyWeight
      daily.push({date:value,weight:dailyWeight})
      if (cutoff && value > cutoff) remainingWeight += dailyWeight
    }
    weights.push(weight)
    dailyWeights.push(daily)
    if (!cutoff) remainingWeight = Number(week.index) > latest ? weight : 0
    accountedWeights.push(Math.max(0, weight-remainingWeight))
  }
  const total = weights.reduce((sum,weight)=>sum+weight,0)
  const countedWeeks = accountedWeights.reduce((sum,weight)=>sum+weight,0)
  return { weights, accountedWeights, dailyWeights, total, accounted: countedWeeks, remaining: Math.max(0,total-countedWeeks) }
}
export function remainingTeachingWeeks(hours) {
  const weeks = hours.weeks || [], cutoff = accountingCutoff(hours)
  const isCompleteWeek = week => {
    const start = day(week.from), end = day(week.to)
    return end-start === 6 * 24 * 60 * 60 * 1000 ||
      start.getUTCDay() === 1 && end.getUTCDay() === 6 && end-start === 5 * 24 * 60 * 60 * 1000
  }
  if (cutoff) return weeks.filter(week => week.from > cutoff && isCompleteWeek(week)).length
  const latest = latestAccountedWeek(hours)
  return weeks.filter(week => Number(week.index) > latest && isCompleteWeek(week)).length
}
export function referenceTeacherLoad(teacher, hours) {
  if (isTransferredToPpTeacher(teacher.teacher_name)) return 0
  const remaining = finite(teacher.remaining_hours ?? (finite(teacher.planned_hours)-finite(teacher.credited_hours ?? teacher.scheduled_hours)))
  const weeks = referenceSummaryWeeks(hours)
  return weeks > 0 ? remaining/weeks : remaining === 0 ? 0 : null
}
export function referenceSummaryWeeks(hours) {
  const first=[...(hours.groups||[])].sort((a,b)=>finite(a.accounting_order??a.group_id)-finite(b.accounting_order??b.group_id)||finite(a.group_id)-finite(b.group_id))[0]
  return first?referenceGroupWeeks(first,hours).remaining:remainingTeachingWeeks(hours)
}
export function referenceTeacherConductedNote(teacher, hours) {
  const cutoff = accountingCutoff(hours)
  const lessons = new Map((hours.lessons || []).map(lesson => [Number(lesson.lesson_id), lesson]))
  const groups = new Map((hours.groups || []).map(group => [Number(group.group_id), group]))
  const events = []
  for (const item of teacher.credited_occurrences || []) {
    const lesson = lessons.get(Number(item.lesson_id))
    const details = { ...lesson, ...item, name: item.lesson_name || lesson?.name }
    if (!item.date || cutoff && item.date > cutoff || item.is_class_hour || lesson?.is_class_hour ||
        isProductionPractice(details)) continue
    const groupId = Number(item.group_id ?? lesson?.group_id)
    const group = groups.get(groupId)
    const groupName = item.group_name || lesson?.group_name || group?.group_name || group?.name || '\u0413\u0440\u0443\u043f\u043f\u0430 \u043d\u0435 \u0443\u043a\u0430\u0437\u0430\u043d\u0430'
    const ordinal = subgroupOrdinal(details.subgroup, groupId)
    const hasSubgroups = (hours.lessons || []).some(other =>
      Number(other.group_id) === groupId &&
      Number(other.subject_id) === Number(lesson?.subject_id) && Number(other.subgroup ?? -1) >= 0)
    const pair = finite(item.slot ?? item.pair_number ?? item.pair ?? item.lesson_number)
    const pairLabel = pair > 0 ? `${pair} \u043f\u0430\u0440\u0430` : '\u043d\u043e\u043c\u0435\u0440 \u043f\u0430\u0440\u044b \u043d\u0435 \u0443\u043a\u0430\u0437\u0430\u043d'
    const subgroup = ordinal ? ` · ${ordinal} подгруппа` : hasSubgroups ? ' · вся группа' : ''
    const substitute = item.is_substitution && item.teacher_name
      ? ` (\u0437\u0430\u043c\u0435\u043d\u0430 \u0432\u043c\u0435\u0441\u0442\u043e ${item.teacher_name})` : ''
    events.push({ date: item.date, pair, groupName, subgroup, pairLabel, substitute, hours: finite(item.hours ?? 2) })
  }
  if (!events.length) return '\u0414\u0430\u0442\u044b \u043f\u0440\u043e\u0432\u0435\u0434\u0435\u043d\u0438\u044f:\n\u041f\u043e\u0434\u0442\u0432\u0435\u0440\u0436\u0434\u0451\u043d\u043d\u044b\u0445 \u0437\u0430\u043d\u044f\u0442\u0438\u0439 \u043d\u0435\u0442.'
  const formatDate = value => value.split('-').reverse().join('.')
  events.sort((a, b) => a.date.localeCompare(b.date) || a.pair - b.pair ||
    a.groupName.localeCompare(b.groupName, 'ru'))
  const lines = events.map((item, index) =>
    `${index + 1}. ${formatDate(item.date)} — ${item.pairLabel} — ${item.groupName}${item.subgroup}${item.substitute}`)
  const totals = new Map()
  for (const item of events) totals.set(item.date, (totals.get(item.date) || 0) + item.hours)
  const dates = [...totals].map(([date, hours]) => `${formatDate(date)} — ${hours} ч`)
  return limitNote(`Даты проведения:\n${dates.join('\n')}\n\nЗанятия подтверждённого журнала:\n${lines.join('\n')}`)
}
export function referenceLessonConductedNote(row, subgroup = '') {
  const events = (row.occurrences || []).filter(item => item.date)
    .sort((a, b) => String(a.date).localeCompare(String(b.date)) ||
      finite(a.slot ?? a.pair_number ?? a.pair) - finite(b.slot ?? b.pair_number ?? b.pair))
  if (!events.length) return '\u0414\u0430\u0442\u044b \u043f\u0440\u043e\u0432\u0435\u0434\u0435\u043d\u0438\u044f:\n\u041f\u043e\u0434\u0442\u0432\u0435\u0440\u0436\u0434\u0451\u043d\u043d\u044b\u0445 \u0437\u0430\u043d\u044f\u0442\u0438\u0439 \u043d\u0435\u0442.'
  const lines = events.map((item, index) => {
    const date = String(item.date).split('-').reverse().join('.')
    const pair = finite(item.slot ?? item.pair_number ?? item.pair ?? item.lesson_number)
    const pairLabel = pair > 0 ? `${pair} \u043f\u0430\u0440\u0430` : '\u043d\u043e\u043c\u0435\u0440 \u043f\u0430\u0440\u044b \u043d\u0435 \u0443\u043a\u0430\u0437\u0430\u043d'
    return `${index + 1}. ${date} — ${pairLabel}`
  })
  return limitNote(`\u0414\u0430\u0442\u044b \u043f\u0440\u043e\u0432\u0435\u0434\u0435\u043d\u0438\u044f:\n${lines.join('\n')}`)
}
export function referenceLessonRows(hours, group) {
  const ppTeacherIds = new Set((hours.teachers || []).filter(item => isTransferredToPpTeacher(item.teacher_name)).map(item => Number(item.teacher_id)))
  return (hours.lessons || []).filter(lesson=>Number(lesson.group_id)===Number(group.group_id) &&
    !ppTeacherIds.has(Number(lesson.teacher_id)) && !isTransferredToPpTeacher(lesson.teacher_name) &&
    !isProductionPractice(lesson)).flatMap(lesson=>{
    // The source workbook counts issued hours only through the accounting date.
    // Future solver reservations remain visible in the site but do not inflate
    // "выдано" or consume the remaining teaching weeks in this report.
    const occurrences = (lesson.scheduled_occurrences || []).filter(item => accounted(item, hours))
    const original = occurrences.filter(item=>Number(item.credited_teacher_id ?? lesson.teacher_id)===Number(lesson.teacher_id))
    const base = { ...lesson, occurrences: original, issued: original.reduce((sum,item)=>sum+Number(item.hours??2),0) }
    if (!Array.isArray(lesson.scheduled_occurrences)) base.issued = finite(lesson.scheduled_hours)
    const replacements = new Map()
    for (const item of occurrences) {
      const teacher = Number(item.credited_teacher_id ?? lesson.teacher_id)
      if (teacher===Number(lesson.teacher_id)) continue
      if (!replacements.has(teacher)) replacements.set(teacher,{...lesson,planned_hours:0,teacher_id:teacher,
        teacher_name:item.credited_teacher_name ?? item.actual_teacher_name,occurrences:[],issued:0,replacement:true})
      const row=replacements.get(teacher); row.occurrences.push(item);row.issued+=Number(item.hours??2)
    }
    return [base,...replacements.values()]
  })
}
export function referenceWeeklyHours(row, weeks) {
  return weeks.map(week=>(row.occurrences || []).filter(item=>item.date
    ? week.from <= item.date && item.date <= week.to
    : Number(item.week_index)===Number(week.index))
    .reduce((sum,item)=>sum+Number(item.hours??2),0))
}
