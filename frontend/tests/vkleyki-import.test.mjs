import test from 'node:test'
import assert from 'node:assert/strict'
import * as X from 'xlsx'
import {parseVkleyki} from '../src/utils/excelImport.js'

test('workload group alias preserves lesson IDs, journal links and manually disabled plan rows',async()=>{
 const lesson={id:5,group:22,name:'Математика',teacher:7,subgroup:44,subject_id:9,total_hours:20,total_slots:3,generation_active:true,plan_active:false,curriculum_active:true,fixed_room:9,allowed_campuses:[1],source_index:'ОП.01'}
 const data={groups:[{id:22,name:'ПКД-3806/07п',parts:2}],teachers:[{id:7,name:'Teacher'}],lessons:[lesson],teaching_ledger:[{id:1,lesson_id:5,date:'2026-09-07',slot:1,hours:2,status:'confirmed'}]}
 const book=X.utils.book_new();X.utils.book_append_sheet(book,X.utils.aoa_to_sheet([['Индекс','Наименование','','Всего','Семестр','','Преподаватель'],['','','','','5 сем','',''],['ОП.01','Математика 1 п/г','','',32,'','Teacher']]),'ПКД-380607п')
 const bytes=X.write(book,{type:'buffer',bookType:'xlsx'}),result=await parseVkleyki({name:'source.xlsx',arrayBuffer:async()=>bytes},data,1)
 assert.deepEqual(result.errors,[]);assert.equal(result.data.groups.length,1);assert.equal(result.data.lessons.length,1)
 assert.equal(result.data.groups[0].name,'ПКД-3806/07п');assert.equal(result.data.groups[0].accounting_order,0);assert.equal(result.data.lessons[0].id,5);assert.equal(result.data.lessons[0].subgroup,44);assert.equal(result.data.lessons[0].total_hours,32);assert.equal(result.data.lessons[0].workload_order,2)
 for(const field of ['plan_active','total_slots','generation_active','fixed_room','allowed_campuses'])assert.deepEqual(result.data.lessons[0][field],lesson[field])
 assert.deepEqual(result.data.teaching_ledger,data.teaching_ledger)
})
