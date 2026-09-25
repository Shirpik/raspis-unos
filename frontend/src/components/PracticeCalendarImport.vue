<template>
  <section class="card calendar-import">
    <h2>Календарь учебного времени</h2>
    <p>Загрузите XLSX из Google-таблицы. До применения проверьте группы и даты. Часы вклеек и журнал сохраняются.</p>

    <FileUploader
      ref="uploader"
      accept=".xlsx"
      accept-label="XLSX файл из Google Таблиц"
      :show-upload-button="false"
      @file-selected="preview"
    />

    <p v-if="error" class="error">{{ error }}</p>
    <template v-if="entries.length">
      <p>Однозначно сопоставлено {{ entries.length }} групп, по 52 недели. Источник: {{ filename }}.</p>
      <div class="table-wrap"><table><thead><tr><th>Группа</th><th>ПП / ПДП</th><th>Теория в 1-м семестре, ч</th></tr></thead><tbody><tr v-for="g in entries" :key="g.id"><td>{{ g.name }}</td><td>{{ g.practice_periods.map(p=>`${p.from} — ${p.to}`).join('; ')||'Нет' }}</td><td>{{ g.calendar_theory_semester_hours }}</td></tr></tbody></table></div>
      <button class="btn btn-primary" :disabled="busy" @click="apply">{{ busy?'Сохраняю…':'Применить календарь' }}</button>
    </template>
  </section>
</template>
<script setup>
import {ref} from 'vue'
import {api} from '../api/index.js'
import {parsePracticeCalendar} from '../utils/practiceCalendarImport.js'
import FileUploader from './FileUploader.vue'

const emit=defineEmits(['updated'])
const entries=ref([]),error=ref(''),busy=ref(false),filename=ref(''),uploader=ref(null)

async function preview(file){
  entries.value=[];error.value='';if(!file)return
  busy.value=true
  try{
    const capability=await api.data.semesterReadout();if(!capability.ok||!(capability.data?.rules_version>=3))throw new Error('Для импорта календаря перезапустите сайт с новой сборкой сервера')
    const response=await api.groups.list();if(!response.ok)throw new Error(response.data?.message||'Не удалось загрузить группы')
    const XLSX=await import('xlsx'),buffer=await file.arrayBuffer()
    const sha=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',buffer)),b=>b.toString(16).padStart(2,'0')).join('')
    entries.value=parsePracticeCalendar(XLSX.read(buffer,{type:'array'}),response.data,XLSX,sha);filename.value=file.name
  }catch(e){error.value=e.message}finally{busy.value=false}
}

async function apply(){
  busy.value=true;error.value='';
  try{
    const r=await api.groups.importCalendar(entries.value);
    if(!r.ok)throw new Error(r.data?.message||'Ошибка сохранения');
    entries.value=[];
    uploader.value?.removeFile();
    emit('updated')
  }catch(e){error.value=e.message}finally{busy.value=false}
}
</script>
<style scoped>.calendar-import{padding:18px;margin-bottom:20px}.calendar-import h2{font-size:20px;margin:0}.calendar-import p{color:var(--text-secondary);font-size:13px}.calendar-import .error{color:var(--error)}.table-wrap{max-height:300px;margin:12px 0}</style>
