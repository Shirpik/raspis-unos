<template>
  <div class="teacher-portal">
    <div v-if="!authenticated" class="auth-screen">
      <div class="auth-card">
        <h1 class="auth-title">Портал преподавателя</h1>
        <p class="auth-subtitle">Введите пароль для доступа</p>
        <form @submit.prevent="handleAuth" class="auth-form">
          <input
            v-model="password"
            type="password"
            placeholder="Пароль"
            class="auth-input"
            autofocus
          />
          <button type="submit" class="auth-btn">Войти</button>
          <p v-if="authError" class="auth-error">{{ authError }}</p>
        </form>
        <RouterLink to="/" class="back-link">← Назад</RouterLink>
      </div>
    </div>

    <div v-else class="portal-content">
      <div class="portal-header">
        <h1 class="portal-title">Портал преподавателя</h1>
        <button @click="logout" class="logout-btn">Выйти</button>
      </div>

      <div class="portal-tabs">
        <button
          v-for="tab in tabs"
          :key="tab.id"
          :class="['tab-btn', { active: activeTab === tab.id }]"
          @click="activeTab = tab.id"
        >
          {{ tab.label }}
        </button>
      </div>

      <div class="tab-content">
        <div v-if="activeTab === 'schedule'" class="schedule-tab">
          <div class="search-box">
            <input
              v-model="teacherSearch"
              type="text"
              placeholder="Поиск по ФИО или фамилии"
              class="search-input"
            />
          </div>
          <div v-if="selectedTeacher" class="teacher-schedule">
            <h2>Расписание: {{ selectedTeacher.name }}</h2>
            <div v-if="loading" class="loading">Загрузка...</div>
            <div v-else-if="schedule" class="schedule-grid">
              <!-- Schedule display similar to StudentView -->
              <div v-for="(daySlots, day) in groupedSchedule" :key="day" class="day-column">
                <div class="day-header">{{ dayName(day) }}</div>
                <div v-for="slot in daySlots" :key="`${day}-${slot.slot}`" class="lesson-card">
                  <div class="lesson-time">{{ slot.time }}</div>
                  <div class="lesson-subject">{{ slot.subject }}</div>
                  <div class="lesson-groups">{{ slot.groups }}</div>
                  <div class="lesson-room">{{ slot.room }}</div>
                </div>
              </div>
            </div>
          </div>
          <div v-else class="no-selection">
            Введите ФИО преподавателя для поиска
          </div>
        </div>

        <div v-if="activeTab === 'hours'" class="hours-tab">
          <div class="search-box">
            <input
              v-model="hoursTeacherSearch"
              type="text"
              placeholder="Поиск по ФИО или фамилии"
              class="search-input"
            />
          </div>
          <div v-if="selectedHoursTeacher" class="hours-display">
            <h2>Учет часов: {{ selectedHoursTeacher.name }}</h2>
            <div v-if="hoursLoading" class="loading">Загрузка...</div>
            <div v-else-if="hoursData" class="hours-content">
              <div class="hours-summary">
                <div class="hours-card">
                  <div class="hours-label">Всего часов</div>
                  <div class="hours-value">{{ hoursData.total || 0 }}</div>
                </div>
                <div class="hours-card">
                  <div class="hours-label">Выполнено</div>
                  <div class="hours-value">{{ hoursData.completed || 0 }}</div>
                </div>
                <div class="hours-card">
                  <div class="hours-label">Осталось</div>
                  <div class="hours-value">{{ hoursData.remaining || 0 }}</div>
                </div>
              </div>
              <button @click="downloadHours" class="download-btn">Скачать отчет</button>
            </div>
          </div>
          <div v-else class="no-selection">
            Введите ФИО преподавателя для просмотра учета часов
          </div>
        </div>

        <div v-if="activeTab === 'notify'" class="notify-tab">
          <h2>Сообщить диспетчеру о недоступности</h2>
          <form @submit.prevent="submitNotification" class="notify-form">
            <div class="form-group">
              <label>ФИО преподавателя</label>
              <input v-model="notifyTeacherSearch" type="text" required class="form-input" placeholder="Начните вводить ФИО..." />
              <div v-if="selectedNotifyTeacher" class="selected-teacher">
                Выбран: {{ selectedNotifyTeacher.name }}
              </div>
            </div>
            <div class="form-group">
              <label>Дата начала</label>
              <input v-model="notification.dateFrom" type="date" required class="form-input" />
            </div>
            <div class="form-group">
              <label>Дата окончания</label>
              <input v-model="notification.dateTo" type="date" required class="form-input" />
            </div>
            <div class="form-group">
              <label>Причина</label>
              <textarea v-model="notification.reason" required class="form-textarea" rows="4"></textarea>
            </div>
            <div class="form-group">
              <label>Приложить фото (необязательно)</label>
              <input type="file" @change="handleFileUpload" accept="image/*" class="form-file" />
            </div>
            <button type="submit" class="submit-btn" :disabled="submitting || !selectedNotifyTeacher">
              {{ submitting ? 'Отправка...' : 'Отправить' }}
            </button>
            <p v-if="submitSuccess" class="success-msg">Сообщение успешно отправлено</p>
            <p v-if="submitError" class="error-msg">{{ submitError }}</p>
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

const notification = ref({
  dateFrom: '',
  dateTo: '',
  reason: '',
  photo: null
})

const tabs = [
  { id: 'schedule', label: 'Расписание' },
  { id: 'hours', label: 'Учет часов' },
  { id: 'notify', label: 'Сообщить диспетчеру' }
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
    teachers.value = response || []
  } catch (e) {
    console.error('Failed to load teachers:', e)
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
    const scheduleData = response.schedule || response
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
      photo: notification.value.photo
    })
    submitSuccess.value = true
    notification.value = {
      dateFrom: '',
      dateTo: '',
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
.teacher-portal { min-height: 100vh; background: #0A0D12; }
.auth-screen { display: flex; align-items: center; justify-content: center; min-height: 100vh; padding: 24px; }
.auth-card { background: #0F131C; border-radius: 16px; padding: 48px; max-width: 420px; width: 100%; }
.auth-title { font-size: 28px; font-weight: 700; color: #E2E8F0; margin-bottom: 8px; }
.auth-subtitle { font-size: 14px; color: #94A3B8; margin-bottom: 32px; }
.auth-form { display: flex; flex-direction: column; gap: 16px; }
.auth-input { background: #161D2B; border: 1px solid #1E2636; border-radius: 8px; padding: 12px 16px; color: #E2E8F0; font-size: 15px; }
.auth-input:focus { outline: none; border-color: #38BDF8; }
.auth-btn { background: #38BDF8; color: #0A0D12; font-weight: 600; padding: 12px; border-radius: 8px; cursor: pointer; border: none; }
.auth-btn:hover { background: #0EA5E9; }
.auth-error { color: #F87171; font-size: 13px; margin-top: -8px; }
.back-link { display: inline-block; margin-top: 24px; color: #64748B; font-size: 14px; text-decoration: none; }
.back-link:hover { color: #38BDF8; }

.portal-content { padding: 24px; }
.portal-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 24px; }
.portal-title { font-size: 24px; font-weight: 700; color: #E2E8F0; }
.logout-btn { background: #1E2636; color: #E2E8F0; padding: 8px 16px; border-radius: 8px; border: none; cursor: pointer; }
.logout-btn:hover { background: #2D3548; }

.portal-tabs { display: flex; gap: 8px; margin-bottom: 24px; border-bottom: 1px solid #1E2636; }
.tab-btn { background: none; border: none; color: #94A3B8; padding: 12px 24px; cursor: pointer; border-bottom: 2px solid transparent; font-size: 15px; font-weight: 500; }
.tab-btn.active { color: #38BDF8; border-bottom-color: #38BDF8; }
.tab-btn:hover { color: #E2E8F0; }

.tab-content { background: #0F131C; border-radius: 16px; padding: 24px; }
.search-box { margin-bottom: 24px; }
.search-input { width: 100%; background: #161D2B; border: 1px solid #1E2636; border-radius: 8px; padding: 12px 16px; color: #E2E8F0; font-size: 15px; }
.search-input:focus { outline: none; border-color: #38BDF8; }

.no-selection { color: #64748B; text-align: center; padding: 48px; }
.loading { color: #94A3B8; text-align: center; padding: 48px; }

.teacher-schedule h2, .hours-display h2 { font-size: 20px; font-weight: 600; color: #E2E8F0; margin-bottom: 24px; }

.hours-summary { display: grid; grid-template-columns: repeat(3, 1fr); gap: 16px; margin-bottom: 24px; }
.hours-card { background: #161D2B; border-radius: 12px; padding: 20px; text-align: center; }
.hours-label { color: #94A3B8; font-size: 13px; margin-bottom: 8px; }
.hours-value { color: #38BDF8; font-size: 32px; font-weight: 700; }

.download-btn { background: #38BDF8; color: #0A0D12; padding: 12px 24px; border-radius: 8px; border: none; cursor: pointer; font-weight: 600; }
.download-btn:hover { background: #0EA5E9; }

.notify-form { max-width: 600px; }
.form-group { margin-bottom: 20px; }
.form-group label { display: block; color: #94A3B8; font-size: 14px; margin-bottom: 8px; }
.form-input, .form-textarea { width: 100%; background: #161D2B; border: 1px solid #1E2636; border-radius: 8px; padding: 12px 16px; color: #E2E8F0; font-size: 15px; }
.form-input:focus, .form-textarea:focus { outline: none; border-color: #38BDF8; }
.form-file { color: #94A3B8; }
.selected-teacher { margin-top: 8px; padding: 8px 12px; background: #161D2B; border-radius: 6px; color: #34D399; font-size: 14px; }
.submit-btn { background: #38BDF8; color: #0A0D12; padding: 12px 32px; border-radius: 8px; border: none; cursor: pointer; font-weight: 600; font-size: 15px; }
.submit-btn:hover:not(:disabled) { background: #0EA5E9; }
.submit-btn:disabled { opacity: 0.5; cursor: not-allowed; }
.success-msg { color: #34D399; margin-top: 12px; }
.error-msg { color: #F87171; margin-top: 12px; }
</style>
