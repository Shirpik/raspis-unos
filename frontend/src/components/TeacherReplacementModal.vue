<template>
  <Modal :model-value="true" :title="`Временная замена · ${teacher.name}`" @update:model-value="value => { if (!value && !saving) $emit('close') }">
    <p>Выберите конкретный предмет группы и даты включительно. Заменяющий будет учитываться решателем со своей занятостью и ограничениями. Учебная нагрузка и зачёт часов остаются у основного преподавателя.</p>
    <p v-if="loading">Загружаю предметы…</p>
    <template v-else>
      <label class="form-label">Предмет и группа</label>
      <select v-model.number="lessonId" class="form-select" :disabled="saving">
        <option :value="-1">Выберите предмет</option>
        <option v-for="lesson in lessons" :key="lesson.id" :value="lesson.id">{{ groupName(lesson.group) }} · {{ lesson.name }} · {{ lesson.is_lab ? 'ЛПЗ' : 'теория' }} · {{ lesson.subgroup < 0 ? 'вся группа' : `подгруппа ${lesson.subgroup % 2 + 1}` }} (ID {{ lesson.id }})</option>
      </select>
      <p v-if="!lessons.length">У преподавателя нет предметов. Назначьте нагрузку в разделе «Занятия».</p>
      <template v-if="currentLesson">
        <p v-if="!periods.length">Временные замены для этого предмета не назначены.</p>
        <div v-for="(p, index) in periods" :key="index" class="replacement-period">
          <label>С даты<input v-model="p.from" class="form-input" type="date" :disabled="saving" /></label>
          <label>По дату<input v-model="p.to" class="form-input" type="date" :min="p.from" :disabled="saving" /></label>
          <label class="replacement-teacher">Заменяет<select v-model.number="p.teacher" class="form-select" :disabled="saving"><option :value="-1">Выберите преподавателя</option><option v-for="t in substitutes" :key="t.id" :value="t.id">{{ t.name }}</option></select></label>
          <button class="btn btn-ghost" :disabled="saving" @click="periods.splice(index, 1)">Удалить</button>
        </div>
        <button class="btn btn-secondary" :disabled="saving" @click="periods.push({from:'',to:'',teacher:-1})">Добавить период замены</button>
        <p>После окончания периода решатель снова назначает основного преподавателя. Чтобы отменить замену, удалите период и сохраните. Изменения применяются при новой генерации; сохранённое расписание и проведённые пары не переписываются.</p>
      </template>
    </template>
    <template #footer><button class="btn btn-ghost" :disabled="saving" @click="$emit('close')">Закрыть</button><button class="btn btn-primary" :disabled="saving || !currentLesson" @click="save">{{ saving ? 'Сохраняю…' : 'Сохранить замены' }}</button></template>
  </Modal>
</template>
<script setup>
import { computed, onMounted, ref, watch } from 'vue'
import Modal from './Modal.vue'
import { api } from '../api/index.js'
import { useDataStore } from '../stores/data.js'
import { useToast } from '../composables/useToast.js'
import { replacementPeriodsError } from '../utils/teacherReplacements.js'
const props = defineProps({teacher:{type:Object,required:true}})
const emit = defineEmits(['close','saved'])
const store=useDataStore(), toast=useToast()
const loading=ref(true),saving=ref(false),lessonId=ref(-1),periods=ref([])
const lessons=computed(()=>store.lessons.filter(l=>l.teacher===props.teacher.id))
const substitutes=computed(()=>store.teachers.filter(t=>t.id!==props.teacher.id && t.scheduling_active!==false))
const currentLesson=computed(()=>lessons.value.find(l=>l.id===lessonId.value))
const groupName=id=>store.groups.find(g=>g.id===id)?.name || `Группа ${id}`
watch(lessonId,()=>{periods.value=(currentLesson.value?.teacher_replacements||[]).map(p=>({...p}))})
onMounted(async()=>{try{await Promise.all([store.loadLessons(),store.loadGroups()])}finally{loading.value=false}})
async function save(){
  const error=replacementPeriodsError(periods.value,props.teacher.id,store.teachers)
  if(error){toast.error(error);return}
  saving.value=true
  try{
    const r=await api.lessons.patch(lessonId.value,{teacher_replacements:periods.value.map(p=>({...p}))})
    if(r.ok){await store.loadLessons();toast.success('Периоды замен сохранены. Перегенерируйте нужные даты.');emit('saved')}
    else toast.error(r.data?.message||'Не удалось сохранить замены')
  }catch{toast.error('Не удалось сохранить замены')}finally{saving.value=false}
}
</script>
<style scoped>
p{font-size:13px;color:var(--text-secondary);line-height:1.5;margin:12px 0}.replacement-period{display:flex;gap:8px;flex-wrap:wrap;padding:12px 0;border-bottom:1px solid var(--border);margin-bottom:12px}.replacement-period label{flex:1;font-size:13px;min-width:150px}.replacement-teacher{flex-basis:100%!important}
</style>
