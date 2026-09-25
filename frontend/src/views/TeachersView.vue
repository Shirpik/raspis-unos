<template>
  <div class="page">
    <div class="page-header">
      <div>
        <div class="page-title-wrapper">
          <Users :size="24" />
          <h1 class="page-title">Преподаватели</h1>
        </div>
        <p class="page-help">Выберите несколько преподавателей, чтобы назначить им общий рабочий график.</p>
      </div>
      <div class="header-actions">
        <button class="btn btn-secondary" :disabled="!selected.length" @click="openBulk">
          <Clock :size="18" />
          <span>Рабочее время ({{ selected.length }})</span>
        </button>
        <button class="btn btn-primary" @click="openAdd">
          <Plus :size="18" />
          <span>Добавить</span>
        </button>
      </div>
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

    <div v-if="loading" class="loading-state">
      <div class="table-wrap">
        <table>
          <thead>
            <tr>
              <th></th>
              <th>Преподаватель</th>
              <th>Корпус</th>
              <th>Разрешённые площадки</th>
              <th>Закреплённый кабинет</th>
              <th>Рабочий период</th>
              <th>Рабочие дни</th>
              <th></th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="i in 8" :key="i">
              <td><Skeleton class="w-4 h-4" /></td>
              <td><Skeleton class="w-48 h-4" /></td>
              <td><Skeleton class="w-28 h-4" /></td>
              <td><Skeleton class="w-32 h-4" /></td>
              <td><Skeleton class="w-24 h-4" /></td>
              <td><Skeleton class="w-32 h-4" /></td>
              <td><Skeleton class="w-36 h-4" /></td>
              <td><Skeleton class="w-20 h-4" /></td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>

    <div v-else-if="store.teachers.length === 0" class="empty-state card">
      <Users :size="64" style="opacity: 0.3" />
      <h3>Нет преподавателей</h3>
      <p>Добавьте первого преподавателя</p>
    </div>

    <div v-else-if="!filteredTeachers.length" class="empty-state card"><h3>Ничего не найдено</h3><p>Измените строку поиска или фильтр площадки.</p></div>

    <div v-else class="table-wrap">
      <table>
        <thead>
          <tr>
            <th><input v-model="allVisibleSelected" type="checkbox" @change="toggleVisible" /></th>
            <th>Преподаватель</th>
            <th>Корпус</th>
            <th>Разрешённые площадки</th>
            <th>Закреплённый кабинет</th>
            <th>Рабочий период</th>
            <th>Рабочие дни</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="t in filteredTeachers" :key="t.id" :class="{selected:selected.includes(t.id)}">
            <td><input v-model="selected" type="checkbox" :value="t.id" class="entity-check" /></td>
            <td>
              <div class="teacher-info">
                <div class="tc-avatar-sm">{{ initials(t.name) }}</div>
                <div>
                  <strong>{{ t.name }}</strong>
                  <div class="muted">ID: {{ t.id }}</div>
                  <div v-if="t.availability_note" class="availability-note-inline">{{ t.availability_note }}</div>
                </div>
              </div>
            </td>
            <td>{{ campusName(t.campus_priority?.[0]) }}</td>
            <td>{{ allowedCampusSummary(t) }}</td>
            <td>
              <span v-if="t.default_room >= 0">{{ availableRooms.find(r=>r.id===t.default_room)?.name || `ID ${t.default_room}` }}</span>
              <span v-else class="muted">не задан</span>
              <small v-if="t.room_responsibility" class="room-note">{{ t.room_responsibility }}</small>
            </td>
            <td>{{ workSummary(t) }}</td>
            <td><small>{{ workDaysSummary(t) }}</small></td>
            <td class="actions">
              <button class="btn btn-ghost btn-sm" @click="openEdit(t)">
                <Edit2 :size="16" />
              </button>
              <button class="btn btn-ghost btn-sm danger" @click="confirmDelete(t)">
                <Trash2 :size="16" />
              </button>
            </td>
          </tr>
        </tbody>
      </table>
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
      <div class="form-row">
        <div class="form-group"><label class="form-label">Максимум рабочих дней в неделю</label><input v-model.number="form.max_work_days_per_week" type="number" min="0" max="7" class="form-input" /><small>0 — без отдельного ограничения.</small></div>
        <div class="form-group"><label class="form-label">Максимум пар в день</label><input v-model.number="form.max_pairs_per_day" type="number" min="0" max="7" class="form-input" /><small>0 — без отдельного ограничения.</small></div>
      </div>
      <WorkScheduleEditor :schedule="form" />
      <DateLoadEditor v-model="form.date_load_targets" />
      <label class="form-checkbox"><input v-model="form.scheduling_active" type="checkbox" /> Включать преподавателя в генерацию (часы при отключении сохраняются)</label>
      <DesiredLoadEditor v-model="form.desired_load_rules" :groups="store.groups" />
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
        <label><input v-model="bulkApply.overrides" type="checkbox" /> Заменить исключения по датам</label>
        <label><input v-model="bulkApply.campus" type="checkbox" /> Приоритет площадки</label>
        <label><input v-model="bulkApply.allowedCampuses" type="checkbox" /> Разрешённые площадки</label>
        <label><input v-model="bulkApply.room" type="checkbox" /> Закреплённый кабинет</label>
      </div>
      <div v-if="bulkApply.campus" class="form-group"><label class="form-label">Приоритет площадки</label><select v-model.number="bulkForm.preferred_campus" class="form-select"><option :value="-1">Без приоритета</option><option :value="0">Лесная</option><option :value="1">Кривоусова, 53</option></select></div>
      <div v-if="bulkApply.allowedCampuses" class="form-group"><label class="form-label">Разрешённые площадки (жёстко)</label><div class="campus-checks"><label class="form-checkbox"><input v-model="bulkForm.allowed_campuses" type="checkbox" :value="0" /> Лесная</label><label class="form-checkbox"><input v-model="bulkForm.allowed_campuses" type="checkbox" :value="1" /> Кривоусова, 53</label></div></div>
      <div v-if="bulkApply.room" class="form-group"><label class="form-label">Закреплённый кабинет</label><select v-model.number="bulkForm.default_room" class="form-select"><option :value="-1">Не задан</option><option v-for="r in availableRooms" :key="r.id" :value="r.id">{{ r.name }} — {{ campusName(r.campus) }}</option></select></div>
      <WorkScheduleEditor v-if="bulkApply.period || bulkApply.days || bulkApply.overrides" :schedule="bulkForm" />
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
import { Users, Clock, Plus, Edit2, Trash2 } from 'lucide-vue-next'
import Modal from '../components/Modal.vue'
import WorkScheduleEditor from '../components/WorkScheduleEditor.vue'
import DesiredLoadEditor from '../components/DesiredLoadEditor.vue'
import DateLoadEditor from '../components/DateLoadEditor.vue'
import Skeleton from '../components/ui/Skeleton.vue'
import { emptyTeacherForm, teacherFormFromEntity, teacherPayloadFromForm, teacherBulkPayload } from '../utils/entityPayloads.js'
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
const form = ref(emptyTeacherForm())
const bulkForm=ref({preferred_campus:-1,allowed_campuses:[0,1],default_room:-1,...baseSchedule()})
const bulkApply=ref({period:true,days:true,campus:false,allowedCampuses:false,room:false})
const filteredTeachers=computed(()=>{const q=search.value.toLocaleLowerCase('ru');return store.teachers.filter(t=>(campusFilter.value===-2||(t.campus_priority?.[0]??-1)===campusFilter.value)&&(!q||`${t.name} ${t.room_responsibility||''} ${t.availability_note||''}`.toLocaleLowerCase('ru').includes(q)))})
const selectedTeachers=computed(()=>store.teachers.filter(t=>selected.value.includes(t.id)))
const allVisibleSelected=computed({
  get: ()=>filteredTeachers.value.length>0&&filteredTeachers.value.every(t=>selected.value.includes(t.id)),
  set: ()=>toggleVisible()
})
const hasBulkChanges=computed(()=>Object.values(bulkApply.value).some(Boolean))

onMounted(async () => {
  loading.value = true
  await Promise.all([store.loadTeachers(),store.loadRooms(),store.loadGroups()])
  loading.value = false
})

function initials(name) {
  return name.split(/\s+/).map(w => w[0]).join('').slice(0, 2).toUpperCase()
}

function openAdd() {
  editItem.value = null
  form.value = emptyTeacherForm()
  modalOpen.value = true
}

function openEdit(t) {
  editItem.value = t
  form.value = teacherFormFromEntity(t)
  modalOpen.value = true
}
function toggleVisible(){const visible=filteredTeachers.value.map(t=>t.id);selected.value=allVisibleSelected.value?selected.value.filter(id=>!visible.includes(id)):[...new Set([...selected.value,...visible])]}
function openBulk(){if(!selected.value.length)return;bulkForm.value={preferred_campus:-1,allowed_campuses:[0,1],default_room:-1,...baseSchedule()};bulkApply.value={period:true,days:true,campus:false,allowedCampuses:false,room:false};bulkTemplateId.value=-1;bulkModal.value=true}
function loadBulkTemplate(){const t=store.teachers.find(x=>x.id===bulkTemplateId.value);if(!t)return;bulkForm.value=teacherFormFromEntity(t);toast.success('График загружен как шаблон')}
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
function teacherPayload(){return teacherPayloadFromForm(form.value)}
async function saveBulk(){if(!hasBulkChanges.value)return;const patch=teacherBulkPayload(bulkForm.value,bulkApply.value);saving.value=true;const count=selected.value.length;const r=await store.bulkUpdateTeachers(selected.value,patch);saving.value=false;if(r.ok){toast.success(`Настройки применены к ${count} преподавателям`);bulkModal.value=false}else toast.error(r.data?.message||'Ошибка')}

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
.center-load { display:flex; justify-content:center; padding:60px; }.header-actions{display:flex;gap:8px;flex-wrap:wrap}.entity-check{width:17px;height:17px;flex-shrink:0}.bulk-note,.page-help{color:var(--text-secondary);font-size:14px}.page-help{margin-top:4px}.teacher-toolbar{display:grid;grid-template-columns:minmax(260px,1fr) 200px auto auto;gap:8px;margin-bottom:12px}.selection-bar{position:sticky;top:8px;z-index:4;display:flex;align-items:center;gap:12px;padding:10px 14px;margin-bottom:12px;border:1px solid var(--accent);background:var(--accent-light);border-radius:10px}.selection-bar span{flex:1;color:var(--text-secondary);font-size:13px}.template-row{display:grid;grid-template-columns:1fr auto;gap:8px;margin-bottom:14px}.bulk-options{display:grid;grid-template-columns:1fr 1fr;gap:8px;padding:12px;background:var(--bg-secondary);border:1px solid var(--border);border-radius:9px;margin-bottom:14px}.bulk-options label{display:flex;gap:8px;align-items:center;font-size:14px}.form-row{display:grid;grid-template-columns:1fr 1fr;gap:10px}.form-group small{display:block;color:var(--text-muted);font-size:12px;margin-top:4px}
.muted{color:var(--text-muted);font-size:12px}
.teacher-info{display:flex;align-items:center;gap:12px}
.tc-avatar-sm{width:36px;height:36px;border-radius:50%;background:var(--accent-light);color:var(--accent);display:flex;align-items:center;justify-content:center;font-size:13px;font-weight:700;flex-shrink:0}
.availability-note-inline{margin-top:3px;font-size:11px;line-height:1.35;color:var(--warning)}
.room-note{display:block;margin-top:3px;color:var(--text-muted);font-size:11px}
.actions{display:flex;gap:4px;margin-left:auto}.danger{color:var(--error)}
.campus-checks{display:flex;gap:18px;flex-wrap:wrap}.campus-checks .form-checkbox{margin:0}
tr.selected{background:var(--accent-light);border-color:var(--accent)}
@media(max-width:850px){.teacher-toolbar{grid-template-columns:1fr}.selection-bar{align-items:flex-start;flex-direction:column}.template-row,.bulk-options{grid-template-columns:1fr}}
</style>
