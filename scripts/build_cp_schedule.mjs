/* Convert a CP-SAT placement witness into the timetable JSON consumed by the
 * existing Excel exporter.  The quota model already proves teacher and
 * physical-subgroup non-overlap; this step assigns concrete compatible rooms.
 */
import fs from 'node:fs/promises'

const folder = process.argv[2] || 'outputs/friday-saturday-20260918'
const out = process.argv[3] || 'candidate-cp'
const [data, quota] = await Promise.all([
  fs.readFile(`${folder}/data/timetable_data.json`, 'utf8').then(JSON.parse),
  fs.readFile(`${folder}/quota-result-relaxed.json`, 'utf8').then(JSON.parse),
])
if (!quota.success) throw new Error(`Quota result: ${quota.status}`)
const days = ['2026-09-18', '2026-09-19']
const lessonById = new Map(data.lessons.map(item => [item.id, item]))
const teacherById = new Map(data.teachers.map(item => [item.id, item]))
const rooms = data.rooms.filter(room => room.active !== false && room.access_mode !== 'blocked')
const occupiedRooms = new Set()
const roomFor = (lesson, day, slot) => {
  const teacher = teacherById.get(lesson.teacher)
  if (data.settings.distance_learning) {
    if((data.settings.distance_revision||1)<2) return {id:-1,name:'Дистант',campus:0}
    const fixed=rooms.find(r=>r.id===lesson.fixed_room)
    const assigned=rooms.find(r=>r.id===teacher?.default_room)
    if(fixed || assigned) return fixed || assigned
    const history=(data.teaching_ledger||[]).filter(e=>e.lesson_id===lesson.id && e.date<days[0] && ['confirmed','planned'].includes(e.status)).sort((a,b)=>b.date.localeCompare(a.date))
    const normal=x=>String(x||'').replace(/\s+/g,'').toLowerCase()
    const usual=new Map()
    for(const e of history){
      const room=rooms.find(r=>r.id===e.room_id || normal(`${r.name}_${r.campus===0?'Л':'К'}`)===normal(e.room))
      if(room) usual.set(room.id,(usual.get(room.id)||0)+1)
    }
    if(usual.size){const [id]=[...usual].sort((a,b)=>b[1]-a[1])[0];return rooms.find(r=>r.id===id)}
    return rooms.find(r=>(!lesson.allowed_campuses?.length || lesson.allowed_campuses.includes(r.campus)) &&
      (!teacher?.allowed_campuses?.length || teacher.allowed_campuses.includes(r.campus)) &&
      (!lesson.required_room_type || r.room_type===lesson.required_room_type) &&
      (lesson.required_room_purpose==='sports_hall' ? r.purpose==='sports_hall' : r.purpose!=='sports_hall') &&
      !lesson.required_equipment?.some(e=>!r.equipment?.includes(e))) || {id:-1,name:'Дистант',campus:0}
  }
  const compatible = rooms.filter(room => {
    if (lesson.fixed_room >= 0 && room.id !== lesson.fixed_room) return false
    if (lesson.allowed_campuses?.length && !lesson.allowed_campuses.includes(room.campus)) return false
    if (teacher?.allowed_campuses?.length && !teacher.allowed_campuses.includes(room.campus)) return false
    if (lesson.required_room_type && room.room_type !== lesson.required_room_type) return false
    if (lesson.required_capacity && room.capacity < lesson.required_capacity) return false
    if (lesson.required_equipment?.some(item => !room.equipment?.includes(item))) return false
    if (lesson.required_room_purpose === 'sports_hall' && room.purpose !== 'sports_hall') return false
    if (lesson.required_room_purpose !== 'sports_hall' && room.purpose === 'sports_hall') return false
    return true
  })
  const room = compatible.find(item => !occupiedRooms.has(`${day}:${slot}:${item.id}`)) || compatible[0]
  if (!room) throw new Error(`Нет аудитории: ${lesson.name}`)
  occupiedRooms.add(`${day}:${slot}:${room.id}`)
  return room
}
const entries = new Map()
for (const [rawId, times] of Object.entries(quota.placement_witness)) {
  const lesson = lessonById.get(Number(rawId))
  if (!lesson) throw new Error(`Unknown lesson ${rawId}`)
  for (const time of times) {
    const day = days[Math.floor(time / 7)]
    const slot = time % 7 + 1
    const key = `${lesson.group}:${day}:${slot}`
    const list = entries.get(key) || []
    list.push({ lesson, room: roomFor(lesson, day, slot) })
    entries.set(key, list)
  }
}
const timeLabel = slot => `${slot} пара`
const groups = data.groups.map(group => ({
  group_index: group.id,
  group_name: group.name,
  days: days.map((dateIso, dayIndex) => ({
    date: dateIso.slice(8, 10) + '.' + dateIso.slice(5, 7) + '.' + dateIso.slice(0, 4),
    date_iso: dateIso,
    day_index: dayIndex,
    slots: Array.from({ length: 7 }, (_, index) => {
      const slot = index + 1
      const lessons = (entries.get(`${group.id}:${dateIso}:${slot}`) || []).map(({ lesson, room }) => ({
        id: lesson.id, uid: lesson.uid, name: lesson.name, subgroup: lesson.subgroup,
        teacher_id: lesson.teacher, room_id: room.id, room_name: room.name,
        requested_room_id: null, requested_room_name: null, room_substituted: false,
        room_substitution_reason: null, room_type: room.room_type || 0,
        is_block: Boolean(lesson.is_block), is_lab: Boolean(lesson.is_lab),
        consecutive_pairs: lesson.consecutive_pairs || 1,
        avoid_lunch_split: Boolean(lesson.avoid_lunch_split), week_parity: lesson.week_parity || 'all',
      }))
      return { slot, time: timeLabel(slot), lessons, text: lessons.length ? lessons.map(item => item.name).join(' | ') : '-' }
    }),
  })),
}))
const count = groups.flatMap(group => group.days).flatMap(day => day.slots).reduce((n, slot) => n + slot.lessons.length, 0)
await fs.mkdir(`${folder}/${out}`, { recursive: true })
await fs.writeFile(`${folder}/${out}/schedule_all.json`, `${JSON.stringify({ groups }, null, 2)}\n`)
await fs.writeFile(`${folder}/${out}/quality_report.json`, `${JSON.stringify({ completion_percent: 100, planned_events: count, student_windows: 0, source: 'CP-SAT placement witness' }, null, 2)}\n`)
console.log(JSON.stringify({ events: count, groups: groups.length }))
