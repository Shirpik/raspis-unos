import fs from 'node:fs/promises'
import {FileBlob,SpreadsheetFile} from 'file:///C:/Users/Student/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/@oai/artifact-tool/dist/artifact_tool.mjs'
const root=process.cwd(), folder=process.argv[2] || `${root}/outputs/friday-saturday-20260918`
const d=JSON.parse(await fs.readFile(`${folder}/data/timetable_data.json`,'utf8'))
const s=JSON.parse(await fs.readFile(`${folder}/candidate-cp/schedule_all.json`,'utf8'))
const rooms=new Map(d.rooms.map(x=>[x.id,x])), teachers=new Map(d.teachers.map(x=>[x.id,x]))
const wb=await SpreadsheetFile.importXlsx(await FileBlob.load(`${root}/frontend/public/templates/schedule-template.xlsx`))
const col=n=>String.fromCharCode(65+n)
const previewDir=`${folder}/preview-final`
await fs.mkdir(previewDir,{recursive:true})
let written=0
for(let i=0;i<4;i++){
 // The template's final group columns on courses 3/4 have values but lost
 // their formatting. Extend the adjacent group style, preserving content.
 if(i===2 || i===3){
  const sh=wb.worksheets.getItemAt(i), target=i===2?'O':'N', source=i===2?'N':'M';
  const range=sh.getRange(`${target}62:${target}91`), original=range.values;
  range.copyFrom(sh.getRange(`${source}62:${source}91`),'all');range.values=original;
  sh.getRange(`${target}:${target}`).format.columnWidth=sh.getRange(`${source}:${source}`).format.columnWidth;
  range.format.font={name:'Arial',size:9};range.format.fill='#FFFFFF';
  for(const header of [62,77]){
   const head=sh.getRange(`${target}${header}`);head.format.fill='#C9DAF8';head.format.font={name:'Arial',size:9,bold:true};
   head.format.horizontalAlignment='center';head.format.wrapText=true;
   head.format.borders={preset:'all',style:'thick',color:'#000000'};
   for(let pair=0;pair<7;pair++) sh.getRange(`${target}${header+1+pair*2}:${target}${header+2+pair*2}`).format.borders={
    left:{style:'thick',color:'#000000'},right:{style:'thick',color:'#000000'},
    top:{style:'thin',color:'#000000'},bottom:{style:'thin',color:'#000000'}};
  }
 }
 const sh=wb.worksheets.getItemAt(i), headers=sh.getRange('D62:O62').values[0].map((v,j)=>[String(v||'').trim(),j+3]).filter(x=>x[0]);
 const end=col(headers.at(-1)[1]);
 for(const [date,row] of [['2026-09-18',62],['2026-09-19',77]]){
  sh.getRange(`D${row+1}:${end}${row+14}`).unmerge();sh.getRange(`D${row+1}:${end}${row+14}`).clear({applyTo:'contents'});
  sh.getRange(`A${row}`).values=[[new Date(`${date}T00:00:00Z`)]];sh.getRange(`A${row}`).setNumberFormat('d\\.m');
  const vals=Array.from({length:14},()=>Array(headers.length).fill(null));
  for(const [j,[name,c]] of headers.entries()){
   const g=s.groups.find(x=>x.group_name===name);if(!g)continue;const day=g.days.find(x=>x.date_iso===date);
   for(const slot of day.slots)for(const l of slot.lessons){
    const ord=l.subgroup<0?0:l.subgroup%2+1, firstRow=(slot.slot-1)*2
    const t=teachers.get(l.teacher_id), rm=rooms.get(l.room_id)
    const place=(d.settings.distance_learning && (d.settings.distance_revision||1)<2) || !rm?'Дистант':`${rm.name}_${rm.campus===0?'Л':'К'}`
    const text=`${l.name}${ord?` ${ord} п/г`:''}\n${t.name.split(' ')[0]} ${place}`
    const rows=[firstRow+(ord===2?1:0)]
    for(const r of rows){if(vals[r][j])throw Error(`cell collision ${name}`);vals[r][j]=text;written++}
   }
  }
  sh.getRange(`D${row+1}:${end}${row+14}`).values=vals;sh.getRange(`D${row+1}:${end}${row+14}`).format.wrapText=true;sh.getRange(`D${row+1}:${end}${row+14}`).format.horizontalAlignment='center';sh.getRange(`D${row+1}:${end}${row+14}`).format.verticalAlignment='center';
  if(d.settings.distance_revision>=2){
   sh.getRange(`A${row}:${end}${row+14}`).format.fill='#E2F0D9';
   sh.getRange(`A${row}:${end}${row}`).format.fill='#A9D18E';
  }
 }
 // Make the template's unspecified final-row heights explicit for Excel,
 // so wrapped subgroup names remain visible next to the next day's header.
 for(const row of [[76],[75,76,91],[76,91],[]][i]) {
  const cells=sh.getRange(`D${row}:${end}${row}`);cells.format.autofitRows();cells.format.rowHeight=cells.format.rowHeight+3;
 }
}
await wb.recalculate();console.log((await wb.inspect({kind:'region',sheetId:'1 курс',range:'D62:H91',maxChars:1600})).ndjson);const e=await wb.inspect({kind:'match',searchTerm:'#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A',options:{useRegex:true,maxResults:20},maxChars:1000});console.log(e.ndjson)
for(const sh of wb.worksheets.items){const end=sh.name==='1 курс'||sh.name==='4 курс'?'N':'O';const png=await wb.render({sheetName:sh.name,range:`A62:${end}91`,scale:1,format:'png'});await fs.writeFile(`${previewDir}/${sh.name.replaceAll(' ','_')}.png`,new Uint8Array(await png.arrayBuffer()))}
const x=await SpreadsheetFile.exportXlsx(wb);const output=`${folder}/Расписание_18-19.09.2026_дистант_без_дублей.xlsx`;await x.save(output);console.log(JSON.stringify({output,written,previewDir}));
