<template>
  <div class="page">
    <div class="page-header">
      <div class="page-title-wrapper">
        <GraduationCap :size="24" />
        <h1 class="page-title">Группы</h1>
      </div>
      <div class="header-actions">
        <button v-if="store.groups.length" class="btn btn-secondary" @click="toggleAll">
          {{ allSelected ? 'Снять выделение' : 'Выделить все' }}
        </button>
        <button v-if="selected.length" class="btn btn-secondary" @click="openBulk">
          <Clock :size="18" />
          <span>Рабочее время ({{ selected.length }})</span>
        </button>
        <button class="btn btn-primary" @click="openAdd">
          <Plus :size="18" />
          <span>Добавить</span>
        </button>
      </div>
    </div>

    <PracticeCalendarImport @updated="calendarUpdated" />
    <PracticeReadout :refresh-key="readoutRevision" />
    <div v-if="loading" class="loading-state">
      <div class="table-wrap">
        <table>
          <thead>
            <tr>
              <th></th>
              <th>Группа</th>
              <th>Подгруппы</th>
              <th>Численность</th>
              <th>Корпус</th>
              <th>Куратор</th>
              <th>Рабочий период</th>
              <th></th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="i in 8" :key="i">
              <td><Skeleton class="w-4 h-4" /></td>
              <td><Skeleton class="w-32 h-4" /></td>
              <td><Skeleton class="w-24 h-4" /></td>
              <td><Skeleton class="w-20 h-4" /></td>
              <td><Skeleton class="w-28 h-4" /></td>
              <td><Skeleton class="w-36 h-4" /></td>
              <td><Skeleton class="w-32 h-4" /></td>
              <td><Skeleton class="w-20 h-4" /></td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>

    <div v-else-if="store.groups.length === 0" class="empty-state card">
      <GraduationCap :size="64" style="opacity: 0.3" />
      <h3>Нет групп</h3>
      <p>Добавьте первую группу</p>
    </div>

    <div v-else class="table-wrap">
      <table>
        <thead>
          <tr>
            <th><input v-model="allSelected" type="checkbox" @change="toggleAll" /></th>
            <th>Группа</th>
            <th>Подгруппы</th>
            <th>Численность</th>
            <th>Корпус</th>
            <th>Куратор</th>
            <th>Рабочий период</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="g in store.groups" :key="g.id">
            <td><input v-model="selected" type="checkbox" :value="g.id" class="entity-check" /></td>
            <td>
              <strong>{{ g.name }}</strong>
              <div class="muted">ID {{ g.id }}</div>
            </td>
            <td>{{ g.parts === 1 ? '1 подгруппа' : '2 подгруппы' }}</td>
            <td>{{ g.size > 0 ? g.size : '—' }}</td>
            <td>{{ g.home_campus === 1 ? 'Кривоусова, 53' : 'Лесная' }}</td>
            <td>
              <span v-if="g.curator_teacher >= 0">{{ store.teachers.find(t=>t.id===g.curator_teacher)?.name || `ID ${g.curator_teacher}` }}</span>
              <span v-else class="muted">не назначен</span>
              <small v-if="g.curator_teacher >= 0 && g.class_hour_enabled !== false" class="class-hour-note">классный час: ПН 07:50</small>
            </td>
            <td>
              {{ workSummary(g) }}
              <small v-for="(period,index) in g.practice_periods || []" :key="index" class="practice-note">ПП: {{ period.from }} — {{ period.to }}</small>
              <small v-if="g.teaching_deadline" class="deadline-note">Вычитать до {{ g.teaching_deadline }}</small>
            </td>
            <td class="actions">
              <button class="btn btn-ghost btn-sm" @click="openEdit(g)">
                <Edit2 :size="16" />
              </button>
              <button class="btn btn-ghost btn-sm danger" @click="confirmDelete(g)">
                <Trash2 :size="16" />
              </button>
            </td>
          </tr>
        </tbody>
      </table>
    </div>

    <Modal v-model="modalOpen" :title="editItem ? 'Редактировать группу' : 'Добавить группу'">
      <div class="form-group">
        <label class="form-label">Название группы</label>
        <input v-model="form.name" class="form-input" placeholder="Например: ИСП-3306" />
      </div>
      <WorkScheduleEditor :schedule="form" />
      <PracticeCalendarEditor v-model="form.practice_periods" />
      <details v-if="form.academic_calendar?.length"><summary>Учебные недели из XLSX ({{ form.academic_calendar.length }})</summary><p class="bulk-note">ПП из исходного календаря также ограничивает срок вычитки. Для переноса этих дат загрузите исправленный календарь. Ручной срок может ускорить вычитку.</p><div class="table-wrap" style="max-height:240px"><table><thead><tr><th>Неделя</th><th>Теория/ЛПЗ, ч</th><th>УП, ч</th><th>ПП, ч</th><th>Экзамены, ч</th><th>Каникулы</th></tr></thead><tbody><tr v-for="w in form.academic_calendar" :key="w.from"><td>{{ w.from }} — {{ w.to }}</td><td>{{ w.theory_hours }}</td><td>{{ w.up_hours }}</td><td>{{ w.pp_hours }}</td><td>{{ w.exam_hours }}</td><td>{{ w.vacation?'Да':'' }}</td></tr></tbody></table></div></details>
      <div class="form-group"><label class="form-label">Дополнительный срок вычитки (необязательно)</label><input v-model="form.teaching_deadline" type="date" class="form-input" /><small>Используется самый ранний срок: эта дата, начало практики или конец семестра.</small></div>
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
import { GraduationCap, Clock, Plus, Edit2, Trash2 } from 'lucide-vue-next'
import Modal from '../components/Modal.vue'
import WorkScheduleEditor from '../components/WorkScheduleEditor.vue'
import PracticeCalendarEditor from '../components/PracticeCalendarEditor.vue'
import PracticeReadout from '../components/PracticeReadout.vue'
import PracticeCalendarImport from '../components/PracticeCalendarImport.vue'
import Skeleton from '../components/ui/Skeleton.vue'
import { useDataStore } from '../stores/data.js'
import { useToast } from '../composables/useToast.js'

const store = useDataStore()
const toast = useToast()
const loading = ref(false)
const saving = ref(false)
const readoutRevision = ref(0)
const modalOpen = ref(false)
const deleteModal = ref(false)
const editItem = ref(null)
const deleteTarget = ref(null)
const selected=ref([]), bulkModal=ref(false)
const defaultDays=()=>Array.from({length:7},(_,i)=>({day:i+1,enabled:i<6,start_slot:1,end_slot:7}))
const baseSchedule=()=>({work_period:{from:'',to:''},work_days:defaultDays()})
const form = ref({ name: '', parts: 2, size: 0, home_campus: 0, curator_teacher: -1, class_hour_enabled: true, class_hour_campus: -1, ...baseSchedule() })
const bulkForm=ref(baseSchedule())
const allSelected=computed({
  get: ()=>store.groups.length>0&&selected.value.length===store.groups.length,
  set: (val)=>{selected.value=val?store.groups.map(g=>g.id):[]}
})
async function calendarUpdated(){await store.loadGroups();readoutRevision.value++;toast.success('Календарь обновлён. Предыдущая версия базы сохранена.')}

onMounted(async () => { loading.value = true; await Promise.all([store.loadGroups(), store.loadTeachers()]); loading.value = false })

const scheduleOf=e=>({work_period:{from:e.work_period?.from||'',to:e.work_period?.to||''},work_days:(e.work_days||defaultDays()).map(d=>({...d}))})
function openAdd() { editItem.value = null; form.value = { name: '', parts: 2, size: 0, home_campus: 0, curator_teacher: -1, class_hour_enabled: true, class_hour_campus: -1, practice_periods: [], teaching_deadline: '', ...baseSchedule() }; modalOpen.value = true }
function openEdit(g) { editItem.value = g; form.value = { ...JSON.parse(JSON.stringify(g)), ...scheduleOf(g), practice_periods: (g.practice_periods||[]).map(p=>({...p})), teaching_deadline: g.teaching_deadline || '' }; modalOpen.value = true }
function toggleAll(){selected.value=allSelected.value?[]:store.groups.map(g=>g.id)}
function openBulk(){bulkForm.value=baseSchedule();bulkModal.value=true}
const workSummary=g=>g.work_period?.from&&g.work_period?.to?`${g.work_period.from} — ${g.work_period.to}`:'весь семестр'
function confirmDelete(g) { deleteTarget.value = g; deleteModal.value = true }

async function save() {
  if (!form.value.name.trim()) return
  if ((form.value.practice_periods||[]).some(p=>!p.from||!p.to||p.to<p.from)) { toast.error('Укажите корректные даты практики'); return }
  saving.value = true
  const d = { name: form.value.name.trim(), parts: form.value.parts, size: form.value.size || 0, home_campus: form.value.home_campus, curator_teacher: form.value.curator_teacher, class_hour_enabled: form.value.class_hour_enabled, class_hour_campus: form.value.class_hour_campus, class_hour_weekday: 1, class_hour_slot: 0, class_hour_from: '07:50', class_hour_to: '09:15', work_period:form.value.work_period, work_days:form.value.work_days }
  d.practice_periods = form.value.practice_periods || []
  d.teaching_deadline = form.value.teaching_deadline || ''
  d.date_slot_overrides = form.value.date_slot_overrides || []
  const r = editItem.value ? await store.updateGroup(editItem.value.id, d) : await store.createGroup(d)
  saving.value = false
  if (r.ok) { toast.success(editItem.value ? 'Группа обновлена' : 'Группа добавлена'); modalOpen.value = false; readoutRevision.value++ }
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
.header-actions{display:flex;gap:8px;flex-wrap:wrap}.entity-check{width:17px;height:17px}.bulk-note{color:var(--text-secondary);font-size:14px}
.class-hour-chip{color:#8b5cf6;border-color:rgba(139,92,246,.35)}
.class-hour-box{padding:14px;border:1px solid rgba(139,92,246,.35);border-radius:12px;background:rgba(139,92,246,.07);margin:12px 0}.class-hour-box p{margin:8px 0 14px;color:var(--text-secondary);font-size:13px;line-height:1.45}.class-hour-head{display:flex;justify-content:space-between;gap:14px;align-items:center}.class-hour-head label{display:flex;gap:7px;align-items:center;font-size:13px;color:var(--text-secondary)}
.muted{color:var(--text-muted);font-size:12px}
.class-hour-note{display:block;margin-top:3px;color:#8b5cf6;font-size:11px}
.practice-note{display:block;margin-top:3px;color:var(--text-muted);font-size:11px}
.deadline-note{display:block;margin-top:3px;color:var(--warning);font-size:11px}
.actions{display:flex;gap:4px;margin-left:auto}.danger{color:var(--error)}
</style>
