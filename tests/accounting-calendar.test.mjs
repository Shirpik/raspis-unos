import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import path from 'node:path'
import {spawnSync} from 'node:child_process'

test('hours report uses source school weeks and confirmed journal dates, including provenance',async()=>{
 const dir=await fs.mkdtemp(path.resolve('.tmp/accounting-calendar-'));await fs.mkdir(path.join(dir,'data'))
 const ledger=[['2026-09-05','confirmed',2],['2026-09-07','confirmed',6],['2026-09-12','confirmed',2],['2026-09-14','planned',2]].map(([date,status,hours],id)=>({id,lesson_id:0,group_id:0,date,status,hours,slot:1,source_file:'week.xlsx',source_sheet:'1 курс',source_cell:`D${id+1}`}))
 const data={settings:{semester_start_date:'2026-09-01',semester_end_date:'2026-09-19',accounting_as_of_date:'2026-09-05'},groups:[{id:0,name:'TEST-101',parts:1}],teachers:[{id:0,name:'Teacher'}],lessons:[{id:0,group:0,teacher:0,subgroup:-1,name:'Subject',total_hours:20}],teaching_ledger:ledger}
 await fs.writeFile(path.join(dir,'data/timetable_data.json'),JSON.stringify(data))
 const result=spawnSync(path.resolve('build/Release/timetable_solver.exe'),['--hours-report'],{cwd:dir,encoding:'utf8',windowsHide:true,maxBuffer:5e6});assert.equal(result.status,0,result.stderr)
 const report=JSON.parse(result.stdout)
 assert.deepEqual(report.weeks.map(({from,to})=>({from,to})),[{from:'2026-09-01',to:'2026-09-05'},{from:'2026-09-07',to:'2026-09-12'},{from:'2026-09-14',to:'2026-09-19'}])
 assert.deepEqual(report.groups[0].weekly_hours,[2,8,0]);assert.equal(report.groups[0].scheduled_hours,10);assert.equal(report.teachers[0].remaining_weeks,2)
 const events=report.lessons[0].scheduled_occurrences;assert.equal(events.length,3);assert.equal(events.find(e=>e.date==='2026-09-07').week_index,2)
 for(const e of events){assert.equal(e.journal_id,ledger.find(r=>r.date===e.date).id);assert.equal(e.source_file,'week.xlsx');assert.equal(e.status,'confirmed')}
 assert.deepEqual(JSON.parse(await fs.readFile(path.join(dir,'data/timetable_data.json'),'utf8')),data)
})
