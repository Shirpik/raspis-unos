import fs from 'node:fs'
const source=process.argv[2]||'.tmp/google-sheets/final-week.json'
const modelPath=process.argv[3]||'.tmp/google-sheets/week-full-12-21-opt.quota-model.json'
const resultPath=process.argv[4]||'.tmp/google-sheets/week-full-12-21-opt.quota-raw.json'
const dest=process.argv[5]||'.tmp/google-sheets/week-full-candidate.json'
const data=JSON.parse(fs.readFileSync(source,'utf8'))
const model=JSON.parse(fs.readFileSync(modelPath,'utf8'))
const result=JSON.parse(fs.readFileSync(resultPath,'utf8'))
if(!result.success)throw new Error('quota optimizer did not find feasible quotas')
const byId=new Map(model.variables.map(v=>[v.id,v]))
for(const lesson of data.lessons){
 const variable=byId.get(Number(lesson.id))
 const slots=variable?Number(result.quotas[String(lesson.id)]||0):0
 if(lesson.is_block&&slots%2)throw new Error(`odd UP quota ${lesson.id}`)
 lesson.total_slots=lesson.is_block?slots/2:slots
 lesson.generation_active=slots>0
 if(slots>0)lesson.week_quota_source='exact_quota_optimizer_20260928'
}
const partLoads=model.parts.map(p=>({group:p.group,part:p.part,pairs:p.lesson_ids.reduce((n,id)=>n+Number(result.quotas[String(id)]||0),0)}))
data.meta={...(data.meta||{}),week_quota_status:result.status,week_quota_model:modelPath,week_quota_min:Math.min(...partLoads.map(x=>x.pairs)),week_quota_max:Math.max(...partLoads.map(x=>x.pairs)),week_quota_average:partLoads.reduce((a,x)=>a+x.pairs,0)/partLoads.length}
fs.writeFileSync(dest,JSON.stringify(data,null,2)+'\n')
fs.writeFileSync(dest.replace(/\.json$/,'.quota-summary.json'),JSON.stringify({partLoads,lessonsSelected:data.lessons.filter(l=>l.generation_active).length,status:result.status},null,2)+'\n')
console.log({dest,selected:data.lessons.filter(l=>l.generation_active).length,min:data.meta.week_quota_min,max:data.meta.week_quota_max,average:data.meta.week_quota_average,under18:partLoads.filter(x=>x.pairs<18).length})
