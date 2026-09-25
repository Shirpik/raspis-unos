<template>
  <div class="teacher-portal">
    <div v-if="!authenticated" class="auth-screen">
      <div class="auth-container">
        <div class="auth-header">
          <div class="auth-icon">
            <UserCheck :size="32" />
          </div>
          <h1>Портал преподавателя</h1>
          <p class="auth-subtitle">Введите пароль для доступа к порталу</p>
        </div>

        <form @submit.prevent="handleAuth" class="auth-form">
          <div class="input-group">
            <div class="input-icon">
              <Lock :size="18" />
            </div>
            <input
              v-model="password"
              type="password"
              placeholder="Введите пароль"
              class="input-field"
              autofocus
            />
          </div>

          <button type="submit" class="btn-submit">
            <LogIn :size="18" />
            <span>Войти</span>
          </button>

          <div v-if="authError" class="error-message">
            <AlertCircle :size="16" />
            <span>{{ authError }}</span>
          </div>
        </form>

        <RouterLink to="/" class="back-link">
          <ArrowLeft :size="16" />
          <span>Вернуться на главную</span>
        </RouterLink>
      </div>
    </div>

    <div v-else class="portal-content">
      <div class="portal-header">
        <div class="portal-header-left">
          <UserCheck :size="24" style="color: var(--accent)" />
          <h1 class="portal-title">Портал преподавателя</h1>
        </div>
        <button @click="logout" class="btn btn-secondary">
          <LogOut :size="18" />
          <span>Выйти</span>
        </button>
      </div>

      <div class="portal-tabs">
        <button
          v-for="tab in tabs"
          :key="tab.id"
          :class="['tab-btn', { active: activeTab === tab.id }]"
          @click="activeTab = tab.id"
        >
          <component :is="tab.icon" :size="18" />
          <span>{{ tab.label }}</span>
        </button>
      </div>

      <div class="tab-content">
        <div v-if="activeTab === 'schedule'" class="schedule-tab">
          <div class="search-box">
            <Search :size="20" />
            <input
              v-model="teacherSearch"
              type="text"
              placeholder="Поиск по ФИО или фамилии"
              class="search-input"
            />
          </div>
          <div v-if="selectedTeacher" class="teacher-schedule">
            <div class="section-header">
              <User :size="20" />
              <h2>Расписание: {{ selectedTeacher.name }}</h2>
            </div>
            <div v-if="loading" class="loading-state">
              <div class="spinner spinner-lg"></div>
              <span>Загрузка расписания...</span>
            </div>
            <div v-else-if="schedule" class="schedule-grid">
              <div v-for="(daySlots, day) in groupedSchedule" :key="day" class="day-column">
                <div class="day-header">
                  <Calendar :size="16" />
                  <span>{{ dayName(day) }}</span>
                </div>
                <div v-for="slot in daySlots" :key="`${day}-${slot.slot}`" class="lesson-card">
                  <div class="lesson-time">
                    <Clock :size="14" />
                    <span>{{ slot.time }}</span>
                  </div>
                  <div class="lesson-subject">{{ slot.subject }}</div>
                  <div class="lesson-groups">
                    <Users :size="14" />
                    <span>{{ slot.groups }}</span>
                  </div>
                  <div class="lesson-room">
                    <MapPin :size="14" />
                    <span>{{ slot.room }}</span>
                  </div>
                </div>
              </div>
            </div>
          </div>
          <div v-else class="no-selection">
            <Search :size="48" style="opacity: 0.3" />
            <p>Введите ФИО преподавателя для поиска</p>
          </div>
        </div>

        <div v-if="activeTab === 'hours'" class="hours-tab">
          <div class="search-box">
            <Search :size="20" />
            <input
              v-model="hoursTeacherSearch"
              type="text"
              placeholder="Поиск по ФИО или фамилии"
              class="search-input"
            />
          </div>
          <div v-if="selectedHoursTeacher" class="hours-display">
            <div class="section-header">
              <Clock :size="20" />
              <h2>Учет часов: {{ selectedHoursTeacher.name }}</h2>
            </div>
            <div v-if="hoursLoading" class="loading-state">
              <div class="spinner spinner-lg"></div>
              <span>Загрузка данных...</span>
            </div>
            <div v-else-if="hoursData" class="hours-content">
              <div class="hours-summary">
                <div class="hours-card">
                  <div class="hours-icon">
                    <BookOpen :size="24" />
                  </div>
                  <div class="hours-label">Всего часов</div>
                  <div class="hours-value">{{ hoursData.total || 0 }}</div>
                </div>
                <div class="hours-card">
                  <div class="hours-icon" style="color: var(--success)">
                    <CheckCircle :size="24" />
                  </div>
                  <div class="hours-label">Выполнено</div>
                  <div class="hours-value" style="color: var(--success)">{{ hoursData.completed || 0 }}</div>
                </div>
                <div class="hours-card">
                  <div class="hours-icon" style="color: var(--accent)">
                    <TrendingUp :size="24" />
                  </div>
                  <div class="hours-label">Осталось</div>
                  <div class="hours-value" style="color: var(--accent)">{{ hoursData.remaining || 0 }}</div>
                </div>
              </div>
              <button @click="downloadHours" class="btn btn-primary">
                <Download :size="18" />
                <span>Скачать отчет</span>
              </button>
            </div>
          </div>
          <div v-else class="no-selection">
            <Search :size="48" style="opacity: 0.3" />
            <p>Введите ФИО преподавателя для просмотра учета часов</p>
          </div>
        </div>

        <div v-if="activeTab === 'notify'" class="notify-tab">
          <div class="section-header">
            <Bell :size="20" />
            <h2>Сообщить диспетчеру о недоступности</h2>
          </div>
          <form @submit.prevent="submitNotification" class="notify-form">
            <div class="form-group">
              <label class="form-label">
                <User :size="16" />
                <span>ФИО преподавателя</span>
              </label>
              <div class="search-box">
                <Search :size="20" />
                <input v-model="notifyTeacherSearch" type="text" required class="search-input" placeholder="Начните вводить ФИО..." />
              </div>
              <div v-if="selectedNotifyTeacher" class="selected-teacher">
                <CheckCircle :size="16" />
                <span>Выбран: {{ selectedNotifyTeacher.name }}</span>
              </div>
            </div>
            <div class="form-row">
              <div class="form-group">
                <label class="form-label">
                  <Calendar :size="16" />
                  <span>Дата начала</span>
                </label>
                <input v-model="notification.dateFrom" type="date" required class="form-input" />
              </div>
              <div class="form-group">
                <label class="form-label">
                  <Calendar :size="16" />
                  <span>Дата окончания</span>
                </label>
                <input v-model="notification.dateTo" type="date" required class="form-input" />
              </div>
            </div>
            <div class="form-row">
              <div class="form-group">
                <label class="form-label">
                  <Clock :size="16" />
                  <span>Время начала (необязательно)</span>
                </label>
                <input v-model="notification.timeFrom" type="time" class="form-input" />
              </div>
              <div class="form-group">
                <label class="form-label">
                  <Clock :size="16" />
                  <span>Время окончания (необязательно)</span>
                </label>
                <input v-model="notification.timeTo" type="time" class="form-input" />
              </div>
            </div>
            <div class="form-group">
              <label class="form-label">
                <FileText :size="16" />
                <span>Причина</span>
              </label>
              <textarea v-model="notification.reason" required class="form-textarea" rows="4" placeholder="Опишите причину недоступности..."></textarea>
            </div>
            <div class="form-group">
              <label class="form-label">
                <Image :size="16" />
                <span>Приложить фото (необязательно)</span>
              </label>
              <div
                class="file-upload-area"
                :class="{ 'has-file': notification.photo, 'drag-over': isDragging }"
                @click="$refs.fileInput.click()"
                @dragover.prevent="isDragging = true"
                @dragleave.prevent="isDragging = false"
                @drop.prevent="handleFileDrop"
              >
                <input
                  ref="fileInput"
                  type="file"
                  @change="handleFileUpload"
                  accept="image/*"
                  style="display: none"
                />
                <div v-if="!notification.photo" class="file-upload-empty">
                  <div class="file-upload-icon">
                    <Upload :size="32" />
                  </div>
                  <div class="file-upload-text">
                    <p class="file-upload-primary">Нажмите для загрузки или перетащите файл</p>
                    <p class="file-upload-secondary">PNG, JPG до 10MB</p>
                  </div>
                </div>
                <div v-else class="file-upload-preview">
                  <img :src="notification.photo" alt="Preview" />
                  <button type="button" @click.stop="removeFile" class="file-remove-btn">
                    <X :size="16" />
                  </button>
                </div>
              </div>
            </div>
            <button type="submit" class="btn btn-primary" style="width: 100%" :disabled="submitting || !selectedNotifyTeacher">
              <Send :size="18" />
              <span>{{ submitting ? 'Отправка...' : 'Отправить' }}</span>
            </button>
            <div v-if="submitSuccess" class="success-msg">
              <CheckCircle :size="16" />
              <span>Сообщение успешно отправлено</span>
            </div>
            <div v-if="submitError" class="error-msg">
              <AlertCircle :size="16" />
              <span>{{ submitError }}</span>
            </div>
          </form>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch } from 'vue'
import { useRouter } from 'vue-router'
import { api } from '../api'
import {
  UserCheck, Lock, LogIn, LogOut, ArrowLeft, AlertCircle,
  Search, User, Calendar, Clock, MapPin, Users, Bell,
  CheckCircle, FileText, Image, Upload, X, Send, Download,
  BookOpen, TrendingUp
} from 'lucide-vue-next'

const router = useRouter()
const authenticated = ref(false)
const password = ref('')
const authError = ref('')
const activeTab = ref('schedule')
const teacherSearch = ref('')
const hoursTeacherSearch = ref('')
const notifyTeacherSearch = ref('')
const selectedTeacher = ref(null)
const selectedHoursTeacher = ref(null)
const selectedNotifyTeacher = ref(null)
const schedule = ref(null)
const loading = ref(false)
const hoursData = ref(null)
const hoursLoading = ref(false)
const teachers = ref([])
const submitting = ref(false)
const submitSuccess = ref(false)
const submitError = ref('')
const isDragging = ref(false)

const notification = ref({
  dateFrom: '',
  dateTo: '',
  timeFrom: '',
  timeTo: '',
  reason: '',
  photo: null
})

const tabs = [
  { id: 'schedule', label: 'Расписание', icon: Calendar },
  { id: 'hours', label: 'Учет часов', icon: Clock },
  { id: 'notify', label: 'Сообщить диспетчеру', icon: Bell }
]

async function handleAuth() {
  try {
    const response = await api.teacher.auth(password.value)
    if (response.ok) {
      authenticated.value = true
      authError.value = ''
      await loadTeachers()
    } else {
      authError.value = 'Неверный пароль'
    }
  } catch (e) {
    authError.value = 'Ошибка авторизации'
  }
}

function logout() {
  authenticated.value = false
  password.value = ''
  teacherSearch.value = ''
  selectedTeacher.value = null
}

async function loadTeachers() {
  try {
    const response = await api.teacher.getTeachersList(password.value)
    teachers.value = Array.isArray(response.data) ? response.data : []
  } catch (e) {
    console.error('Failed to load teachers:', e)
    teachers.value = []
  }
}

watch(teacherSearch, async (query) => {
  if (!query || query.length < 2) {
    selectedTeacher.value = null
    schedule.value = null
    return
  }
  const found = teachers.value.find(t =>
    t.name?.toLowerCase().includes(query.toLowerCase())
  )
  if (found) {
    selectedTeacher.value = found
    await loadSchedule(found.id)
  } else {
    selectedTeacher.value = null
    schedule.value = null
  }
})

watch(hoursTeacherSearch, async (query) => {
  if (!query || query.length < 2) {
    selectedHoursTeacher.value = null
    hoursData.value = null
    return
  }
  const found = teachers.value.find(t =>
    t.name?.toLowerCase().includes(query.toLowerCase())
  )
  if (found) {
    selectedHoursTeacher.value = found
    await loadHours(found.id)
  } else {
    selectedHoursTeacher.value = null
    hoursData.value = null
  }
})

watch(notifyTeacherSearch, (query) => {
  if (!query || query.length < 2) {
    selectedNotifyTeacher.value = null
    return
  }
  const found = teachers.value.find(t =>
    t.name?.toLowerCase().includes(query.toLowerCase())
  )
  selectedNotifyTeacher.value = found || null
})

async function loadSchedule(teacherId) {
  loading.value = true
  try {
    const response = await api.schedule.get()
    const scheduleData = response.data?.schedule || response.data
    // Filter schedule for this teacher
    schedule.value = scheduleData
  } catch (e) {
    console.error('Failed to load schedule:', e)
  } finally {
    loading.value = false
  }
}

async function loadHours(teacherId) {
  hoursLoading.value = true
  try {
    const response = await api.data.hours()
    if (response.ok && response.data) {
      const data = response.data.find(t => t.teacher_id === teacherId)
      hoursData.value = data || { total: 0, completed: 0, remaining: 0 }
    }
  } catch (e) {
    console.error('Failed to load hours:', e)
  } finally {
    hoursLoading.value = false
  }
}

const groupedSchedule = computed(() => {
  if (!schedule.value) return {}
  // Group schedule by day - implementation similar to StudentView
  return {}
})

function dayName(day) {
  const names = ['Понедельник', 'Вторник', 'Среда', 'Четверг', 'Пятница', 'Суббота']
  return names[day - 1] || ''
}

function handleFileUpload(e) {
  const file = e.target.files[0]
  if (file) {
    const reader = new FileReader()
    reader.onload = (ev) => {
      notification.value.photo = ev.target.result
    }
    reader.readAsDataURL(file)
  }
}

function handleFileDrop(e) {
  isDragging.value = false
  const file = e.dataTransfer.files[0]
  if (file && file.type.startsWith('image/')) {
    const reader = new FileReader()
    reader.onload = (ev) => {
      notification.value.photo = ev.target.result
    }
    reader.readAsDataURL(file)
  }
}

function removeFile() {
  notification.value.photo = null
}

async function submitNotification() {
  if (!selectedNotifyTeacher.value) {
    submitError.value = 'Пожалуйста, выберите преподавателя'
    return
  }
  submitting.value = true
  submitSuccess.value = false
  submitError.value = ''
  try {
    const dates = []
    const start = new Date(notification.value.dateFrom)
    const end = new Date(notification.value.dateTo)
    for (let d = new Date(start); d <= end; d.setDate(d.getDate() + 1)) {
      dates.push(d.toISOString().split('T')[0])
    }

    await api.teacher.submitNotification({
      password: password.value,
      teacher_id: selectedNotifyTeacher.value.id,
      teacher_name: selectedNotifyTeacher.value.name,
      dates: dates,
      reason: notification.value.reason,
      photo: notification.value.photo,
      time_from: notification.value.timeFrom || null,
      time_to: notification.value.timeTo || null
    })
    submitSuccess.value = true
    notification.value = {
      dateFrom: '',
      dateTo: '',
      timeFrom: '',
      timeTo: '',
      reason: '',
      photo: null
    }
    notifyTeacherSearch.value = ''
    selectedNotifyTeacher.value = null
  } catch (e) {
    submitError.value = 'Ошибка отправки: ' + (e.message || 'Неизвестная ошибка')
  } finally {
    submitting.value = false
  }
}

function downloadHours() {
  if (!hoursData.value) return
  const csv = `ФИО,Всего часов,Выполнено,Осталось\n${selectedHoursTeacher.value.name},${hoursData.value.total},${hoursData.value.completed},${hoursData.value.remaining}`
  const blob = new Blob([csv], { type: 'text/csv;charset=utf-8;' })
  const link = document.createElement('a')
  link.href = URL.createObjectURL(blob)
  link.download = `hours_${selectedHoursTeacher.value.name}.csv`
  link.click()
}
</script>

<style scoped>
.teacher-portal { min-height: 100vh; background: var(--bg-primary); }

/* ── Auth screen ──────────────────────────────────────────────────────── */
.auth-screen {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 100vh;
  padding: 24px;
  background: var(--bg-primary);
}

.auth-container {
  width: 100%;
  max-width: 440px;
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: 16px;
  padding: 48px 40px;
  box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.1), 0 2px 4px -1px rgba(0, 0, 0, 0.06);
}

.auth-header {
  text-align: center;
  margin-bottom: 32px;
}

.auth-icon {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 72px;
  height: 72px;
  background: rgba(251, 146, 60, 0.1);
  border-radius: 12px;
  color: var(--accent);
  margin-bottom: 24px;
}

.auth-header h1 {
  font-size: 26px;
  font-weight: 700;
  color: var(--text-primary);
  letter-spacing: -0.02em;
  margin: 0 0 8px 0;
}

.auth-subtitle {
  font-size: 15px;
  color: var(--text-muted);
  margin: 0;
}

.auth-form {
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.input-group {
  position: relative;
}

.input-icon {
  position: absolute;
  left: 16px;
  top: 50%;
  transform: translateY(-50%);
  color: var(--text-muted);
  pointer-events: none;
  z-index: 1;
}

.input-field {
  width: 100%;
  padding: 14px 16px 14px 48px;
  background: var(--bg-tertiary);
  border: 1px solid var(--border);
  border-radius: 8px;
  color: var(--text-primary);
  font-size: 15px;
  transition: all 0.2s;
  outline: none;
}

.input-field:focus {
  border-color: var(--accent);
  box-shadow: 0 0 0 3px rgba(251, 146, 60, 0.1);
  background: var(--bg-secondary);
}

.input-field::placeholder {
  color: var(--text-muted);
}

.btn-submit {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 10px;
  width: 100%;
  padding: 14px 24px;
  background: var(--accent);
  color: var(--bg-primary);
  font-size: 15px;
  font-weight: 600;
  border: none;
  border-radius: 8px;
  cursor: pointer;
  transition: all 0.2s;
}

.btn-submit:hover {
  background: #f97316;
  transform: translateY(-1px);
  box-shadow: 0 4px 12px rgba(251, 146, 60, 0.3);
}

.btn-submit:active {
  transform: translateY(0);
}

.error-message {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 12px 16px;
  background: rgba(239, 68, 68, 0.1);
  border: 1px solid rgba(239, 68, 68, 0.2);
  border-radius: 8px;
  color: #ef4444;
  font-size: 14px;
}

.back-link {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  width: 100%;
  margin-top: 16px;
  padding: 12px;
  color: var(--text-secondary);
  font-size: 14px;
  text-decoration: none;
  border-radius: 8px;
  transition: all 0.2s;
}

.back-link:hover {
  color: var(--accent);
  background: var(--bg-tertiary);
}

/* ── Portal content ───────────────────────────────────────────────────── */
.portal-content { padding: 24px; max-width: 1400px; margin: 0 auto; }
.portal-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 24px;
  padding-bottom: 20px;
  border-bottom: 1px solid var(--border);
}
.portal-header-left {
  display: flex;
  align-items: center;
  gap: 12px;
}
.portal-title {
  font-size: 24px;
  font-weight: 700;
  color: var(--text-primary);
  letter-spacing: -0.02em;
}

/* ── Tabs ─────────────────────────────────────────────────────────────── */
.portal-tabs {
  display: flex;
  gap: 8px;
  margin-bottom: 24px;
  border-bottom: 1px solid var(--border);
  overflow-x: auto;
}
.tab-btn {
  display: flex;
  align-items: center;
  gap: 8px;
  background: none;
  border: none;
  color: var(--text-secondary);
  padding: 12px 20px;
  cursor: pointer;
  border-bottom: 2px solid transparent;
  font-size: 14px;
  font-weight: 500;
  transition: all var(--transition);
  white-space: nowrap;
}
.tab-btn:hover { color: var(--text-primary); }
.tab-btn.active {
  color: var(--accent);
  border-bottom-color: var(--accent);
}

/* ── Tab content ──────────────────────────────────────────────────────── */
.tab-content {
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 24px;
}

/* ── Search box ───────────────────────────────────────────────────────── */
.search-box {
  position: relative;
  display: flex;
  align-items: center;
  gap: 12px;
  background: var(--bg-tertiary);
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  padding: 12px 16px;
  margin-bottom: 24px;
  transition: all var(--transition);
}
.search-box:focus-within {
  border-color: var(--accent);
  box-shadow: 0 0 0 3px rgba(251, 146, 60, 0.1);
}
.search-box svg {
  color: var(--text-muted);
  flex-shrink: 0;
}
.search-input {
  flex: 1;
  background: transparent;
  border: none;
  color: var(--text-primary);
  font-size: 15px;
  outline: none;
}

/* ── Section header ───────────────────────────────────────────────────── */
.section-header {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 24px;
  padding-bottom: 16px;
  border-bottom: 1px solid var(--border);
}
.section-header svg { color: var(--accent); }
.section-header h2 {
  font-size: 20px;
  font-weight: 600;
  color: var(--text-primary);
  margin: 0;
}

/* ── No selection / Loading ───────────────────────────────────────────── */
.no-selection {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 16px;
  color: var(--text-muted);
  text-align: center;
  padding: 80px 24px;
}
.no-selection p { margin: 0; font-size: 15px; }

.loading-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 16px;
  color: var(--text-secondary);
  padding: 80px 24px;
}

/* ── Hours summary ────────────────────────────────────────────────────── */
.hours-summary {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 16px;
  margin-bottom: 24px;
}
.hours-card {
  background: var(--bg-tertiary);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 24px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 12px;
  transition: all var(--transition);
}
.hours-card:hover {
  border-color: var(--accent);
  transform: translateY(-2px);
}
.hours-icon { color: var(--accent); }
.hours-label {
  color: var(--text-secondary);
  font-size: 13px;
  font-weight: 500;
  text-transform: uppercase;
  letter-spacing: 0.05em;
}
.hours-value {
  color: var(--accent);
  font-size: 36px;
  font-weight: 800;
  line-height: 1;
}

/* ── Form ─────────────────────────────────────────────────────────────── */
.notify-form { max-width: 700px; }
.form-group { margin-bottom: 20px; }
.form-label {
  display: flex;
  align-items: center;
  gap: 8px;
  color: var(--text-secondary);
  font-size: 13px;
  font-weight: 600;
  margin-bottom: 8px;
  text-transform: uppercase;
  letter-spacing: 0.05em;
}
.form-row {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
  gap: 16px;
}

.selected-teacher {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-top: 8px;
  padding: 10px 14px;
  background: rgba(52, 211, 153, 0.1);
  border: 1px solid var(--success);
  border-radius: var(--radius-sm);
  color: var(--success);
  font-size: 14px;
  font-weight: 500;
}

/* ── File upload ──────────────────────────────────────────────────────── */
.file-upload-area {
  position: relative;
  border: 2px dashed var(--border);
  border-radius: var(--radius);
  background: var(--bg-tertiary);
  padding: 32px;
  cursor: pointer;
  transition: all var(--transition);
}
.file-upload-area:hover {
  border-color: var(--accent);
  background: var(--bg-primary);
}
.file-upload-area.drag-over {
  border-color: var(--accent);
  background: rgba(251, 146, 60, 0.05);
}
.file-upload-empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 16px;
}
.file-upload-icon {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 64px;
  height: 64px;
  background: var(--bg-primary);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  color: var(--text-muted);
}
.file-upload-text {
  text-align: center;
}
.file-upload-primary {
  color: var(--text-primary);
  font-size: 14px;
  font-weight: 500;
  margin: 0 0 4px;
}
.file-upload-secondary {
  color: var(--text-muted);
  font-size: 12px;
  margin: 0;
}
.file-upload-preview {
  position: relative;
  display: flex;
  justify-content: center;
}
.file-upload-preview img {
  max-width: 100%;
  max-height: 300px;
  border-radius: var(--radius-sm);
}
.file-remove-btn {
  position: absolute;
  top: 8px;
  right: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  width: 32px;
  height: 32px;
  background: var(--error);
  border: none;
  border-radius: 50%;
  color: #fff;
  cursor: pointer;
  transition: all var(--transition);
}
.file-remove-btn:hover {
  transform: scale(1.1);
}

/* ── Messages ─────────────────────────────────────────────────────────── */
.success-msg {
  display: flex;
  align-items: center;
  gap: 8px;
  color: var(--success);
  font-size: 14px;
  margin-top: 16px;
  padding: 12px 16px;
  background: rgba(52, 211, 153, 0.1);
  border: 1px solid var(--success);
  border-radius: var(--radius-sm);
}
.error-msg {
  display: flex;
  align-items: center;
  gap: 8px;
  color: var(--error);
  font-size: 14px;
  margin-top: 16px;
  padding: 12px 16px;
  background: rgba(248, 113, 113, 0.1);
  border: 1px solid var(--error);
  border-radius: var(--radius-sm);
}

/* ── Schedule grid ────────────────────────────────────────────────────── */
.schedule-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
  gap: 16px;
}
.day-column {
  background: var(--bg-tertiary);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  overflow: hidden;
}
.day-header {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  background: var(--bg-primary);
  padding: 12px;
  font-size: 14px;
  font-weight: 600;
  color: var(--text-primary);
  border-bottom: 1px solid var(--border);
}
.lesson-card {
  padding: 16px;
  border-bottom: 1px solid var(--border);
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.lesson-card:last-child { border-bottom: none; }
.lesson-time {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: var(--text-muted);
  font-weight: 500;
}
.lesson-subject {
  font-size: 14px;
  font-weight: 600;
  color: var(--text-primary);
}
.lesson-groups, .lesson-room {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
  color: var(--text-secondary);
}

@media (max-width: 640px) {
  .auth-card { padding: 32px 24px; }
  .portal-content { padding: 16px; }
  .tab-content { padding: 16px; }
  .hours-summary {
    grid-template-columns: 1fr;
  }
  .form-row {
    grid-template-columns: 1fr;
  }
  .schedule-grid {
    grid-template-columns: 1fr;
  }
}
</style>
