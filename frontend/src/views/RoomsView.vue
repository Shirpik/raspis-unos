<template>
  <div class="page">
    <div class="page-header">
      <div>
        <h1 class="page-title">🚪 Аудиторный фонд</h1>
        <p class="subtitle">Обычные аудитории доступны всем, персональные мастерские — только ответственным, закрытые помещения никогда не попадают в расписание.</p>
      </div>
      <div class="header-actions">
        <button class="btn btn-primary" @click="openAdd">+ Добавить аудиторию</button>
      </div>
    </div>

    <div class="summary card">
      <span><strong>{{ store.rooms.length }}</strong> аудиторий</span>
      <span><strong>{{ activeCount }}</strong> доступно</span>
      <span><strong>{{ exclusiveCount }}</strong> персональных</span>
      <span><strong>{{ blockedCount }}</strong> закрыто</span>
      <span><strong>{{ store.rooms.filter(r => r.campus === 0).length }}</strong> Лесная</span>
      <span><strong>{{ store.rooms.filter(r => r.campus === 1).length }}</strong> Кривоусова, 53</span>
    </div>

    <section v-if="allocationReport" class="allocation card">
      <div class="section-head">
        <div><h2>Последнее распределение кабинетов</h2><p class="subtitle">Отчёт обновляется после генерации расписания.</p></div>
        <button class="btn btn-ghost btn-sm" @click="loadAllocation">↻ Обновить</button>
      </div>
      <div class="allocation-metrics">
        <div><strong>{{ allocationReport.assigned || 0 }}</strong><span>назначено</span></div>
        <div class="replacement"><strong>{{ allocationReport.substituted || 0 }}</strong><span>автозамен</span></div>
        <div :class="{ error: allocationReport.unassigned }"><strong>{{ allocationReport.unassigned || 0 }}</strong><span>без кабинета</span></div>
      </div>
      <div v-if="allocationReport.substitutions?.length" class="table-wrap replacements">
        <table>
          <thead><tr><th>Дата / пара</th><th>Занятие</th><th>Запрашивался</th><th>Назначен</th><th>Причина</th></tr></thead>
          <tbody><tr v-for="(item, index) in allocationReport.substitutions" :key="`${item.lesson_id}-${item.date}-${item.slot}-${index}`">
            <td>{{ formatDate(item.date) }} · {{ item.slot }}-я</td><td>{{ item.lesson_name }}</td>
            <td>{{ item.requested_room_name || `ID ${item.requested_room_id}` }}</td>
            <td><span class="badge badge-success">{{ item.assigned_room_name }}</span></td><td>{{ item.reason }}</td>
          </tr></tbody>
        </table>
      </div>
      <p v-else class="subtitle report-note">Автоматических замен в последней генерации не было.</p>
    </section>

    <div v-if="loading" class="center-load"><span class="spinner spinner-lg" /></div>
    <div v-else-if="!store.rooms.length" class="empty-state card">
      <span class="icon">🏫</span><h3>Аудитории ещё не заполнены</h3>
      <p>Добавьте известные кабинеты и укажите корпус. Закрепление за преподавателем задаётся в разделе «Преподаватели».</p>
    </div>
    <div v-else class="table-wrap">
      <table>
        <thead><tr><th>Кабинет</th><th>Корпус</th><th>Тип / оснащение</th><th>Назначение</th><th>Режим</th><th>Ответственные</th><th>Вместимость</th><th>Доступные пары</th><th>Статус</th><th></th></tr></thead>
        <tbody>
          <tr v-for="room in store.rooms" :key="room.uid || room.id">
            <td><strong>{{ room.name }}</strong><div class="muted">ID {{ room.id }}</div></td>
            <td>{{ campusName(room.campus) }}</td>
            <td>{{ roomTypeName(room.room_type) }}<small v-if="room.equipment?.length">{{ room.equipment.join(', ') }}</small></td>
            <td><span :class="['badge', room.purpose === 'sports_hall' ? 'badge-warning' : 'badge-muted']">{{ purposeName(room.purpose) }}</span></td>
            <td><span :class="['badge', room.access_mode === 'exclusive' ? 'badge-warning' : room.access_mode === 'blocked' ? 'badge-muted' : 'badge-success']">{{ accessModeName(room.access_mode) }}</span></td>
            <td><span v-if="responsibleNames(room)" class="owner-list">{{ responsibleNames(room) }}</span><span v-else class="muted">не заданы</span></td>
            <td>{{ room.capacity || 'не указана' }}</td>
            <td><span v-if="room.available_slots?.length" class="badge badge-muted">{{ room.available_slots.join(', ') }}</span><span v-else class="muted">весь день</span></td>
            <td><span :class="['badge', room.active === false ? 'badge-muted' : 'badge-success']">{{ room.active === false ? 'не используется' : 'активна' }}</span></td>
            <td class="actions"><button class="btn btn-ghost btn-sm" @click="openEdit(room)">✏️</button><button class="btn btn-ghost btn-sm danger" @click="remove(room)">🗑</button></td>
          </tr>
        </tbody>
      </table>
    </div>

    <Modal v-model="modalOpen" :title="editItem ? 'Изменить аудиторию' : 'Новая аудитория'">
      <div class="form-grid">
        <div class="form-group"><label class="form-label">Номер / название</label><input v-model="form.name" class="form-input" placeholder="Например: 305 или Мастерская 2" /></div>
        <div class="form-group"><label class="form-label">Корпус</label><select v-model.number="form.campus" class="form-select"><option :value="0">Лесная</option><option :value="1">Кривоусова, 53</option></select></div>
        <div class="form-group"><label class="form-label">Вместимость</label><input v-model.number="form.capacity" type="number" min="0" class="form-input" placeholder="0 — неизвестно" /></div>
        <div class="form-group"><label class="form-label">Тип аудитории</label><select v-model.number="form.room_type" class="form-select"><option :value="0">Без специализации</option><option v-for="type in store.roomTypes" :key="type.id" :value="type.id">{{ type.name }}</option></select></div>
        <div class="form-group wide"><label class="form-label">Оборудование</label><input v-model="form.equipment_text" class="form-input" placeholder="Проектор, ПК, станки" /><small>Несколько позиций разделяйте запятыми. Недельные и календарные ограничения сохраняются при редактировании.</small></div>
        <div class="form-group"><label class="form-label">Назначение</label><select v-model="form.purpose" class="form-select"><option value="">Обычная учебная аудитория</option><option value="sports_hall">Спортивный зал — только физкультура</option></select></div>
        <div class="form-group"><label class="form-label">Режим использования</label><select v-model="form.access_mode" class="form-select"><option value="general">Обычная — можно подбирать всем</option><option value="exclusive">Персональная — только ответственным</option><option value="blocked">Закрыта — не использовать</option></select></div>
        <div class="form-group wide"><label class="form-label">Ответственные преподаватели</label><select v-model="form.responsible_teacher_ids" class="form-select owner-select" multiple size="6"><option v-for="teacher in store.teachers" :key="teacher.id" :value="teacher.id">{{ teacher.name }}</option></select><small>Для персональной аудитории хотя бы один ответственный должен быть выбран. Если сотрудника нет в системе, аудитория останется недоступной для всех.</small></div>
        <div class="form-group wide"><label class="form-label">Доступные пары</label><div class="slot-picker"><label v-for="slot in 7" :key="slot" class="form-checkbox"><input v-model="form.available_slots" type="checkbox" :value="slot" /> {{ slot }}</label></div><small>Если ничего не выбрано — кабинет доступен весь учебный день.</small></div>
      </div>
      <label v-if="form.access_mode !== 'blocked'" class="form-checkbox"><input v-model="form.active" type="checkbox" /> Аудитория доступна для расписания</label>
      <template #footer><button class="btn btn-ghost" @click="modalOpen=false">Отмена</button><button class="btn btn-primary" :disabled="saving || !form.name.trim()" @click="save">{{ saving ? 'Сохраняю…' : 'Сохранить' }}</button></template>
    </Modal>
  </div>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import Modal from '../components/Modal.vue'
import { api } from '../api/index.js'
import { useDataStore } from '../stores/data.js'
import { useToast } from '../composables/useToast.js'
import { emptyRoomForm, roomFormFromEntity, roomPayloadFromForm } from '../utils/entityPayloads.js'

const store = useDataStore()
const toast = useToast()
const loading = ref(true), saving = ref(false), modalOpen = ref(false), editItem = ref(null)
const allocationReport = ref(null)
const form = ref(emptyRoomForm())
const activeCount = computed(() => store.rooms.filter(r => r.active !== false && r.access_mode !== 'blocked').length)
const exclusiveCount = computed(() => store.rooms.filter(r => r.access_mode === 'exclusive').length)
const blockedCount = computed(() => store.rooms.filter(r => r.access_mode === 'blocked' || r.active === false).length)

onMounted(async () => {
  await Promise.all([store.loadRooms(), store.loadTeachers(), store.loadRoomTypes()])
  await loadAllocation()
  loading.value = false
})

const campusName = c => c === 1 ? 'Кривоусова, 53' : 'Лесная'
const roomTypeName = id => id > 0 ? (store.roomTypes.find(type => type.id === id)?.name || `Тип ${id}`) : 'без специализации'
const purposeName = purpose => purpose === 'sports_hall' ? 'спортзал' : 'обычная'
const accessModeName = mode => mode === 'exclusive' ? 'только ответственным' : mode === 'blocked' ? 'закрыта' : 'обычная'
const responsibleNames = room => (room.responsible_teacher_ids || []).map(id => store.teachers.find(t => t.id === id)?.name).filter(Boolean).join(', ') || room.responsible_note || ''
const formatDate = iso => iso ? new Date(`${iso}T00:00:00`).toLocaleDateString('ru-RU') : '—'
async function loadAllocation() { const r = await api.schedule.rooms(); allocationReport.value = r.ok ? r.data : null }
function openAdd() { editItem.value = null; form.value = emptyRoomForm(); modalOpen.value = true }
function openEdit(r) { editItem.value = r; form.value = roomFormFromEntity(r); modalOpen.value = true }
async function save() {
  saving.value = true
  const data = roomPayloadFromForm(form.value)
  const r = editItem.value ? await store.updateRoom(editItem.value.id, data) : await store.createRoom(data)
  saving.value = false
  if (r.ok) { modalOpen.value = false; toast.success('Аудитория сохранена') } else toast.error(r.data?.message || 'Ошибка сохранения')
}
async function remove(room) {
  if (!confirm(`Удалить аудиторию «${room.name}»? История данных позволит выполнить откат.`)) return
  const r = await store.deleteRoom(room.id)
  if (r.ok) toast.success('Аудитория удалена'); else toast.error(r.data?.message || 'Ошибка удаления')
}
</script>

<style scoped>
.subtitle,.muted,small{color:var(--text-muted);font-size:12px}.header-actions{display:flex;gap:8px;flex-wrap:wrap}.summary{display:flex;gap:28px;margin-bottom:18px;flex-wrap:wrap}.summary strong{color:var(--accent);font-size:20px;margin-right:4px}.center-load{display:flex;justify-content:center;padding:60px}.chip{margin:2px}.actions{display:flex;gap:4px;margin-left:auto}.danger{color:var(--error)}.form-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px;margin-bottom:14px}.wide{grid-column:1/-1}.slot-picker{display:flex;gap:12px;flex-wrap:wrap;margin-bottom:5px}.slot-picker .form-checkbox{margin:0}.owner-list{font-size:12px;line-height:1.35}.owner-select{min-height:130px}.type-code{display:inline-flex;align-items:center;justify-content:center;min-width:28px;height:28px;border-radius:7px;background:var(--accent-light);color:var(--accent);font-weight:700;margin-right:8px}.allocation{margin-bottom:18px}.section-head{display:flex;justify-content:space-between;gap:12px}.section-head h2{font-size:17px}.allocation-metrics{display:flex;gap:10px;margin-top:12px;flex-wrap:wrap}.allocation-metrics>div{min-width:130px;padding:10px 14px;border-radius:9px;background:var(--bg-secondary);display:flex;flex-direction:column}.allocation-metrics strong{font-size:22px}.allocation-metrics span{font-size:12px;color:var(--text-muted)}.allocation-metrics .replacement{border:1px solid var(--warning)}.allocation-metrics .error{border:1px solid var(--error)}.replacements{margin-top:14px}.report-note{margin-top:12px}.type-help{margin-bottom:14px}.type-list{display:flex;flex-direction:column;gap:8px;max-height:280px;overflow:auto}.type-row{display:flex;align-items:center;gap:4px;padding:10px;background:var(--bg-secondary);border-radius:9px}.type-row>div:nth-child(2){flex:1}.type-row p{font-size:12px;color:var(--text-muted);margin-top:2px}.type-editor{border-top:1px solid var(--border);margin-top:16px;padding-top:14px}.type-editor h3{font-size:15px;margin-bottom:10px}.type-form{display:grid;grid-template-columns:100px 1fr;gap:10px}.editor-actions{display:flex;justify-content:flex-end;gap:8px;margin-top:12px}@media(max-width:700px){.form-grid,.type-form{grid-template-columns:1fr}.wide{grid-column:auto}.section-head{flex-direction:column}}
</style>
