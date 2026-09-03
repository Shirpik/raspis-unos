<template>
  <div class="page">
    <div class="page-header">
      <h1 class="page-title">📖 Пары / Занятия</h1>
      <button class="btn btn-primary" @click="openAdd">+ Добавить</button>
    </div>

    <!-- Filters -->
    <div class="filters">
      <select v-model="filterGroup" class="form-select" style="max-width:220px">
        <option :value="-1">Все группы</option>
        <option v-for="g in store.groups" :key="g.id" :value="g.id">{{ g.name }}</option>
      </select>
    </div>

    <div v-if="loading" class="center-load"><span class="spinner spinner-lg" style="color:var(--accent)"/></div>

    <div v-else-if="filteredLessons.length === 0" class="empty-state">
      <span class="icon">📖</span>
      <h3>Нет занятий</h3>
      <p>Добавьте первое занятие</p>
    </div>

    <div v-else class="table-wrap">
      <table>
        <thead>
          <tr>
            <th>Предмет</th>
            <th>Группа</th>
            <th>Подгруппа</th>
            <th>Преподаватель</th>
            <th>Часов</th><th>Недели</th><th>Аудитория / требования</th>
            <th>Вид занятия</th>
            <th>Кампусы</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="l in filteredLessons" :key="l.id">
            <td><strong>{{ l.name }}</strong></td>
            <td>{{ groupName(l.group) }}</td>
            <td>{{ subgroupLabel(l.subgroup, l.group) }}</td>
            <td>{{ teacherName(l.teacher) }}</td>
            <td>{{ l.total_hours || l.total_slots * 2 }}</td>
            <td><span class="badge badge-accent">{{ parityLabel(l.week_parity) }}</span></td>
            <td>
              {{ roomName(l.fixed_room) }}
              <small v-if="l.allow_room_substitution !== false" class="auto-room">автозамена разрешена</small>
              <small v-if="roomRequirementSummary(l)">{{ roomRequirementSummary(l) }}</small>
            </td>
            <td>
              <span v-if="l.is_lab" class="badge badge-success">ЛПЗ</span>
              <span v-else-if="l.is_block" class="badge badge-warning">УП-блок</span>
              <span v-else class="badge badge-muted">Лекция</span>
            </td>
            <td>
              <span v-for="c in l.allowed_campuses" :key="c" class="chip" style="margin-right:3px">
                {{ c === 0 ? 'Лесная' : 'Кривоусова' }}
              </span>
            </td>
            <td>
              <div style="display:flex;gap:4px">
                <button class="btn btn-ghost btn-sm btn-icon" @click="openEdit(l)" title="Изменить">✏️</button>
                <button class="btn btn-ghost btn-sm btn-icon" style="color:var(--error)" @click="confirmDelete(l)" title="Удалить">🗑</button>
              </div>
            </td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- Form Modal -->
    <Modal v-model="modalOpen" :title="editItem ? 'Редактировать занятие' : 'Добавить занятие'">
      <div class="form-group">
        <label class="form-label">Название предмета</label>
        <input v-model="form.name" class="form-input" placeholder="Например: МДК.01.01" />
      </div>
      <div class="form-row">
        <div class="form-group">
          <label class="form-label">Группа</label>
          <select v-model.number="form.group" class="form-select" @change="onGroupChange">
            <option v-for="g in store.groups" :key="g.id" :value="g.id">{{ g.name }}</option>
          </select>
        </div>
        <div class="form-group">
          <label class="form-label">Подгруппа</label>
          <select v-model.number="form.subgroup" class="form-select">
            <option :value="-1">Вся группа</option>
            <option :value="form.group * 2">1-я подгруппа</option>
            <option :value="form.group * 2 + 1">2-я подгруппа</option>
          </select>
        </div>
      </div>
      <div class="form-row">
        <div class="form-group">
          <label class="form-label">Преподаватель</label>
          <select v-model.number="form.teacher" class="form-select">
            <option v-for="t in store.teachers" :key="t.id" :value="t.id">{{ t.name }}</option>
          </select>
        </div>
        <div class="form-group">
          <label class="form-label">Часов по плану</label>
          <input v-model.number="form.total_hours" type="number" min="1" max="1000" class="form-input" />
        </div>
      </div>
      <div class="form-row">
        <div class="form-group"><label class="form-label">Чётность недель</label><select v-model="form.week_parity" class="form-select"><option value="all">Каждую неделю</option><option value="odd">Только нечётные</option><option value="even">Только чётные</option></select></div>
        <div class="form-group"><label class="form-label">Закреплённая аудитория</label><select v-model.number="form.fixed_room" class="form-select"><option :value="-1">Не закреплена</option><option v-for="r in activeRooms" :key="r.id" :value="r.id">{{ r.name }} — {{ r.campus === 1 ? 'Кривоусова' : 'Лесная' }}</option></select></div>
      </div>
      <label class="form-checkbox substitution-option">
        <input v-model="form.allow_room_substitution" type="checkbox" />
        Если закреплённый кабинет занят, автоматически назначить свободный кабинет в том же корпусе
      </label>
      <div class="form-row">
        <div class="form-group"><label class="form-label">Требуемый тип аудитории</label><select v-model.number="form.required_room_type" class="form-select"><option :value="0">Любой тип</option><option v-for="type in store.roomTypes" :key="type.id" :value="type.id">{{ type.name }}</option></select></div>
        <div class="form-group"><label class="form-label">Минимальная вместимость</label><input v-model.number="form.required_capacity" type="number" min="0" class="form-input" placeholder="0 — по размеру группы" /></div>
      </div>
      <div class="form-group"><label class="form-label">Требуемое оборудование</label><input v-model="form.required_equipment_text" class="form-input" placeholder="Проектор, ПК, станки" /><small>Несколько позиций разделяйте запятыми.</small></div>
      <div class="form-row">
        <label class="form-checkbox">
          <input type="checkbox" v-model="form.is_lab" />
          Лаб. работа (ЛПЗ)
        </label>
        <label class="form-checkbox">
          <input type="checkbox" v-model="form.is_block" />
          УП-блок
        </label>
      </div>
      <div class="form-group">
        <label class="form-label">Разрешённые кампусы</label>
        <div class="campus-checks">
          <label class="form-checkbox">
            <input type="checkbox" :checked="form.allowed_campuses.includes(0)" @change="toggleCampus(0)" />
            Лесная (0)
          </label>
          <label class="form-checkbox">
            <input type="checkbox" :checked="form.allowed_campuses.includes(1)" @change="toggleCampus(1)" />
            Кривоусова (1)
          </label>
        </div>
      </div>
      <template #footer>
        <button class="btn btn-ghost" @click="modalOpen = false">Отмена</button>
        <button class="btn btn-primary" :disabled="saving || !form.name.trim()" @click="save">
          <span v-if="saving" class="spinner spinner-sm"/>
          {{ editItem ? 'Сохранить' : 'Добавить' }}
        </button>
      </template>
    </Modal>

    <Modal v-model="deleteModal" title="Удалить занятие?">
      <p style="color:var(--text-secondary)">Занятие <strong style="color:var(--text-primary)">{{ deleteTarget?.name }}</strong> будет удалено.</p>
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
import { ref, computed, onMounted } from 'vue'
import Modal from '../components/Modal.vue'
import { useDataStore } from '../stores/data.js'
import { useToast } from '../composables/useToast.js'
import { emptyLessonForm, lessonFormFromEntity, lessonPayloadFromForm } from '../utils/entityPayloads.js'

const store = useDataStore()
const toast = useToast()
const loading = ref(false)
const saving = ref(false)
const modalOpen = ref(false)
const deleteModal = ref(false)
const editItem = ref(null)
const deleteTarget = ref(null)
const filterGroup = ref(-1)

const form = ref(emptyLessonForm())

onMounted(async () => {
  loading.value = true
  await Promise.all([store.loadLessons(), store.loadGroups(), store.loadTeachers(), store.loadRooms(), store.loadRoomTypes()])
  if (store.groups.length) form.value.group = store.groups[0].id
  if (store.teachers.length) form.value.teacher = store.teachers[0].id
  loading.value = false
})

const filteredLessons = computed(() =>
  filterGroup.value === -1 ? store.lessons : store.lessons.filter(l => l.group === filterGroup.value)
)
const activeRooms = computed(() => store.rooms.filter(r => r.active !== false))

function groupName(id) { return store.groups.find(g => g.id === id)?.name ?? `Группа ${id}` }
function teacherName(id) { return store.teachers.find(t => t.id === id)?.name ?? `Преп. ${id}` }
function roomName(id) { return id >= 0 ? (store.rooms.find(r => r.id === id)?.name ?? `ID ${id}`) : '—' }
function roomRequirementSummary(lesson) {
  const parts = []
  if (lesson.required_room_type > 0) parts.push(store.roomTypes.find(type => type.id === lesson.required_room_type)?.name || `тип ${lesson.required_room_type}`)
  if (lesson.required_capacity > 0) parts.push(`от ${lesson.required_capacity} мест`)
  if (lesson.required_equipment?.length) parts.push(lesson.required_equipment.join(', '))
  return parts.join(' · ')
}
function parityLabel(value) { return value === 'odd' ? 'нечётные' : value === 'even' ? 'чётные' : 'все' }
function subgroupLabel(sub, group) {
  if (sub === -1) return 'Вся группа'
  if (sub === group * 2) return '1-я п/г'
  if (sub === group * 2 + 1) return '2-я п/г'
  return `п/г ${sub}`
}

function toggleCampus(c) {
  const arr = form.value.allowed_campuses
  const idx = arr.indexOf(c)
  if (idx >= 0) arr.splice(idx, 1)
  else arr.push(c)
}

function onGroupChange() { form.value.subgroup = -1 }

function openAdd() {
  editItem.value = null
  form.value = emptyLessonForm()
  if (store.groups.length) form.value.group = store.groups[0].id
  if (store.teachers.length) form.value.teacher = store.teachers[0].id
  modalOpen.value = true
}

function openEdit(l) {
  editItem.value = l
  form.value = lessonFormFromEntity(l)
  modalOpen.value = true
}

function confirmDelete(l) { deleteTarget.value = l; deleteModal.value = true }

async function save() {
  if (!form.value.name.trim()) return
  saving.value = true
  const d = lessonPayloadFromForm(form.value)
  const r = editItem.value ? await store.updateLesson(editItem.value.id, d) : await store.createLesson(d)
  saving.value = false
  if (r.ok) { toast.success(editItem.value ? 'Занятие обновлено' : 'Занятие добавлено'); modalOpen.value = false }
  else toast.error(r.data?.message || 'Ошибка')
}

async function doDelete() {
  saving.value = true
  const r = await store.deleteLesson(deleteTarget.value.id)
  saving.value = false
  if (r.ok) { toast.success('Занятие удалено'); deleteModal.value = false }
  else toast.error(r.data?.message || 'Ошибка')
}
</script>

<style scoped>
.center-load { display:flex; justify-content:center; padding:60px; }
.filters { margin-bottom: 16px; }
.form-row { display: flex; gap: 12px; }
.form-row > * { flex: 1; }
.campus-checks { display: flex; gap: 16px; }
.auto-room { display:block; margin-top:3px; color:var(--success); white-space:nowrap; }
.substitution-option { margin: 2px 0 14px; padding:10px 12px; border:1px solid var(--border); border-radius:8px; background:var(--bg-secondary); }
.form-group small { display:block; color:var(--text-muted); font-size:12px; margin-top:4px; }
@media (max-width: 500px) { .form-row { flex-direction: column; } }
</style>
