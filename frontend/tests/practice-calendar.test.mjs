import test from 'node:test'
import assert from 'node:assert/strict'
import * as XLSX from 'xlsx'
import {parsePracticeCalendar} from '../src/utils/practiceCalendarImport.js'

function fixture(){
  const first={'!ref':'A1:BH30'},second={'!ref':'A1:BH30'}
  const put=(s,r,c,v)=>s[XLSX.utils.encode_cell({r:r-1,c:c-1})]={v}
  for(let i=0;i<52;i++){
    const from=new Date(Date.UTC(2026,7,31)+i*7*86400000),to=new Date(+from+6*86400000)
    put(first,10,i+9,(+from-Date.UTC(1899,11,30))/86400000);put(first,11,i+9,(+to-Date.UTC(1899,11,30))/86400000)
    put(second,2,i+9,from.getUTCDate());put(second,3,i+9,to.getUTCDate())
  }
  put(first,16,3,'МЦМ-Пф-102');put(first,16,5,'теория');put(first,16,7,576);put(first,16,9,36);put(first,16,10,'=')
  put(second,8,3,'СП-4611');put(second,8,5,'теория');put(second,8,9,24);put(second,9,5,'уч. практика');put(second,9,9,12)
  put(second,10,5,'п. практика');put(second,10,10,36);put(second,10,11,36)
  put(second,16,3,'СП-4612п');put(second,16,5,'теория');put(second,16,9,36)
  return {workbook:{Sheets:{'1_2_курсы':first,'3_4_курсы':second}},groups:[{id:1,name:'МЦМ-Пф-103'},{id:2,name:'СП-4611'},{id:3,name:'СП-4612п'}],put,first,second}
}
test('calendar distinguishes holidays, UP and PP and keeps SP groups separate',()=>{
  const f=fixture(),rows=parsePracticeCalendar(f.workbook,f.groups,XLSX)
  assert.equal(rows.length,3);assert.equal(rows[0].academic_calendar.length,52)
  assert.equal(rows[0].academic_calendar[1].vacation,true);assert.equal(rows[0].practice_periods.length,0)
  assert.equal(rows[1].academic_calendar[0].theory_hours,24);assert.equal(rows[1].academic_calendar[0].up_hours,12)
  assert.deepEqual(rows[1].practice_periods,[{from:'2026-09-07',to:'2026-09-20',calendar_hours:72,kind:'industrial'}])
  assert.equal(rows[2].practice_periods.length,0)
})
test('duplicate SP name must never be guessed as the other SP group',()=>{
  const f=fixture();f.put(f.second,16,3,'СП-4611')
  assert.throws(()=>parsePracticeCalendar(f.workbook,f.groups,XLSX),/дважды/)
})
test('calendar import rejects missing groups and inconsistent date headers',()=>{
  const f=fixture()
  assert.throws(()=>parsePracticeCalendar(f.workbook,[...f.groups,{id:4,name:'Нет в файле'}],XLSX),/нет групп/)
  f.put(f.second,2,9,17)
  assert.throws(()=>parsePracticeCalendar(f.workbook,f.groups,XLSX),/Даты двух листов/)
})
