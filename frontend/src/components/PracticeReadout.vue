<template>
  <section class="card practice-readout">
    <div class="report-heading"><div><h2>Вычитка до практики и конца семестра</h2><p>Расчёт на {{ report?.as_of_date || 'дату начала генерации' }}. Факт — подтверждённые часы до этой даты, без классных часов и проектов расписаний.</p></div><button class="btn btn-secondary btn-sm" :disabled="loading" @click="load()">{{ loading ? 'Считаю…' : 'Обновить прогноз' }}</button></div>
    <div class="filters"><label>Дата прогноза<input v-model="asOf" type="date" /></label><label>Конец периода<input v-model="periodEnd" type="date" :min="asOf" /></label><button class="btn btn-secondary btn-sm" :disabled="loading||!asOf||!periodEnd||periodEnd<asOf" @click="load(true)">Рассчитать на эти даты</button></div>
    <p v-if="error" class="error">{{ error }}</p>
    <template v-if="report">
      <p v-if="report.rules_version!==2" class="error">Работает предыдущая сборка сервера. Для полного контроля календаря и прогноза по предметам требуется перезапуск сайта с новой сборкой.</p>
      <p>{{ earlyGroups.length }} групп с ранним сроком. {{ riskGroups.length }} групп с риском вычитки. Конец семестра: {{ report.semester_end_date }}.</p>
      <div class="filters"><label><input v-model="showAll" type="checkbox" /> Показать все группы</label><select v-model.number="selectedGroup" aria-label="Группа прогноза"><option :value="-1">Все группы</option><option v-for="g in report.groups" :key="g.group_id" :value="g.group_id">{{ g.group_name }}</option></select></div>
      <div class="table-wrap"><table><thead><tr><th>Группа</th><th>Завершить до</th><th>Подгруппа</th><th>Осталось, ч</th><th>Нужно, ч/нед.</th><th>Доступно не более, ч</th><th>Дефицит по группе, ч</th><th>Оценка</th></tr></thead><tbody>
        <template v-for="group in visibleGroups" :key="group.group_id"><tr v-for="part in group.subgroups" :key="`${group.group_id}-${part.subgroup}`"><td>{{ group.group_name }}</td><td>{{ group.deadline }}</td><td>{{ part.subgroup }}</td><td>{{ part.remaining_hours }}</td><td>{{ part.required_hours_per_week == null ? 'Нет дней' : part.required_hours_per_week.toFixed(1) }}</td><td>{{ group.capacity_hours_per_subgroup }}</td><td>{{ part.shortfall_hours }}</td><td :class="{error:group.status==='shortfall'}">{{ group.status==='shortfall' ? 'Проверить причины' : 'Верхняя оценка достаточна' }}</td></tr></template>
      </tbody></table></div>
      <p class="help">Нулевой дефицит по группе не исключает проблемы с назначением или доступностью преподавателя. Выберите группу, чтобы увидеть её предметы и причины ниже.</p><h3>Предметы и преподаватели</h3>
      <div class="filters"><input v-model="search" placeholder="Группа, преподаватель или предмет" aria-label="Поиск предметов" /><label><input v-model="onlyRisk" type="checkbox" /> Только риски</label><label><input v-model="onlySenior" type="checkbox" /> Только 2–4 курс</label><button class="btn btn-secondary btn-sm" @click="download">Скачать отчёт CSV</button></div>
      <div class="table-wrap"><table><thead><tr><th>Группа / п/г</th><th>Преподаватель</th><th>Предмет</th><th>Вид</th><th>План, ч</th><th>Факт, ч</th><th>Осталось, ч</th><th>Срок</th><th>Ч/нед.</th><th>Совместно доступно ≤ ч</th><th>Оценка</th></tr></thead><tbody><tr v-for="l in visibleLessons" :key="l.lesson_id"><td>{{ l.group_name }} / {{ l.subgroup<0?'все':l.subgroup%2+1 }}</td><td>{{ l.teacher_name }}</td><td>{{ l.subject }}</td><td>{{ l.kind }}</td><td>{{ l.planned_hours }}</td><td>{{ l.confirmed_hours }}</td><td>{{ l.remaining_hours }}</td><td>{{ l.deadline }}</td><td>{{ l.required_hours_per_week?.toFixed(1)??'Нет дней' }}</td><td>{{ l.shared_capacity_hours }}</td><td :class="{error:isRisk(l)}">{{ statusName(l.status) }}</td></tr></tbody></table></div>
      <p class="help">Показано {{ visibleLessons.length }} предметов. Темп = остаток часов / оставшиеся учебные недели (доступные учебные дни / 6). Доступное время у предметов пересекается: его нельзя складывать.</p>
      <details v-if="warnings.length" open><summary>Проблемы и предупреждения: {{ warnings.length }}</summary><div class="filters"><select v-model="severity"><option value="all">Все</option><option value="error">Ошибки</option><option value="warning">Предупреждения</option></select></div><p v-for="(warning,index) in visibleWarnings" :key="index" :class="{error:warning.severity==='error'}">{{ warning.message }}</p></details>
      <p class="help">Общие занятия входят в нагрузку каждой физической подгруппы. ПП, каникулы и недели без теории исключены из обычных занятий. Недельный объём календаря сверяется отдельно с вклейками. Это верхняя оценка времени, а не доказательство расписания всего семестра: кабинеты, последовательность теории/ЛПЗ, наставники и рабочие места практики требуют проверки. При дефиците обычная публикация блокируется.</p>
    </template>
  </section>
</template>
<script setup>
import {computed,onMounted,ref,watch} from 'vue'
import {api} from '../api/index.js'
const props=defineProps({refreshKey:{type:Number,default:0}})
const report=ref(null),loading=ref(false),error=ref(''),showAll=ref(false)
const selectedGroup=ref(-1)
const search=ref(''),onlyRisk=ref(true),onlySenior=ref(false),severity=ref('all'),asOf=ref(''),periodEnd=ref('')
const isRisk=l=>!['completed','capacity_upper_bound_ok'].includes(l.status)
const statusName=s=>({completed:'Вычитан',capacity_upper_bound_ok:'Верхняя оценка достаточна',shortfall:'Не хватает времени',teacher_missing:'Нет преподавателя',teacher_inactive:'Преподаватель выключен',lesson_disabled:'Предмет выключен',indivisible_remaining_hours:'Нужно сверить остаток'}[s]||s)
const earlyGroups=computed(()=>(report.value?.groups||[]).filter(g=>g.early_deadline))
const riskGroups=computed(()=>(report.value?.groups||[]).filter(g=>g.status==='shortfall'))
const visibleGroups=computed(()=>(report.value?.groups||[]).filter(g=>(selectedGroup.value<0||g.group_id===selectedGroup.value)&&(showAll.value||g.early_deadline||g.status==='shortfall')))
const warnings=computed(()=>report.value?.issues||[])
const visibleWarnings=computed(()=>warnings.value.filter(i=>(severity.value==='all'||i.severity===severity.value)&&(selectedGroup.value<0||i.group===selectedGroup.value||i.group<0)))
const visibleLessons=computed(()=>(report.value?.lessons||[]).filter(l=>(selectedGroup.value<0||l.group_id===selectedGroup.value)&&(!onlyRisk.value||isRisk(l))&&(!onlySenior.value||(l.course_year>=2&&l.course_year<=4))&&`${l.group_name} ${l.teacher_name} ${l.subject}`.toLocaleLowerCase('ru').includes(search.value.toLocaleLowerCase('ru'))))
async function load(custom=false){loading.value=true;error.value='';try{const r=await api.data.semesterReadout(custom?{as_of_date:asOf.value,period_end_date:periodEnd.value}:null);if(r.ok){report.value=r.data;asOf.value=r.data.as_of_date;periodEnd.value=r.data.period_end_date}else error.value=r.data?.message||'Прогноз недоступен'}catch(e){error.value=e.message}finally{loading.value=false}}
function download(){const quote=v=>'"'+String(v??'').replaceAll('"','""')+'"';const rows=[['Группа','Подгруппа','Преподаватель','Предмет','Вид','План, ч','Факт, ч','Осталось, ч','Срок','Нужно ч/нед','Совместная ёмкость, ч','Дефицит, ч','Оценка'],...visibleLessons.value.map(l=>[l.group_name,l.subgroup<0?'Все':l.subgroup%2+1,l.teacher_name,l.subject,l.kind,l.planned_hours,l.confirmed_hours,l.remaining_hours,l.deadline,l.required_hours_per_week,l.shared_capacity_hours,l.shortfall_hours,statusName(l.status)])];const url=URL.createObjectURL(new Blob(['\uFEFF'+rows.map(r=>r.map(quote).join(';')).join('\r\n')],{type:'text/csv;charset=utf-8'}));const a=document.createElement('a');a.href=url;a.download=`Прогноз_вычитки_${report.value.as_of_date}.csv`;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000)}
onMounted(()=>load())
watch(()=>props.refreshKey,()=>load())
</script>
<style scoped>
.practice-readout{padding:18px;margin-bottom:20px}.report-heading{display:flex;justify-content:space-between;gap:16px;align-items:start}.report-heading h2{margin:0;font-size:20px}.report-heading p,.help{color:var(--text-secondary);font-size:13px}.error{color:var(--error)}.table-wrap{max-height:380px;margin-top:12px}.table-wrap th{position:sticky;top:0;background:var(--bg-secondary)}details{margin-top:14px}details p{font-size:13px}summary{cursor:pointer}.filters{display:flex;gap:12px;align-items:center;flex-wrap:wrap;margin:12px 0}.filters input:not([type=checkbox]),.filters select{padding:8px;background:var(--bg-secondary);color:var(--text-primary);border:1px solid var(--border);border-radius:6px}.filters label{display:flex;gap:6px;align-items:center;font-size:13px}
</style>
