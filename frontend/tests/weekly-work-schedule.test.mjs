import test from 'node:test'
import assert from 'node:assert/strict'
import {weekStart,weekDates,ordinaryWorkDays,weekWorkDays,setWeekSchedule} from '../src/utils/weeklyWorkSchedule.js'
import {teacherFormFromEntity,teacherPayloadFromForm} from '../src/utils/entityPayloads.js'

test('week selection uses Monday through Sunday across month and year boundaries',()=>{
 assert.equal(weekStart('2026-10-22'),'2026-10-19')
 assert.equal(weekStart('2026-10-25'),'2026-10-19')
 assert.deepEqual(weekDates('2027-01-01'),['2026-12-28','2026-12-29','2026-12-30','2026-12-31','2027-01-01','2027-01-02','2027-01-03'])
 assert.equal(weekStart(''),'')
})

test('no week rule inherits ordinary exact tiles; an empty explicit day closes it',()=>{
 const teacher={work_days:[{day:5,enabled:true,slots:[2,4],start_slot:2,end_slot:4}]}
 const ordinary=ordinaryWorkDays(teacher)
 assert.deepEqual(weekWorkDays(teacher,null),ordinary)
 assert.deepEqual(ordinary[4].slots,[2,4])
 const days=weekWorkDays(teacher,{work_days:[{day:5,slots:[]}]})
 assert.deepEqual(days[4].slots,[])
 assert.equal(days[4].enabled,false)
 assert.deepEqual(days[3],ordinary[3])
 assert.deepEqual(teacher.work_days[0].slots,[2,4])
})

test('week edits and resets preserve other weeks and exact-date exceptions',()=>{
 const original={name:'Teacher',work_days:ordinaryWorkDays(),date_slot_overrides:[{date:'2026-10-23',slots:[2]}],week_schedule_overrides:[{week_from:'2026-10-12',work_days:[{day:1,slots:[]}]}]}
 const days=ordinaryWorkDays(original).map(d=>({...d,enabled:false,slots:[]}))
 const entries=setWeekSchedule(original.week_schedule_overrides,'2026-10-22',days)
 assert.equal(entries.length,2)
 assert(entries[1].work_days.every(d=>d.slots.length===0))
 const edited=teacherPayloadFromForm(teacherFormFromEntity({...original,week_schedule_overrides:entries}))
 assert.deepEqual(edited.work_days,original.work_days)
 assert.deepEqual(edited.date_slot_overrides,original.date_slot_overrides)
 assert.deepEqual(edited.week_schedule_overrides,entries)
 assert.deepEqual(setWeekSchedule(entries,'2026-10-24',null),original.week_schedule_overrides)
 assert.equal(original.week_schedule_overrides.length,1)
})
