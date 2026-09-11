// The workbook is read only. Colour is not an identifier: green '=' is vacation.
const sourceUrl = 'https://docs.google.com/spreadsheets/d/1YQ4TsERPrgtNNrC1weyDPox8hwIjU8XDfaM9XDO2X4U/edit'
const key = value => String(value).toLowerCase().replaceAll('ё','е').replace(/[^а-яa-z0-9]/g,'').replaceAll('пф','')
const aliases = {'мцм102':'МЦМ-Пф-103','тэорп2901':'ТЭиРП-2901','такхс2202':'ТАКХС-Пф-2202','такх4202':'ТАКХС-4202','такхс3202':'ТАКХС-3201','мцм407':'МЦМ-408'}
const iso = value => {
  const date = typeof value === 'number' ? new Date(Date.UTC(1899,11,30) + Math.round(value)*86400000) : value instanceof Date ? value : null
  if (!date || !Number.isFinite(date.getTime())) throw new Error('Не прочитаны полные даты верхнего столбца')
  return date.toISOString().slice(0,10)
}
const addDays = (date,days) => new Date(Date.parse(date+'T00:00:00Z')+days*86400000).toISOString().slice(0,10)
export function parsePracticeCalendar(workbook, groups, XLSX, sha256='') {
  const first = workbook.Sheets['1_2_курсы']
  if (!first || !workbook.Sheets['3_4_курсы']) throw new Error('Нужны листы «1_2_курсы» и «3_4_курсы»')
  const cell = (sheet,row,col) => sheet[XLSX.utils.encode_cell({r:row-1,c:col-1})]?.v
  const weeks = Array.from({length:52},(_,i)=>({col:i+9,from:iso(cell(first,10,i+9)),to:iso(cell(first,11,i+9))}))
  weeks.forEach((w,i)=>{
    if (new Date(w.from+'T00:00:00Z').getUTCDay()!==1 || addDays(w.from,6)!==w.to || (i&&addDays(weeks[i-1].to,1)!==w.from)) throw new Error('Повреждена последовательность учебных недель')
  })
  const used=new Set(), entries=[]
  for (const name of ['1_2_курсы','3_4_курсы']) {
    const sheet=workbook.Sheets[name]
    if (name==='3_4_курсы') for (const w of weeks) {
      if (+cell(sheet,2,w.col)!==+w.from.slice(-2)||+cell(sheet,3,w.col)!==+w.to.slice(-2)) throw new Error('Даты двух листов расходятся')
    }
    const last=XLSX.utils.decode_range(sheet['!ref']).e.r+1
    const starts=[]
    for(let row=1;row<=last;row++) if(cell(sheet,row,3)&&String(cell(sheet,row,5)).trim()==='теория')starts.push(row)
    starts.forEach((row,index)=>{
      const sourceName=String(cell(sheet,row,3)).trim(), mappedName=aliases[key(sourceName)]||sourceName
      const matches=groups.filter(g=>key(g.name)===key(mappedName))
      if(matches.length!==1)throw new Error(`${name}, строка ${row}: группа «${sourceName}» не сопоставлена однозначно`)
      const g=matches[0]
      if(used.has(g.id))throw new Error(`Группа ${g.name} указана дважды. СП-4611 и СП-4612п нельзя объединять.`)
      used.add(g.id)
      const stop=starts[index+1]||Math.min(row+8,last+1)
      const calendar=weeks.map(w=>{
        const rec={from:w.from,to:w.to,theory_hours:0,up_hours:0,pp_hours:0,exam_hours:0,vacation:false,source_cells:[]}
        for(let r=row;r<stop;r++){
          const label=String(cell(sheet,r,5)).trim().toLowerCase().replaceAll(' ',''), value=cell(sheet,r,w.col)
          const field={'теория':'theory_hours','уч.практика':'up_hours','п.практика':'pp_hours','пр.практика':'pp_hours','пдп':'pp_hours','экзамены':'exam_hours','гиа':'exam_hours'}[label]
          if(!field)continue
          if(typeof value==='number'){
            if(!Number.isInteger(value)||value<0)throw new Error(`${name}, строка ${r}: некорректные часы`)
            rec[field]+=value
          }
          if(label==='теория'&&String(value).trim()==='=')rec.vacation=true
          if(value!=null&&value!=='')rec.source_cells.push(XLSX.utils.encode_cell({r:r-1,c:w.col-1}))
        }
        return rec
      })
      const periods=[]
      for(const w of calendar.filter(w=>w.pp_hours>0)){
        const p=periods.at(-1)
        if(p&&addDays(p.to,1)===w.from){p.to=w.to;p.calendar_hours+=w.pp_hours}
        else periods.push({from:w.from,to:w.to,calendar_hours:w.pp_hours,kind:'industrial'})
      }
      entries.push({id:g.id,name:g.name,expected_calendar:{practice_periods:g.practice_periods||[],academic_calendar:g.academic_calendar||[],practice_calendar_source:g.practice_calendar_source||{}},practice_periods:periods,academic_calendar:calendar,calendar_theory_semester_hours:cell(sheet,row,7),practice_calendar_source:{url:sourceUrl,sheet:name,row,name:sourceName,sha256}})
    })
  }
  const missing=groups.filter(g=>!used.has(g.id))
  if(missing.length)throw new Error('В календаре нет групп: '+missing.map(g=>g.name).join(', '))
  return entries
}
