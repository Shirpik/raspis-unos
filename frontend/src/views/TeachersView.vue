<template>
  <div class="page">
    <div class="page-header">
      <div><h1 class="page-title">👤 Преподаватели</h1><p class="page-help">Выберите несколько преподавателей, чтобы назначить им общий рабочий график.</p></div>
      <div class="header-actions"><button class="btn btn-secondary" :disabled="!selected.length" @click="openBulk">⏱ Рабочее время ({{ selected.length }})</button><button class="btn btn-primary" @click="openAdd">+ Добавить</button></div>
    </div>

    <div v-if="store.teachers.length" class="teacher-toolbar card">
      <input v-model.trim="search" class="form-input" placeholder="Поиск по ФИО или закреплению кабинета" />
      <select v-model.number="campusFilter" class="form-select"><option :value="-2">Все площадки</option><option :value="-1">Без приоритета</option><option :value="0">Лесная</option><option :value="1">Кривоусова, 53</option></select>
      <button class="btn btn-secondary" :disabled="!filteredTeachers.length" @click="toggleVisible">{{ allVisibleSelected ? 'Снять найденных' : `Выделить найденных (${filteredTeachers.length})` }}</button>
      <button v-if="selected.length" class="btn btn-ghost" @click="selected=[]">Сбросить выбор</button>
    </div>

    <div v-if="selected.length" class="selection-bar">
      <strong>Выбрано: {{ selected.length }}</strong><span>Настройки применятся ко всем выбранным преподавателям одним сохранением.</span><button class="btn btn-primary btn-sm" @click="openBulk">Настроить рабочее время</button>
    </div>

    <div v-if="loading" class="center-load"><span class="spinner spinner-lg" style="color:var(--accent)"/></div>

    <div v-else-if="store.teachers.length === 0" class="empty-state">
      <span class="icon">👤</span>
      <h3>Нет преподавателей</h3>
      <p>Добавьте первого преподавателя</p>
    </div>

    <div v-else-if="!filteredTeachers.length" class="empty-state card"><h3>Ничего не найдено</h3><p>Измените строку поиска или фильтр площадки.</p></div>

    <div v-else class="cards-grid">
      <div v-for="t in filteredTeachers" :key="t.id" :class="['teacher-card','card',{selected:selected.includes(t.id)}]">
        <div class="tc-body">
          <input v-model="selected" type="checkbox" :value="t.id" class="entity-check" />
          <div class="tc-avatar">{{ initials(t.name) }}</div>
          <div class="tc-info">
            <div class="tc-name">{{ t.name }}</div>
            <div class="tc-id">ID: {{ t.id }}</div>
            <div class="tc-id">{{ workSummary(t) }} · {{ campusName(t.campus_priority?.[0]) }}</div>
            <div class="tc-id campus-lock">Разрешено: {{ allowedCampusSummary(t) }}</div>
            <div class="tc-id">{{ workDaysSummary(t) }}</div>
            <div v-if="t.availability_note" class="availability-note">{{ t.availability_note }}</div>
          </div>
        </div>
        <div class="tc-actions">
          <button class="btn btn-ghost btn-sm" @click="openEdit(t)">✏️ Изменить</button>
          <button class="btn btn-ghost btn-sm" style="color:var(--error)" @click="confirmDelete(t)">🗑 Удалить</button>
        </div>
      </div>
    </div>

    <!-- Add/Edit modal -->
    <Modal v-model="modalOpen" :title="editItem ? 'Редактировать преподавателя' : 'Добавить преподавателя'">
      <div class="form-group">
        <label class="form-label">ФИО преподавателя</label>
        <input v-model="form.name" class="form-input" placeholder="Например: Иванов И.И." @keyup.enter="save" />
      </div>
      <div class="form-group"><label class="form-label">Закреплённый кабинет</label><select v-model.number="form.default_room" class="form-select"><option :value="-1">Не задан</option><option v-for="r in availableRooms" :key="r.id" :value="r.id">{{ r.name }} — {{ campusName(r.campus) }}</option></select></div>
      <div class="form-group"><label class="form-label">Приоритет площадки</label><select v-model.number="form.preferred_campus" class="form-select"><option :value="-1">Без приоритета</option><option :value="0">Лесная</option><option :value="1">Кривоусова, 53</option></select></div>
      <div class="form-group"><label class="form-label">Разрешённые площадки (жёстко)</label><div class="campus-checks"><label class="form-checkbox"><input v-model="form.allowed_campuses" type="checkbox" :value="0" /> Лесная</label><label class="form-checkbox"><input v-model="form.allowed_campuses" type="checkbox" :value="1" /> Кривоусова, 53</label></div><small>Решатель никогда не поставит преподавателя на неотмеченную площадку.</small></div>
      <div class="form-group"><label class="form-label">Закрепление из аудиторного фонда</label><textarea v-model="form.room_responsibility" class="form-input" rows="2" placeholder="Например: 409 (Лесная, 1)" /></div>
      <div class="form-group"><label class="form-label">Примечание по доступности</label><textarea v-model="form.availability_note" class="form-input" rows="2" placeholder="Источник и расшифровка ограничения" /></div>
      <WorkScheduleEditor :schedule="form" />
      <template #footer>
        <button class="btn btn-ghost" @click="modalOpen = false">Отмена</button>
        <button class="btn btn-primary" :disabled="saving || !form.name.trim()" @click="save">
          <span v-if="saving" class="spinner spinner-sm"/>
          {{ editItem ? 'Сохранить' : 'Добавить' }}
        </button>
      </template>
    </Modal>

    <Modal v-model="bulkModal" title="Настройки выбранных преподавателей">
      <p class="bulk-note">Изменения будут применены сразу к {{ selected.length }} преподавателям. Отметьте только те поля, которые нужно заменить.</p>
      <div class="template-row">
        <select v-model.number="bulkTemplateId" class="form-select"><option :value="-1">Новый общий график</option><option v-for="t in selectedTeachers" :key="t.id" :value="t.id">Взять график: {{ t.name }}</option></select>
        <button class="btn btn-secondary" :disabled="bulkTemplateId<0" @click="loadBulkTemplate">Загрузить</button>
      </div>
      <div class="bulk-options">
        <label><input v-model="bulkApply.period" type="checkbox" /> Рабочие даты</label>
        <label><input v-model="bulkApply.days" type="checkbox" /> Дни и пары</label>
        <label><input v-model="bulkApply.campus" type="checkbox" /> Приоритет площадки</label>
        <label><input v-model="bulkApply.allowedCampuses" type="checkbox" /> Разрешённые площадки</label>
        <label><input v-model="bulkApply.room" type="checkbox" /> Закреплённый кабинет</label>
      </div>
      <div v-if="bulkApply.campus" class="form-group"><label class="form-label">Приоритет площадки</label><select v-model.number="bulkForm.preferred_campus" class="form-select"><option :value="-1">Без приоритета</option><option :value="0">Лесная</option><option :value="1">Кривоусова, 53</option></select></div>
      <div v-if="bulkApply.allowedCampuses" class="form-group"><label class="form-label">Разрешённые площадки (жёстко)</label><div class="campus-checks"><label class="form-checkbox"><input v-model="bulkForm.allowed_campuses" type="checkbox" :value="0" /> Лесная</label><label class="form-checkbox"><input v-model="bulkForm.allowed_campuses" type="checkbox" :value="1" /> Кривоусова, 53</label></div></div>
      <div v-if="bulkApply.room" class="form-group"><label class="form-label">Закреплённый кабинет</label><select v-model.number="bulkForm.default_room" class="form-select"><option :value="-1">Не задан</option><option v-for="r in availableRooms" :key="r.id" :value="r.id">{{ r.name }} — {{ campusName(r.campus) }}</option></select></div>
      <WorkScheduleEditor v-if="bulkApply.period || bulkApply.days" :schedule="bulkForm" />
      <template #footer><button class="btn btn-ghost" @click="bulkModal=false">Отмена</button><button class="btn btn-primary" :disabled="saving || !hasBulkChanges" @click="saveBulk">Применить к {{ selected.length }}</button></template>
    </Modal>

    <!-- Delete confirm -->
    <Modal v-model="deleteModal" title="Удалить преподавателя?">
      <p style="color:var(--text-secondary)">Преподаватель <strong style="color:var(--text-primary)">{{ deleteTarget?.name }}</strong> будет удалён. Это действие нельзя отменить.</p>
      <template #footer>
        <button class="btn btn-ghost" @click="deleteModal = false">Отмена</button>
        <button class="btn btn-danger" :disabled="saving" @click="doDelete">
          <span v-if="saving" class="spinner spinner-sm"/>
          Удалить
        </button>
      </template>
    </Modal>
  </div>
</template>

<script setup>
import { computed, ref, onMounted } from 'vue'
import Modal from '../components/Modal.vue'
import WorkScheduleEditor from '../components/WorkScheduleEditor.vue'
import { useDataStore } from '../stores/data.js'
import { useToast } from '../composables/useToast.js'

const store = useDataStore()
const toast = useToast()
const availableRooms = computed(() => store.rooms.filter(room => room.active !== false && room.access_mode !== 'blocked'))
const loading = ref(false)
const saving = ref(false)
const modalOpen = ref(false)
const deleteModal = ref(false)
const editItem = ref(null)
const deleteTarget = ref(null)
const selected=ref([]),bulkModal=ref(false)
const search=ref(''),campusFilter=ref(-2),bulkTemplateId=ref(-1)
const defaultDays=()=>Array.from({length:7},(_,i)=>({day:i+1,enabled:i<6,start_slot:1,end_slot:7,slots:i<6?Array.from({length:7},(_,j)=>j+1):[]}))
const baseSchedule=()=>({work_period:{from:'',to:''},work_days:defaultDays()})
const form = ref({ name: '',default_room:-1,preferred_campus:-1,allowed_campuses:[0,1],room_responsibility:'',availability_note:'',...baseSchedule() })
const bulkForm=ref({preferred_campus:-1,allowed_campuses:[0,1],default_room:-1,...baseSchedule()})
const bulkApply=ref({period:true,days:true,campus:false,allowedCampuses:false,room:false})
const filteredTeachers=computed(()=>{const q=search.value.toLocaleLowerCase('ru');return store.teachers.filter(t=>(campusFilter.value===-2||(t.campus_priority?.[0]??-1)===campusFilter.value)&&(!q||`${t.name} ${t.room_responsibility||''} ${t.availability_note||''}`.toLocaleLowerCase('ru').includes(q)))})
const selectedTeachers=computed(()=>store.teachers.filter(t=>selected.value.includes(t.id)))
const allVisibleSelected=computed(()=>filteredTeachers.value.length>0&&filteredTeachers.value.every(t=>selected.value.includes(t.id)))
const hasBulkChanges=computed(()=>Object.values(bulkApply.value).some(Boolean))

onMounted(async () => {
  loading.value = true
  await Promise.all([store.loadTeachers(),store.loadRooms()])
  loading.value = false
})

function initials(name) {
  return name.split(/\s+/).map(w => w[0]).join('').slice(0, 2).toUpperCase()
}

function openAdd() {
  editItem.value = null
  form.value = { name: '',default_room:-1,preferred_campus:-1,allowed_campuses:[0,1],room_responsibility:'',availability_note:'',...baseSchedule() }
  modalOpen.value = true
}

function openEdit(t) {
  editItem.value = t
  form.value = { name: t.name,default_room:t.default_room??-1,preferred_campus:t.campus_priority?.[0]??-1,allowed_campuses:t.allowed_campuses?.length?[...t.allowed_campuses]:[0,1],room_responsibility:t.room_responsibility||'',availability_note:t.availability_note||'',work_period:{from:t.work_period?.from||'',to:t.work_period?.to||''},work_days:(t.work_days||defaultDays()).map(d=>({...d})) }
  modalOpen.value = true
}
function toggleVisible(){const visible=filteredTeachers.value.map(t=>t.id);selected.value=allVisibleSelected.value?selected.value.filter(id=>!visible.includes(id)):[...new Set([...selected.value,...visible])]}
function openBulk(){if(!selected.value.length)return;bulkForm.value={preferred_campus:-1,allowed_campuses:[0,1],default_room:-1,...baseSchedule()};bulkApply.value={period:true,days:true,campus:false,allowedCampuses:false,room:false};bulkTemplateId.value=-1;bulkModal.value=true}
function loadBulkTemplate(){const t=store.teachers.find(x=>x.id===bulkTemplateId.value);if(!t)return;bulkForm.value={preferred_campus:t.campus_priority?.[0]??-1,allowed_campuses:t.allowed_campuses?.length?[...t.allowed_campuses]:[0,1],default_room:t.default_room??-1,work_period:{from:t.work_period?.from||'',to:t.work_period?.to||''},work_days:(t.work_days||defaultDays()).map(d=>({...d}))};toast.success('График загружен как шаблон')}
const campusName=id=>id===0?'Лесная':id===1?'Кривоусова, 53':'без приоритета'
const allowedCampusSummary=t=>{const c=t.allowed_campuses?.length?t.allowed_campuses:[0,1];return c.map(campusName).join(' + ')}
const workSummary=t=>t.work_period?.from&&t.work_period?.to?`${t.work_period.from} — ${t.work_period.to}`:'весь семестр'
const workDaysSummary=t=>{const days=(t.work_days||defaultDays()).filter(d=>d.enabled);if(!days.length)return'нет рабочих дней';const names=['','ПН','ВТ','СР','ЧТ','ПТ','СБ','ВС'];const signatures=[...new Set(days.map(d=>(d.slots?.length?d.slots:Array.from({length:d.end_slot-d.start_slot+1},(_,i)=>d.start_slot+i)).join(',')))];return`${days.map(d=>names[d.day]).join(', ')} · пары ${signatures.join('; ')}`}

function confirmDelete(t) {
  deleteTarget.value = t
  deleteModal.value = true
}

async function save() {
  if (!form.value.name.trim()) return
  saving.value = true
  let r
  if (editItem.value) {
    r = await store.updateTeacher(editItem.value.id, teacherPayload())
  } else {
    r = await store.createTeacher(teacherPayload())
  }
  saving.value = false
  if (r.ok) {
    toast.success(editItem.value ? 'Преподаватель обновлён' : 'Преподаватель добавлен')
    modalOpen.value = false
  } else {
    toast.error(r.data?.message || 'Ошибка сохранения')
  }
}
function teacherPayload(){const pref=form.value.preferred_campus;return{name:form.value.name.trim(),default_room:form.value.default_room,room_responsibility:form.value.room_responsibility.trim(),availability_note:form.value.availability_note.trim(),campus_priority:pref<0?[]:[pref,pref===0?1:0],allowed_campuses:[...form.value.allowed_campuses],work_period:form.value.work_period,work_days:form.value.work_days}}
async function saveBulk(){if(!hasBulkChanges.value)return;const patch={};if(bulkApply.value.period)patch.work_period={...bulkForm.value.work_period};if(bulkApply.value.days)patch.work_days=bulkForm.value.work_days.map(d=>({...d}));if(bulkApply.value.campus){const p=bulkForm.value.preferred_campus;patch.campus_priority=p<0?[]:[p,p===0?1:0]}if(bulkApply.value.allowedCampuses)patch.allowed_campuses=[...bulkForm.value.allowed_campuses];if(bulkApply.value.room)patch.default_room=bulkForm.value.default_room;saving.value=true;const count=selected.value.length;const r=await store.bulkUpdateTeachers(selected.value,patch);saving.value=false;if(r.ok){toast.success(`Настройки применены к ${count} преподавателям`);bulkModal.value=false}else toast.error(r.data?.message||'Ошибка')}

async function doDelete() {
  saving.value = true
  const r = await store.deleteTeacher(deleteTarget.value.id)
  saving.value = false
  if (r.ok) {
    toast.success('Преподаватель удалён')
    deleteModal.value = false
  } else {
    toast.error(r.data?.message || 'Ошибка удаления')
  }
}
</script>

<style scoped>
.center-load { display:flex; justify-content:center; padding:60px; }.header-actions{display:flex;gap:8px;flex-wrap:wrap}.entity-check{width:17px;height:17px;flex-shrink:0}.bulk-note,.page-help{color:var(--text-secondary);font-size:14px}.page-help{margin-top:4px}.teacher-toolbar{display:grid;grid-template-columns:minmax(260px,1fr) 200px auto auto;gap:8px;margin-bottom:12px}.selection-bar{position:sticky;top:8px;z-index:4;display:flex;align-items:center;gap:12px;padding:10px 14px;margin-bottom:12px;border:1px solid var(--accent);background:var(--accent-light);border-radius:10px}.selection-bar span{flex:1;color:var(--text-secondary);font-size:13px}.template-row{display:grid;grid-template-columns:1fr auto;gap:8px;margin-bottom:14px}.bulk-options{display:grid;grid-template-columns:1fr 1fr;gap:8px;padding:12px;background:var(--bg-secondary);border:1px solid var(--border);border-radius:9px;margin-bottom:14px}.bulk-options label{display:flex;gap:8px;align-items:center;font-size:14px}
.cards-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(260px, 1fr)); gap: 14px; }
.teacher-card { display: flex; flex-direction: column; gap: 12px; }.teacher-card.selected{border-color:var(--accent);box-shadow:0 0 0 1px var(--accent)}
.tc-body { display: flex; align-items: center; gap: 12px; }
.tc-avatar {
  width: 44px; height: 44px; border-radius: 50%;
  background: var(--accent-light); color: var(--accent);
  display: flex; align-items: center; justify-content: center;
  font-size: 15px; font-weight: 700; flex-shrink: 0;
}
.tc-name { font-weight: 600; font-size: 15px; }
.tc-id { font-size: 12px; color: var(--text-muted); }.campus-lock{color:var(--text-secondary)}.campus-checks{display:flex;gap:18px;flex-wrap:wrap}.campus-checks .form-checkbox{margin:0}
.availability-note{margin-top:5px;font-size:11px;line-height:1.35;color:var(--warning)}
.tc-actions { display: flex; gap: 8px; border-top: 1px solid var(--border); padding-top: 10px; }
@media(max-width:850px){.teacher-toolbar{grid-template-columns:1fr}.selection-bar{align-items:flex-start;flex-direction:column}.template-row,.bulk-options{grid-template-columns:1fr}}
</style>
