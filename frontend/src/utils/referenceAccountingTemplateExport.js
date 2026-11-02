import ExcelJS from 'exceljs'
import template from './accountingTemplate.js'
import { subgroupOrdinal } from './schedulePresentation.js'
import { referenceGroupWeeks, referenceLessonRows, referenceWeeklyHours, referenceTeacherConductedNote, referenceLessonConductedNote, referenceSummaryWeeks } from './referenceAccountingMetrics.js'

const clone = value => JSON.parse(JSON.stringify(value))
const number = value => Number(value || 0)
const quote = value => `'${value.replaceAll("'", "''")}'`
const date = value => value ? new Date(`${value}T00:00:00Z`) : null
const sum = (items, field) => items.reduce((total,item)=>total+number(item[field]),0)
const formula = (cell, text, result) => { cell.value={formula:text,result:result===0?'0':result} }
const divide = (cell, numerator, denominator, value, divisor) => {
  formula(cell, `IF(${denominator}>0,${numerator}/${denominator},IF(${numerator}=0,0,"нет недель"))`,
    divisor > 0 ? value/divisor : value === 0 ? 0 : 'нет недель')
}
function presentation(sheet, base, firstWeek, count, dataRow) {
  sheet.properties=clone(base.properties)
  sheet.views=clone(base.views)
  sheet.pageSetup=clone(base.pageSetup)
  base.cols.forEach((width,index)=>{sheet.getColumn(index+1).width=width})
  for(let i=0;i<count;i++)sheet.getColumn(firstWeek+i).width=base.weekWidth || 7
  for(let i=0;i<5;i++)sheet.getColumn(firstWeek+count+i).width=base.metaWidths[i] || 8
  base.rowHeights.forEach((height,index)=>{sheet.getRow(index+1).height=height})
  for(const item of base.fixedCells){const cell=sheet.getCell(item.row,item.col);cell.style=clone(item.style);if(item.value!==null)cell.value=clone(item.value)}
  for(const range of base.merges) {
    const match=range.match(/([A-Z]+)(\d+):([A-Z]+)(\d+)/)
    if(match && sheet.getColumn(match[1]).number<firstWeek && sheet.getColumn(match[3]).number<firstWeek)sheet.mergeCells(range)
  }
  for(let row=1;row<=dataRow;row++)for(let w=0;w<count;w++)sheet.getCell(row,firstWeek+w).style=clone(base.weekStyles[row-1])
}
function dataStyle(sheet,row,base,weekCount,firstWeek) {
  sheet.getRow(row).height=base.rowHeights.at(-1)
  base.fixedDataStyles.forEach((style,i)=>{sheet.getCell(row,i+1).style=clone(style)})
  for(let w=0;w<weekCount;w++)sheet.getCell(row,firstWeek+w).style=clone(base.weekStyles.at(-1))
  for(let i=0;i<5;i++)sheet.getCell(row,firstWeek+weekCount+i).style=clone(base.metaStyles.at(-1)[i])
}
function uniqueName(name, used) {
  const base=String(name).replace(/[\\/?*:[\]]/g,' ').slice(0,31)||'Группа'
  let value=base,index=2
  while(used.has(value.toLocaleLowerCase('ru'))){const tail=` (${index++})`;value=base.slice(0,31-tail.length)+tail}
  used.add(value.toLocaleLowerCase('ru'));return value
}
function groupSheet(book,hours,group,semester,used) {
  const weeks=hours.weeks||[], count=weeks.length, first=10, last=first+count-1, totalCol=first+count
  const sheet=book.addWorksheet(uniqueName(group.group_name,used))
  const rows=referenceLessonRows(hours,group).sort((a,b)=>number(a.workload_order??a.lesson_id)-number(b.workload_order??b.lesson_id)||String(a.source_index||'').localeCompare(String(b.source_index||''),'ru',{numeric:true})||number(a.subject_id)-number(b.subject_id)||number(a.subgroup??-1)-number(b.subgroup??-1)||Number(a.replacement||0)-Number(b.replacement||0))
  const metrics=referenceGroupWeeks(group,hours), totalLetter=sheet.getColumn(totalCol).letter
  const accountedLetter=sheet.getColumn(totalCol+1).letter, remainingLetter=sheet.getColumn(totalCol+2).letter
  const firstPart=sheet.getColumn(totalCol+3).letter, secondPart=sheet.getColumn(totalCol+4).letter
  presentation(sheet,template.group,first,count,10)
  sheet.getCell('B2').value=String(group.accounting_title||group.group_name).replace(` - ${group.group_name}`,'')
  sheet.getCell('E3').value=group.group_name
  sheet.getCell('D8').value=`${semester} сем.`
  sheet.getCell('D9').value='ауд'
  sheet.getCell('F6').value='выдано в семестре'
  sheet.getCell('G6').value='остаток в семестре'
  weeks.forEach((week,index)=>{
    const col=first+index, source=book.getWorksheet('Расписание').getColumn(index+1).letter
    sheet.getCell(5,col).value=week.index
    sheet.mergeCells(6,col,7,col)
    formula(sheet.getCell(6,col),`${quote('Расписание')}!${source}3`,date(week.from))
    formula(sheet.getCell(8,col),`${quote('Расписание')}!${source}4`,date(week.to))
    sheet.getCell(6,col).numFmt='dd.mm';sheet.getCell(8,col).numFmt='dd.mm'
    const cutoff = hours.as_of_date || hours.accounting_imported_through
    const terms = metrics.dailyWeights[index].map(item => `IF('Расписание'!$A$6>=DATE(${item.date.split('-').map(Number).join(',')}),${item.weight},0)`)
    if(cutoff) formula(sheet.getCell(9,col),terms.join('+')||'0',metrics.accountedWeights[index])
    else sheet.getCell(9,col).value=metrics.accountedWeights[index]
  })
  for(let row=1;row<=9;row++)for(let i=0;i<5;i++)sheet.getCell(row,totalCol+i).style=clone(template.group.metaStyles[row-1][i])
  sheet.getCell(9,totalCol).value=metrics.total
  formula(sheet.getCell(9,totalCol+1),count?`SUM(J9:${sheet.getColumn(last).letter}9)`:'0',metrics.accounted)
  formula(sheet.getCell(9,totalCol+2),`${totalLetter}9-${accountedLetter}9`,metrics.remaining)
  for(const [i,label]of [[3,'1 подгр.'],[4,'2 подгр.']]){sheet.mergeCells(6,totalCol+i,7,totalCol+i);sheet.getCell(6,totalCol+i).value=label}
  const weekValues=[]
  const subjectCells=[]
  rows.forEach((item,index)=>{
    const row=index+10, weekly=referenceWeeklyHours(item,weeks), issued=item.issued, planned=number(item.planned_hours)
    dataStyle(sheet,row,template.group,count,first)
    sheet.getCell(row,2).value=item.source_index||''
    const ordinal=subgroupOrdinal(item.subgroup,item.group_id)
    const hasParts=rows.some(other=>number(other.subject_id)===number(item.subject_id)&&number(other.subgroup??-1)>=0)
    sheet.getCell(row,3).value=`${item.name}${ordinal?` ${ordinal} п/г`:hasParts?'(т)':''}${item.replacement?' (замена)':''}`
    sheet.getCell(row,3).note=referenceLessonConductedNote(item,ordinal?`${ordinal} подгруппа`:hasParts?'вся группа':'')
    subjectCells.push({cell:sheet.getCell(row,3),item,ordinal})
    sheet.getCell(row,4).value=planned
    sheet.getCell(row,5).value=item.teacher_name||'вакансия'
    weekly.forEach((value,w)=>{sheet.getCell(row,first+w).value=value})
    if (!item.occurrences.length && issued) sheet.getCell(row,6).value=issued
    else formula(sheet.getCell(row,6),count?`SUM(J${row}:${sheet.getColumn(last).letter}${row})`:'0',issued)
    formula(sheet.getCell(row,7),`D${row}-F${row}`,planned-issued)
    divide(sheet.getCell(row,8),`D${row}`,`$${totalLetter}$9`,planned,metrics.total)
    divide(sheet.getCell(row,9),`G${row}`,`$${remainingLetter}$9`,planned-issued,metrics.remaining)
    sheet.getCell(row,totalCol+3).value=ordinal===2?0:1
    sheet.getCell(row,totalCol+4).value=ordinal===1?0:1
    weekValues.push(weekly)
  })
  const end=9+rows.length, footer=end+1
  const range=letter=>rows.length?`${letter}10:${letter}${end}`:null
  for(let col=4;col<=last;col++){
    dataStyle(sheet,footer,template.group,count,first)
    const letter=sheet.getColumn(col).letter
    const values=rows.map((item,index)=>col===4?number(item.planned_hours):col===6?item.issued:col===7?number(item.planned_hours)-item.issued:col===8?(metrics.total?number(item.planned_hours)/metrics.total:0):col===9?(metrics.remaining?Math.max(0,number(item.planned_hours)-item.issued)/metrics.remaining:0):weekValues[index]?.[col-first]||0)
    if(col===5)continue
    if(col===9)divide(sheet.getCell(footer,col),`G${footer}`,`$${remainingLetter}$9`,sum(rows,'planned_hours')-sum(rows,'issued'),metrics.remaining)
    else formula(sheet.getCell(footer,col),range(letter)?`SUM(${range(letter)})`:'0',values.reduce((a,b)=>a+b,0))
  }
  for(const ordinal of [1,2]){
    const row=footer+ordinal,mask=ordinal===1?firstPart:secondPart
    dataStyle(sheet,row,template.group,count,first)
    sheet.getCell(row,6).value=`${ordinal}п/г`
    for(let col=7;col<=last;col++){
      const letter=sheet.getColumn(col).letter
      const value=rows.reduce((result,item,index)=>{
        const part=subgroupOrdinal(item.subgroup,item.group_id);if(part&&part!==ordinal)return result
        return result+(col===7?number(item.planned_hours)-item.issued:col===8?(metrics.total?number(item.planned_hours)/metrics.total:0):col===9?(metrics.remaining?(number(item.planned_hours)-item.issued)/metrics.remaining:0):weekValues[index][col-first])
      },0)
      formula(sheet.getCell(row,col),rows.length?`SUMPRODUCT(${letter}$10:${letter}$${end},$${mask}$10:$${mask}$${end})`:'0',value)
    }
  }
  sheet.pageSetup.printTitlesRow='1:9'
  return{sheet,rows,metrics,end,remainingLetter,weekValues,subjectCells}
}
function addLessonDates(book,groups,hours) {
  const dates=book.addWorksheet('Даты занятий',{views:[{state:'frozen',ySplit:1}]})
  dates.columns=[{header:'Группа',key:'group',width:23},{header:'Предмет',key:'subject',width:65},
    {header:'Преподаватель',key:'teacher',width:43},{header:'Дата',key:'date',width:15},
    {header:'Пара',key:'slot',width:9},{header:'Часы',key:'hours',width:9},
    {header:'Подгруппа',key:'subgroup',width:15},{header:'К предмету',key:'back',width:18}]
  dates.getRow(1).font={name:'Calibri',size:11,bold:true}
  dates.getRow(1).height=26
  for(const group of groups)for(const {cell,item,ordinal} of group.subjectCells){
    const target=dates.rowCount+1,label=cell.value
    const events=[...(item.occurrences||[])].filter(e=>e.date).sort((a,b)=>a.date.localeCompare(b.date)||number(a.slot)-number(b.slot))
    for(const event of events.length?events:[null]){
      const row=dates.addRow({group:item.group_name||group.sheet.name,subject:label,teacher:item.teacher_name||'вакансия',
        date:event?date(event.date):'Занятий нет',slot:event?.slot??'',hours:event?.hours??0,subgroup:ordinal?`${ordinal} подгруппа`:'вся группа',
        back:{text:'К предмету',hyperlink:`#${quote(group.sheet.name)}!${cell.address}`}})
      row.height=30;row.alignment={vertical:'middle',wrapText:true};row.getCell(4).numFmt='dd.mm.yyyy'
    }
    if(events.length) {
      const end=dates.rowCount, r=cell.row
      group.weekValues[group.subjectCells.findIndex(entry=>entry.cell===cell)].forEach((cached,w)=>{
        const col=group.sheet.getColumn(10+w).letter
        const cutoff=(hours.as_of_date||hours.accounting_imported_through)?`,'Даты занятий'!$D$${target}:$D$${end},"<="&'Расписание'!$A$6`:''
        formula(group.sheet.getCell(r,10+w),`SUMIFS('Даты занятий'!$F$${target}:$F$${end},'Даты занятий'!$D$${target}:$D$${end},">="&${col}$6,'Даты занятий'!$D$${target}:$D$${end},"<"&${col}$8+1${cutoff})`,cached)
      })
    }
    cell.value={text:label,hyperlink:`#'Даты занятий'!A${target}`,tooltip:'Нажмите, чтобы открыть все даты и пары этого предмета'}
    cell.font={...cell.font,underline:true,color:{argb:'FF0563C1'}}
  }
  dates.autoFilter={from:'A1',to:`H${dates.rowCount}`}
  dates.pageSetup={orientation:'landscape',fitToPage:true,fitToWidth:1,fitToHeight:0,printTitlesRow:'1:1'}
  return dates
}
function summarySheet(book,hours,groups,semester){
  const weeks=hours.weeks||[],sheet=book.addWorksheet('пр'),denominator=9+weeks.length
  presentation(sheet,template.summary,9,weeks.length,3)
  sheet.getCell('A2').value='преподаватель';sheet.getCell('C1').value=`${semester} семестр`
  sheet.getCell('F1').value='ср. недельная нагрузка исходная по группам'
  sheet.getCell('G1').value='кор. ср. нагрузки по группам'
  sheet.getCell('H1').value='нагрузка в парах в неделю'
  const remaining=referenceSummaryWeeks(hours)
  formula(sheet.getCell(2,denominator),groups.length?`${quote(groups[0].sheet.name)}!${groups[0].remainingLetter}9`:String(remaining),remaining)
  sheet.getCell(2,denominator).note='Общий знаменатель сводки связан с остатком учебных недель первой группы, как в исходном образце. F и G используют индивидуальные сроки каждой группы.'
  weeks.forEach((week,index)=>{sheet.getCell(1,9+index).value=week.index;formula(sheet.getCell(2,9+index),`'Расписание'!${book.getWorksheet('Расписание').getColumn(index+1).letter}3`,date(week.from));sheet.getCell(2,9+index).numFmt='dd.mm'})
  const teachers=[...(hours.teachers||[])].filter(t=>{
    const name=String(t.teacher_name||'').trim().toLocaleLowerCase('ru').replaceAll('ё','е')
    return !(name.includes('вынес')&&name.includes('пп'))&&
      (number(t.planned_hours)||number(t.credited_hours)||number(t.adjustment_hours))
  }).sort((a,b)=>a.teacher_name.localeCompare(b.teacher_name,'ru'))
  teachers.forEach((teacher,index)=>{
    const row=index+3, planned=number(teacher.planned_hours),credited=number(teacher.credited_hours??teacher.scheduled_hours),left=planned-credited
    dataStyle(sheet,row,template.summary,weeks.length,9)
    sheet.getCell(row,1).value=teacher.teacher_name
    sheet.getCell(row,1).note=referenceTeacherConductedNote(teacher,hours)
    const add=(column,groupColumn,cached,adjustment=0)=>{
      const text=groups.filter(group=>group.rows.length).map(group=>`SUMIF(${quote(group.sheet.name)}!$E$10:$E$${group.end},$A${row},${quote(group.sheet.name)}!${groupColumn}$10:${groupColumn}$${group.end})`).join('+')||'0'
      formula(sheet.getCell(row,column),text+(adjustment?`${adjustment>0?'+':''}${adjustment}`:''),cached)
    }
    const teacherRows=groups.flatMap(g=>g.rows.filter(r=>r.teacher_name===teacher.teacher_name))
    const exportedPlanned=sum(teacherRows,'planned_hours'), exportedIssued=sum(teacherRows,'issued')
    const adjustment=number(teacher.adjustment_hours), balance=exportedPlanned-exportedIssued-adjustment
    add(4,'F',exportedIssued+adjustment,adjustment)
    add(5,'G',balance,-adjustment)
    formula(sheet.getCell(row,3),`D${row}+E${row}`,exportedPlanned)
    const remainingBalance=balance
    const correctedAverage=remaining>0?remainingBalance/remaining:remainingBalance===0?0:'нет недель'
    const denominatorLetter=sheet.getColumn(denominator).letter
    const averageFormula=`IF($${denominatorLetter}$2>0,E${row}/$${denominatorLetter}$2,IF(E${row}=0,0,"нет недель"))`
    const groupAverage=(column,source,field)=>{
      const selected=groups.filter(g=>g.rows.length)
      const missing=selected.map(g=>`COUNTIFS(${quote(g.sheet.name)}!$E$10:$E$${g.end},$A${row},${quote(g.sheet.name)}!${source}$10:${source}$${g.end},"нет недель")`).join('+')||'0'
      const text=selected.map(g=>`SUMIF(${quote(g.sheet.name)}!$E$10:$E$${g.end},$A${row},${quote(g.sheet.name)}!${source}$10:${source}$${g.end})`).join('+')||'0'
      let cached=0,unavailable=0
      for(const g of selected)for(const r of g.rows.filter(r=>r.teacher_name===teacher.teacher_name)){
        const divisor=g.metrics[field],value=source==='H'?number(r.planned_hours):number(r.planned_hours)-r.issued
        if(divisor>0)cached+=value/divisor;else if(value!==0)unavailable++
      }
      const helper=sheet.getColumn(denominator+(source==='H'?1:2)).letter
      sheet.getCell(1,denominator+(source==='H'?1:2)).value=source==='H'?'Нет недель в исходной нагрузке':'Нет недель в остатке по группам'
      formula(sheet.getCell(row,denominator+(source==='H'?1:2)),missing,unavailable)
      formula(sheet.getCell(row,column),`IF(${helper}${row}>0,"нет недель",${text})`,unavailable?'нет недель':cached)
    }
    groupAverage(6,'H','total')
    groupAverage(7,'I','remaining')
    formula(sheet.getCell(row,2),averageFormula,correctedAverage)
    formula(sheet.getCell(row,8),`IF(ISNUMBER(B${row}),B${row}/2,"нет недель")`,typeof correctedAverage==='number'?correctedAverage/2:'нет недель')
    weeks.forEach((week,w)=>{
      const groupColumn=book.getWorksheet(groups[0]?.sheet.name)?.getColumn(10+w).letter
      if(groupColumn)add(9+w,groupColumn,groups.reduce((total,g)=>total+g.rows.filter(r=>r.teacher_name===teacher.teacher_name).reduce((s,r)=>s+referenceWeeklyHours(r,weeks)[w],0),0))
      else sheet.getCell(row,9+w).value=number(teacher.weekly_hours?.[w])
    })
  })
  return sheet
}
export function buildTemplateAccountingWorkbook(input){
  const weeks=input.weeks||[]
  const cutoff=input.as_of_date||'9999-12-31'
  const latestAccounted=(input.lessons||[]).flatMap(l=>l.scheduled_occurrences||[])
    .filter(item=>!item.date||item.date<=cutoff)
    .reduce((last,item)=>Math.max(last,number(item.week_index)),0)
  const hours={...input,latest_accounted_week:latestAccounted}, semester=Number(String(hours.semester_start||weeks[0]?.from||'').slice(5,7))>=7?1:2
  const book=new ExcelJS.Workbook();book.creator='Генератор расписания техникума'
  // Excel must recalculate every linked summary and group formula on open.
  book.calcProperties={calcMode:'auto',fullCalcOnLoad:true,forceFullCalc:true}
  const schedule=book.addWorksheet('Расписание')
  schedule.getCell('A5').value='Дата учёта (включительно)'
  schedule.getCell('A6').value=date(hours.as_of_date||hours.accounting_imported_through)
  schedule.getCell('A6').numFmt='dd.mm.yyyy'
  schedule.getCell('A6').note='Дата определяет прошедшую долю учебных недель. Подтверждённые занятия за другую дату загрузите новым экспортом с сайта.'
  schedule.getRow(1).height=20;schedule.getRow(2).height=36
  weeks.forEach((week,index)=>{
    schedule.getColumn(index+1).width=24
    schedule.getCell(1,index+1).value=week.index
    schedule.getCell(3,index+1).value=date(week.from);schedule.getCell(4,index+1).value=date(week.to)
    schedule.getCell(3,index+1).numFmt='dd.mm';schedule.getCell(4,index+1).numFmt='dd.mm'
    const source=(hours.accounting_source_links||[]).find(source=>source.from<=week.to&&source.to>=week.from)
    if(source)schedule.getCell(2,index+1).value={text:source.label||source.url,hyperlink:source.url}
  })
  const used=new Set(['расписание','пр','даты занятий']),groups=[...(hours.groups||[])].sort((a,b)=>number(a.accounting_order??a.group_id)-number(b.accounting_order??b.group_id)||number(a.group_id)-number(b.group_id))
  const sheets=groups.map(group=>groupSheet(book,hours,group,semester,used))
  const summary=summarySheet(book,hours,sheets,semester)
  // The original workbook starts with Расписание, пр, followed by group sheets.
  summary.orderNo=1;schedule.orderNo=0;sheets.forEach((group,index)=>{group.sheet.orderNo=index+2})
  addLessonDates(book,sheets,hours).orderNo=sheets.length+2
  return{workbook:book,groups,semester,remainingWeeks:referenceSummaryWeeks(hours),templateSha256:template.sha256}
}
