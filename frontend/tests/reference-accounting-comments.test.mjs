import test from 'node:test'
import assert from 'node:assert/strict'
import ExcelJS from 'exceljs'
import JSZip from 'jszip'
import {buildReferenceAccountingWorkbook,writeReferenceAccountingBuffer} from '../src/utils/referenceAccountingExport.js'
import {referenceLessonConductedNote} from '../src/utils/referenceAccountingMetrics.js'

test('subject notes retain every pair in order, including two pairs on the same date',()=>{
 const note=referenceLessonConductedNote({occurrences:[{date:'2026-10-02',slot:3},{date:'2026-09-01',slot:2},{date:'2026-09-01',slot:1}]},'1 подгруппа')
 assert.equal(note,'Даты проведения:\n1. 01.09.2026 — 1 пара\n2. 01.09.2026 — 2 пара\n3. 02.10.2026 — 3 пара')
 assert(!/[?\uFFFD]/u.test(note))
})

test('XLSX subject links open the complete list for that actual teacher; notes are resized',async()=>{
 const occurrences=Array.from({length:52},(_,i)=>({date:`2026-09-${String(i%26+1).padStart(2,'0')}`,slot:Math.floor(i/26)+1,hours:2,actual_teacher_id:i%2?2:1,actual_teacher_name:i%2?'Замещающий':'Основной'}))
 const input={as_of_date:'2026-09-30',semester_start:'2026-09-01',semester_end:'2026-12-26',weeks:[{index:1,from:'2026-09-01',to:'2026-09-30'}],groups:[{group_id:0,group_name:'Тест-101'}],teachers:[],lessons:[{lesson_id:1,group_id:0,group_name:'Тест-101',teacher_id:1,teacher_name:'Основной',name:'Предмет',planned_hours:80,subgroup:-1,scheduled_occurrences:occurrences}]}
 const {workbook}=buildReferenceAccountingWorkbook(input),group=workbook.getWorksheet('Тест-101'),details=workbook.getWorksheet('Даты занятий')
 assert.equal(details.rowCount,53)
 for(const row of [10,11]){
  const subject=group.getCell(row,3),teacher=group.getCell(row,5).value,target=Number(subject.hyperlink.match(/A(\d+)$/)[1])
  assert.equal(details.getCell(target,3).value,teacher)
  const own=occurrences.filter(e=>e.actual_teacher_name===teacher)
  assert.equal(subject.note.split('\n').length-1,own.length)
  for(const e of own)assert(subject.note.includes(e.date.split('-').reverse().join('.')))
 }
 const bytes=await writeReferenceAccountingBuffer(workbook),zip=await JSZip.loadAsync(bytes)
 const drawing=await zip.file(`xl/drawings/vmlDrawing${group.id}.vml`).async('string')
 assert.match(drawing,/width:285pt;height:464pt/)
 const reloaded=new ExcelJS.Workbook();await reloaded.xlsx.load(bytes)
 assert.equal(reloaded.getWorksheet('Тест-101').getCell('C10').note,group.getCell('C10').note)
 assert.equal(reloaded.getWorksheet('Даты занятий').rowCount,53)
 assert.match(reloaded.getWorksheet('Тест-101').getCell('C10').hyperlink,/Даты занятий/)
})
