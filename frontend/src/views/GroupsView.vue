<template>
  <div class="page">
    <div class="page-header">
      <h1 class="page-title">🎓 Группы</h1>
      <div class="header-actions"><button v-if="store.groups.length" class="btn btn-secondary" @click="toggleAll">{{ allSelected ? 'Снять выделение' : 'Выделить все' }}</button><button v-if="selected.length" class="btn btn-secondary" @click="openBulk">⏱ Рабочее время ({{ selected.length }})</button><button class="btn btn-primary" @click="openAdd">+ Добавить</button></div>
    </div>

    <div v-if="loading" class="center-load"><span class="spinner spinner-lg" style="color:var(--accent)"/></div>

    <div v-else-if="store.groups.length === 0" class="empty-state">
      <span class="icon">🎓</span>
      <h3>Нет групп</h3>
      <p>Добавьте первую группу</p>
    </div>

    <div v-else class="cards-grid">
      <div v-for="g in store.groups" :key="g.id" class="group-card card">
        <div class="gc-top">
          <input v-model="selected" type="checkbox" :value="g.id" class="entity-check" />
          <span class="gc-name">{{ g.name }}</span>
          <span class="badge badge-accent">ID {{ g.id }}</span>
        </div>
        <div class="gc-meta">
          <span class="chip">{{ g.parts === 1 ? '1 подгруппа' : '2 подгруппы' }}</span>
          <span class="chip">{{ g.size > 0 ? `${g.size} студентов` : 'численность не указана' }}</span>
          <span class="chip">{{ g.home_campus === 1 ? 'Кривоусова, 53' : 'Лесная' }}</span>
          <span class="chip">{{ curatorName(g.curator_teacher) }}</span>
          <span v-if="g.curator_teacher >= 0 && g.class_hour_enabled !== false" class="chip class-hour-chip">ПН 07:50 · классный час</span>
          <span class="chip">{{ workSummary(g) }}</span>
        </div>
        <div class="gc-actions">
          <button class="btn btn-ghost btn-sm" @click="openEdit(g)">✏️ Изменить</button>
          <button class="btn btn-ghost btn-sm" style="color:var(--error)" @click="confirmDelete(g)">🗑 Удалить</button>
        </div>
      </div>
    </div>

    <Modal v-model="modalOpen" :title="editItem ? 'Редактировать группу' : 'Добавить группу'">
      <div class="form-group">
        <label class="form-label">Название группы</label>
        <input v-model="form.name" class="form-input" placeholder="Например: ИСП-3306" />
      </div>
      <WorkScheduleEditor :schedule="form" />
      <div class="form-group">
        <label class="form-label">Численность группы</label>
        <input v-model.number="form.size" type="number" min="0" class="form-input" placeholder="0 — пока неизвестна" />
      </div>
      <div class="form-group">
        <label class="form-label">Основной корпус</label>
        <select v-model.number="form.home_campus" class="form-select"><option :value="0">Лесная</option><option :value="1">Кривоусова, 53</option></select>
      </div>
      <div class="class-hour-box">
        <div class="class-hour-head"><strong>Нулевой урок — классный час</strong><label><input v-model="form.class_hour_enabled" type="checkbox" /> включён</label></div>
        <p>Фиксированно по понедельникам с 07:50 до 09:15. Обычные пары в понедельник начинаются с 09:15; решатель этот урок не рассчитывает.</p>
        <div class="form-group">
          <label class="form-label">Куратор группы</label>
          <select v-model.number="form.curator_teacher" class="form-select">
            <option :value="-1">Не назначен</option>
            <option v-for="t in store.teachers" :key="t.id" :value="t.id">{{ t.name }}</option>
          </select>
        </div>
        <div class="form-group">
          <label class="form-label">Площадка классного часа</label>
          <select v-model.number="form.class_hour_campus" class="form-select"><option :value="-1">Основной корпус группы</option><option :value="0">Лесная</option><option :value="1">Кривоусова, 53</option></select>
        </div>
      </div>
      <div class="form-group">
        <label class="form-label">Количество подгрупп</label>
        <select v-model.number="form.parts" class="form-select">
          <option :value="1">1 подгруппа (без деления)</option>
          <option :value="2">2 подгруппы</option>
        </select>
      </div>
      <template #footer>
        <button class="btn btn-ghost" @click="modalOpen = false">Отмена</button>
        <button class="btn btn-primary" :disabled="saving || !form.name.trim()" @click="save">
          <span v-if="saving" class="spinner spinner-sm"/>
          {{ editItem ? 'Сохранить' : 'Добавить' }}
        </button>
      </template>
    </Modal>

    <Modal v-model="bulkModal" title="Рабочее время выбранных групп">
      <p class="bulk-note">Настройки будут применены к {{ selected.length }} группам. Названия, корпуса и численность не изменятся.</p>
      <WorkScheduleEditor :schedule="bulkForm" />
      <template #footer><button class="btn btn-ghost" @click="bulkModal=false">Отмена</button><button class="btn btn-primary" :disabled="saving" @click="saveBulk">Применить</button></template>
    </Modal>

    <Modal v-model="deleteModal" title="Удалить группу?">
      <p style="color:var(--text-secondary)">Группа <strong style="color:var(--text-primary)">{{ deleteTarget?.name }}</strong> будет удалена.</p>
      <template #footer>
        <button class="btn btn-ghost" @click="deleteModal = false">Отмена</button>
        <button class="btn btn-danger" :disabled="saving" @click="doDelete">
          <span v-if="saving" class="spinner spinner-sm"/>Удалить
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
const loading = ref(false)
const saving = ref(false)
const modalOpen = ref(false)
const deleteModal = ref(false)
const editItem = ref(null)
const deleteTarget = ref(null)
const selected=ref([]), bulkModal=ref(false)
const defaultDays=()=>Array.from({length:7},(_,i)=>({day:i+1,enabled:i<6,start_slot:1,end_slot:7}))
const baseSchedule=()=>({work_period:{from:'',to:''},work_days:defaultDays()})
const form = ref({ name: '', parts: 2, size: 0, home_campus: 0, curator_teacher: -1, class_hour_enabled: true, class_hour_campus: -1, ...baseSchedule() })
const bulkForm=ref(baseSchedule())
const allSelected=computed(()=>store.groups.length>0&&selected.value.length===store.groups.length)

onMounted(async () => { loading.value = true; await Promise.all([store.loadGroups(), store.loadTeachers()]); loading.value = false })

const scheduleOf=e=>({work_period:{from:e.work_period?.from||'',to:e.work_period?.to||''},work_days:(e.work_days||defaultDays()).map(d=>({...d}))})
function openAdd() { editItem.value = null; form.value = { name: '', parts: 2, size: 0, home_campus: 0, curator_teacher: -1, class_hour_enabled: true, class_hour_campus: -1, ...baseSchedule() }; modalOpen.value = true }
function openEdit(g) { editItem.value = g; form.value = { name: g.name, parts: g.parts, size: g.size || 0, home_campus: g.home_campus ?? 0, curator_teacher: g.curator_teacher ?? -1, class_hour_enabled: g.class_hour_enabled !== false, class_hour_campus: g.class_hour_campus ?? -1, ...scheduleOf(g) }; modalOpen.value = true }
function toggleAll(){selected.value=allSelected.value?[]:store.groups.map(g=>g.id)}
function openBulk(){bulkForm.value=baseSchedule();bulkModal.value=true}
const workSummary=g=>g.work_period?.from&&g.work_period?.to?`${g.work_period.from} — ${g.work_period.to}`:'весь семестр'
const curatorName=id=>id>=0?`Куратор: ${store.teachers.find(t=>t.id===id)?.name||`ID ${id}`}`:'куратор не назначен'
function confirmDelete(g) { deleteTarget.value = g; deleteModal.value = true }

async function save() {
  if (!form.value.name.trim()) return
  saving.value = true
  const d = { name: form.value.name.trim(), parts: form.value.parts, size: form.value.size || 0, home_campus: form.value.home_campus, curator_teacher: form.value.curator_teacher, class_hour_enabled: form.value.class_hour_enabled, class_hour_campus: form.value.class_hour_campus, class_hour_weekday: 1, class_hour_slot: 0, class_hour_from: '07:50', class_hour_to: '09:15', work_period:form.value.work_period, work_days:form.value.work_days }
  const r = editItem.value ? await store.updateGroup(editItem.value.id, d) : await store.createGroup(d)
  saving.value = false
  if (r.ok) { toast.success(editItem.value ? 'Группа обновлена' : 'Группа добавлена'); modalOpen.value = false }
  else toast.error(r.data?.message || 'Ошибка')
}
async function saveBulk(){saving.value=true;const r=await store.bulkUpdateGroups(selected.value,{work_period:bulkForm.value.work_period,work_days:bulkForm.value.work_days});saving.value=false;if(r.ok){toast.success(`Рабочее время применено к ${selected.value.length} группам`);bulkModal.value=false}else toast.error(r.data?.message||'Ошибка')}

async function doDelete() {
  saving.value = true
  const r = await store.deleteGroup(deleteTarget.value.id)
  saving.value = false
  if (r.ok) { toast.success('Группа удалена'); deleteModal.value = false }
  else toast.error(r.data?.message || 'Ошибка')
}
</script>

<style scoped>
.center-load { display:flex; justify-content:center; padding:60px; }
.cards-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(220px, 1fr)); gap: 14px; }
.group-card { display: flex; flex-direction: column; gap: 10px; }
.header-actions{display:flex;gap:8px;flex-wrap:wrap}.gc-top { display: flex; align-items: center; gap:8px; }.gc-top .badge{margin-left:auto}.entity-check{width:17px;height:17px}.bulk-note{color:var(--text-secondary);font-size:14px}
.gc-name { font-size: 18px; font-weight: 700; }
.class-hour-chip{color:#8b5cf6;border-color:rgba(139,92,246,.35)}
.class-hour-box{padding:14px;border:1px solid rgba(139,92,246,.35);border-radius:12px;background:rgba(139,92,246,.07);margin:12px 0}.class-hour-box p{margin:8px 0 14px;color:var(--text-secondary);font-size:13px;line-height:1.45}.class-hour-head{display:flex;justify-content:space-between;gap:14px;align-items:center}.class-hour-head label{display:flex;gap:7px;align-items:center;font-size:13px;color:var(--text-secondary)}
.gc-meta { }
.gc-actions { display: flex; gap: 8px; border-top: 1px solid var(--border); padding-top: 10px; }
</style>
