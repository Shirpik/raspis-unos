<template>
  <div class="teacher-notify">
    <div class="search-bar">
      <input
        v-model.trim="searchQuery"
        class="form-input"
        placeholder="Поиск по ФИО или фамилии"
      />
      <button
        v-if="selectedTeacher"
        class="btn btn-secondary"
        @click="clearSelection"
      >
        Сбросить выбор
      </button>
    </div>

    <div v-if="!selectedTeacher && filteredTeachers.length > 0" class="teacher-list">
      <div
        v-for="teacher in filteredTeachers"
        :key="teacher.id"
        class="teacher-item"
        @click="selectTeacher(teacher)"
      >
        <div class="teacher-avatar">{{ initials(teacher.name) }}</div>
        <div class="teacher-info">
          <div class="teacher-name">{{ teacher.name }}</div>
          <div class="teacher-id">ID: {{ teacher.id }}</div>
        </div>
      </div>
    </div>

    <div v-else-if="!selectedTeacher && searchQuery && filteredTeachers.length === 0" class="empty-state">
      <Search :size="48" style="opacity: 0.3" />
      <h3>Преподаватель не найден</h3>
      <p>Измените строку поиска</p>
    </div>

    <div v-else-if="!selectedTeacher" class="empty-state">
      <User :size="48" style="opacity: 0.3" />
      <h3>Введите ФИО преподавателя</h3>
      <p>Используйте поиск выше</p>
    </div>

    <div v-if="selectedTeacher" class="notify-content">
      <div class="notify-header">
        <h2>{{ selectedTeacher.name }}</h2>
      </div>

      <div class="notify-form card">
        <h3 class="form-title">
          <Send :size="20" />
          Сообщить о недоступности
        </h3>
        <p class="form-help">Укажите даты и причину отсутствия. Диспетчер рассмотрит ваше сообщение.</p>

        <div class="form-row">
          <div class="form-group">
            <label class="form-label">Дата начала</label>
            <input v-model="form.dateFrom" type="date" class="form-input" />
          </div>
          <div class="form-group">
            <label class="form-label">Дата окончания</label>
            <input v-model="form.dateTo" type="date" class="form-input" />
          </div>
        </div>

        <div class="form-row">
          <div class="form-group">
            <label class="form-label">Время начала (необязательно)</label>
            <input v-model="form.timeFrom" type="time" class="form-input" />
          </div>
          <div class="form-group">
            <label class="form-label">Время окончания (необязательно)</label>
            <input v-model="form.timeTo" type="time" class="form-input" />
          </div>
        </div>

        <div class="form-group">
          <label class="form-label">Причина</label>
          <textarea
            v-model="form.reason"
            class="form-input"
            rows="4"
            placeholder="Опишите причину отсутствия"
          />
        </div>

        <div class="form-group">
          <label class="form-label">Прикрепить документ (необязательно)</label>
          <FileUploader
            ref="uploader"
            accept="image/*,.pdf"
            accept-label="Изображение или PDF документ"
            :show-upload-button="false"
            @file-selected="onFileSelect"
          />
        </div>

        <div class="form-actions">
          <button class="btn btn-primary" :disabled="!isFormValid || saving" @click="submitNotification">
            <span v-if="saving" class="spinner spinner-sm" />
            Отправить
          </button>
          <button class="btn btn-ghost" @click="resetForm">Очистить</button>
        </div>
      </div>

      <div class="history-section">
        <h3 class="section-title">
          <History :size="20" />
          История сообщений
        </h3>

        <div v-if="loading" class="center-block">
          <span class="spinner spinner-lg" style="color: var(--accent)" />
        </div>

        <div v-else-if="notifications.length === 0" class="empty-state card">
          <Inbox :size="48" style="opacity: 0.3" />
          <h3>Сообщений пока нет</h3>
        </div>

        <div v-else class="notifications-list">
          <div
            v-for="notif in notifications"
            :key="notif.id"
            class="notification-card card"
            :class="'status-' + notif.status"
          >
            <div class="notif-header">
              <span class="notif-status">{{ statusLabel(notif.status) }}</span>
              <span class="notif-date">{{ formatDateTime(notif.created_at) }}</span>
            </div>
            <div class="notif-body">
              <p class="notif-period">
                <strong>Период:</strong> {{ formatDate(notif.date_from) }} — {{ formatDate(notif.date_to) }}
                <span v-if="notif.time_from && notif.time_to">
                  ({{ notif.time_from }} — {{ notif.time_to }})
                </span>
              </p>
              <p class="notif-reason"><strong>Причина:</strong> {{ notif.reason }}</p>
              <p v-if="notif.attachment_url" class="notif-attachment">
                <Paperclip :size="16" />
                <a :href="notif.attachment_url" target="_blank">Просмотреть документ</a>
              </p>
              <p v-if="notif.dispatcher_note" class="notif-note">
                <strong>Комментарий диспетчера:</strong> {{ notif.dispatcher_note }}
              </p>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch } from 'vue'
import { useDataStore } from '../stores/data.js'
import { User, Search, Send, History, Inbox, Paperclip } from 'lucide-vue-next'
import FileUploader from './FileUploader.vue'
import { api } from '../api/index.js'
import { useToast } from '../composables/useToast.js'

const dataStore = useDataStore()
const toast = useToast()

const searchQuery = ref('')
const selectedTeacher = ref(null)
const notifications = ref([])
const loading = ref(false)
const saving = ref(false)
const uploader = ref(null)

const form = ref({
  dateFrom: '',
  dateTo: '',
  timeFrom: '',
  timeTo: '',
  reason: '',
  fileName: '',
  fileData: null,
})

const filteredTeachers = computed(() => {
  if (!searchQuery.value) return []
  const q = searchQuery.value.toLowerCase()
  return dataStore.teachers.filter(t => t.name.toLowerCase().includes(q))
})

const isFormValid = computed(() => {
  return form.value.dateFrom && form.value.dateTo && form.value.reason.trim()
})

function initials(name) {
  return name.split(/\s+/).map(w => w[0]).join('').slice(0, 2).toUpperCase()
}

function selectTeacher(teacher) {
  selectedTeacher.value = teacher
  loadNotifications()
}

function clearSelection() {
  selectedTeacher.value = null
  searchQuery.value = ''
  notifications.value = []
  resetForm()
}

function resetForm() {
  form.value = {
    dateFrom: '',
    dateTo: '',
    timeFrom: '',
    timeTo: '',
    reason: '',
    fileName: '',
    fileData: null,
  }
  uploader.value?.removeFile()
}

function onFileSelect(file) {
  if (!file) {
    form.value.fileName = ''
    form.value.fileData = null
    return
  }

  form.value.fileName = file.name

  const reader = new FileReader()
  reader.onload = () => {
    form.value.fileData = reader.result
  }
  reader.readAsDataURL(file)
}

async function loadNotifications() {
  if (!selectedTeacher.value) return
  loading.value = true

  const res = await api.teacherNotifications.list(selectedTeacher.value.id)
  loading.value = false

  if (res.ok) {
    notifications.value = res.data || []
  }
}

async function submitNotification() {
  if (!isFormValid.value || !selectedTeacher.value) return

  saving.value = true

  const payload = {
    teacher_id: selectedTeacher.value.id,
    teacher_name: selectedTeacher.value.name,
    date_from: form.value.dateFrom,
    date_to: form.value.dateTo,
    time_from: form.value.timeFrom || null,
    time_to: form.value.timeTo || null,
    reason: form.value.reason.trim(),
    attachment_data: form.value.fileData,
    attachment_name: form.value.fileName,
  }

  const res = await api.teacherNotifications.create(payload)
  saving.value = false

  if (res.ok) {
    toast.success('Сообщение отправлено диспетчеру')
    resetForm()
    loadNotifications()
  } else {
    toast.error(res.data?.message || 'Ошибка отправки')
  }
}

function statusLabel(status) {
  const map = {
    pending: 'Ожидает рассмотрения',
    approved: 'Принято',
    rejected: 'Отклонено',
  }
  return map[status] || status
}

function formatDate(dateStr) {
  if (!dateStr) return ''
  const [y, m, d] = dateStr.split('-')
  return `${d}.${m}.${y}`
}

function formatDateTime(isoStr) {
  if (!isoStr) return ''
  const date = new Date(isoStr)
  return date.toLocaleString('ru-RU', {
    day: '2-digit',
    month: '2-digit',
    year: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
  })
}

watch(selectedTeacher, () => {
  if (selectedTeacher.value) loadNotifications()
})
</script>

<style scoped>
.teacher-notify { display: flex; flex-direction: column; gap: 20px; }

.search-bar {
  display: flex; gap: 12px; align-items: center;
}

.teacher-list {
  display: grid; grid-template-columns: repeat(auto-fill, minmax(240px, 1fr));
  gap: 12px;
}

.teacher-item {
  display: flex; align-items: center; gap: 12px; padding: 16px;
  background: var(--bg-secondary); border: 1px solid var(--border);
  border-radius: var(--radius); cursor: pointer;
  transition: all var(--transition);
}
.teacher-item:hover {
  border-color: var(--accent); background: var(--accent-light);
}

.teacher-avatar {
  width: 40px; height: 40px; border-radius: 50%;
  background: var(--accent-light); color: var(--accent);
  display: flex; align-items: center; justify-content: center;
  font-size: 14px; font-weight: 700; flex-shrink: 0;
}

.teacher-info { flex: 1; }
.teacher-name { font-weight: 600; font-size: 14px; }
.teacher-id { font-size: 12px; color: var(--text-muted); margin-top: 2px; }

.notify-content { display: flex; flex-direction: column; gap: 24px; }

.notify-header h2 {
  font-size: 18px; font-weight: 700; margin: 0;
  padding: 12px 16px; background: var(--bg-secondary);
  border: 1px solid var(--border); border-radius: var(--radius);
}

.notify-form {
  padding: 24px;
}

.form-title {
  font-size: 16px; font-weight: 700; margin: 0 0 8px;
}

.form-help {
  color: var(--text-secondary); font-size: 13px; margin: 0 0 20px;
}

.form-row {
  display: grid; grid-template-columns: 1fr 1fr; gap: 12px;
}

.form-file {
  padding: 8px; background: var(--bg-tertiary); border: 1px solid var(--border);
  border-radius: var(--radius-sm); width: 100%; font-size: 13px;
}

.file-name {
  margin-top: 8px; font-size: 13px; color: var(--text-secondary);
}

.form-actions {
  display: flex; gap: 12px; margin-top: 8px;
}

.history-section {
  display: flex; flex-direction: column; gap: 16px;
}

.section-title {
  font-size: 16px; font-weight: 700; margin: 0;
}

.notifications-list {
  display: flex; flex-direction: column; gap: 12px;
}

.notification-card {
  padding: 16px;
}

.notification-card.status-approved {
  border-left: 4px solid var(--success);
}

.notification-card.status-rejected {
  border-left: 4px solid var(--error);
}

.notification-card.status-pending {
  border-left: 4px solid var(--warning);
}

.notif-header {
  display: flex; justify-content: space-between; align-items: center;
  margin-bottom: 12px; padding-bottom: 8px;
  border-bottom: 1px solid var(--border);
}

.notif-status {
  font-weight: 600; font-size: 13px;
}

.notif-date {
  font-size: 12px; color: var(--text-muted);
}

.notif-body p {
  margin: 0 0 8px; font-size: 13px; line-height: 1.5;
}

.notif-body p:last-child {
  margin-bottom: 0;
}

.notif-attachment a {
  color: var(--accent); text-decoration: none;
}
.notif-attachment a:hover {
  text-decoration: underline;
}

.notif-note {
  margin-top: 12px; padding-top: 12px;
  border-top: 1px solid var(--border);
  color: var(--text-secondary);
}

.center-block {
  display: flex; flex-direction: column; align-items: center; padding: 60px 20px;
}

@media (max-width: 640px) {
  .search-bar { flex-direction: column; align-items: stretch; }
  .teacher-list { grid-template-columns: 1fr; }
  .form-row { grid-template-columns: 1fr; }
  .form-actions { flex-direction: column; }
}
</style>
