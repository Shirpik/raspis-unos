import test from 'node:test'
import assert from 'node:assert/strict'
import {replacementPeriodsError} from '../src/utils/teacherReplacements.js'
import {roomFormFromEntity,roomPayloadFromForm} from '../src/utils/entityPayloads.js'
test('replacement periods reject overlap, self replacement and unknown teacher',()=>{
  const teachers=[{id:0},{id:1}]
  const period={from:'2026-11-09',to:'2026-11-10',teacher:1}
  assert.equal(replacementPeriodsError([period],0,teachers),'')
  assert.ok(replacementPeriodsError([period,{...period,from:period.to}],0,teachers))
  assert.ok(replacementPeriodsError([{...period,teacher:0}],0,teachers))
  assert.ok(replacementPeriodsError([{...period,teacher:2}],0,teachers))
  assert.ok(replacementPeriodsError([{...period,to:'2026-11-01'}],0,teachers))
})
test('room editing preserves closed days, exact dates and unrelated room restrictions',()=>{
  const room={id:5,name:'101',parallel_classes:2,available_slots:[1,3],work_days:[{day:1,enabled:false,slots:[]}],date_slot_overrides:[{date:'2026-11-10',slots:[]}],week_schedule_overrides:[{from:'2026-11-09'}],responsible_teacher_ids:[1]}
  const form=roomFormFromEntity(room)
  form.name='102'
  assert.deepEqual(roomPayloadFromForm(form),{...room,equipment:[],name:'102',campus:0,room_type:0,capacity:0,active:true,purpose:'',access_mode:'general',responsible_note:'',description:'',work_period:{from:'',to:''}})
  form.work_days[0].slots.push(2)
  assert.deepEqual(room.work_days[0].slots,[])
})
