const iso = date => `${date.getFullYear()}-${String(date.getMonth()+1).padStart(2,'0')}-${String(date.getDate()).padStart(2,'0')}`

export function weekStart(value) {
  const date = value instanceof Date ? new Date(value) : new Date(`${value}T12:00:00`)
  if (!Number.isFinite(date.getTime())) return ''
  date.setDate(date.getDate() - (date.getDay()+6)%7)
  return iso(date)
}

export function weekDates(value) {
  const start = weekStart(value)
  if (!start) return []
  return Array.from({length:7},(_,index)=>{
    const date = new Date(`${start}T12:00:00`)
    date.setDate(date.getDate()+index)
    return iso(date)
  })
}

export function ordinaryWorkDays(schedule={}) {
  return Array.from({length:7},(_,index)=>{
    const day=index+1, source=schedule.work_days?.find(item=>item.day===day)
    const enabled=source ? source.enabled!==false : day!==7
    const start=source?.start_slot||1, end=source?.end_slot||7
    const slots=enabled ? [...new Set((source?.slots ?? Array.from({length:end-start+1},(_,i)=>start+i)).map(Number).filter(n=>n>=1&&n<=7))].sort((a,b)=>a-b) : []
    return {day,enabled:slots.length>0,start_slot:slots[0]||1,end_slot:slots.at(-1)||7,slots}
  })
}

export function weekWorkDays(schedule, week) {
  return ordinaryWorkDays(schedule).map(day=>{
    const override=week?.work_days?.find(item=>item.day===day.day)
    if (!override) return day
    const slots=[...new Set(override.slots||[])].sort((a,b)=>a-b)
    return {...day,slots,enabled:slots.length>0,start_slot:slots[0]||1,end_slot:slots.at(-1)||7}
  })
}

export function setWeekSchedule(entries, from, workDays) {
  const start=weekStart(from)
  if (!start) return entries
  const rest=(entries||[]).filter(item=>item.week_from!==start)
  if (workDays) rest.push({week_from:start,work_days:workDays.map(day=>({day:day.day,slots:day.enabled===false?[]:[...new Set(day.slots||[])].sort((a,b)=>a-b)}))})
  return rest.sort((a,b)=>a.week_from.localeCompare(b.week_from))
}
