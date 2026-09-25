<template>
  <div class="notifications-view">
    <div class="page-header">
      <h1 class="page-title">Уведомления преподавателей</h1>
      <div class="header-actions">
        <button @click="loadNotifications" class="refresh-btn">Обновить</button>
      </div>
    </div>

    <div v-if="loading" class="loading-state">Загрузка...</div>

    <div v-else-if="notifications.length === 0" class="empty-state">
      Нет уведомлений от преподавателей
    </div>

    <div v-else class="notifications-list">
      <div
        v-for="notif in notifications"
        :key="notif.id"
        :class="['notification-card', `status-${notif.status}`]"
      >
        <div class="notif-header">
          <div class="notif-teacher">{{ notif.teacher_name }}</div>
          <div :class="['notif-status', `status-${notif.status}`]">
            {{ statusLabel(notif.status) }}
          </div>
        </div>

        <div class="notif-dates">
          <strong>Даты:</strong> {{ formatDates(notif.dates) }}
        </div>

        <div class="notif-reason">
          <strong>Причина:</strong> {{ notif.reason }}
        </div>

        <div v-if="notif.photo" class="notif-photo">
          <img :src="notif.photo" alt="Приложенное фото" />
        </div>

        <div class="notif-meta">
          <span>Создано: {{ formatDate(notif.created_at) }}</span>
        </div>

        <div v-if="notif.status === 'pending'" class="notif-actions">
          <button @click="approveNotification(notif)" class="btn-approve">
            Принять
          </button>
          <button @click="openRejectDialog(notif)" class="btn-reject">
            Отклонить
          </button>
          <button @click="openEditDialog(notif)" class="btn-edit">
            Редактировать
          </button>
        </div>
      </div>
    </div>

    <div v-if="showEditDialog" class="dialog-overlay" @click="closeEditDialog">
      <div class="dialog-card" @click.stop>
        <h2 class="dialog-title">Редактировать уведомление</h2>

        <div class="form-group">
          <label>ФИО преподавателя</label>
          <input v-model="editForm.teacher_name" class="form-input" />
        </div>

        <div class="form-group">
          <label>Даты (через запятую, формат YYYY-MM-DD)</label>
          <input v-model="editForm.datesStr" class="form-input" placeholder="2026-09-26, 2026-09-27" />
        </div>

        <div class="form-group">
          <label>Причина</label>
          <textarea v-model="editForm.reason" class="form-textarea" rows="3"></textarea>
        </div>

        <div class="dialog-actions">
          <button @click="saveEdit" class="btn-primary">Сохранить</button>
          <button @click="closeEditDialog" class="btn-secondary">Отмена</button>
        </div>
      </div>
    </div>

    <div v-if="showRejectDialog" class="dialog-overlay" @click="closeRejectDialog">
      <div class="dialog-card" @click.stop>
        <h2 class="dialog-title">Отклонить уведомление</h2>
        <p>Вы уверены, что хотите отклонить это уведомление?</p>
        <div class="dialog-actions">
          <button @click="rejectNotification" class="btn-danger">Отклонить</button>
          <button @click="closeRejectDialog" class="btn-secondary">Отмена</button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { api } from '../api'

const notifications = ref([])
const loading = ref(false)
const showEditDialog = ref(false)
const showRejectDialog = ref(false)
const currentNotif = ref(null)
const editForm = ref({
  teacher_name: '',
  datesStr: '',
  reason: ''
})

onMounted(() => {
  loadNotifications()
})

async function loadNotifications() {
  loading.value = true
  try {
    const response = await api.teacherNotifications.listAll()
    if (response.ok) {
      notifications.value = response.data || []
    }
  } catch (e) {
    console.error('Failed to load notifications:', e)
  } finally {
    loading.value = false
  }
}

function statusLabel(status) {
  const labels = {
    pending: 'Ожидает',
    approved: 'Принято',
    rejected: 'Отклонено'
  }
  return labels[status] || status
}

function formatDates(dates) {
  if (!dates || !Array.isArray(dates)) return 'Не указано'
  if (dates.length === 1) return dates[0]
  return `${dates[0]} — ${dates[dates.length - 1]}`
}

function formatDate(timestamp) {
  if (!timestamp) return ''
  const date = new Date(parseInt(timestamp) * 1000)
  return date.toLocaleString('ru-RU')
}

async function approveNotification(notif) {
  try {
    const response = await api.teacherNotifications.update(notif.id, { status: 'approved' })
    if (response.ok) {
      // Добавляем записи в teacher-unavailable с диапазоном дат
      if (notif.dates && notif.dates.length > 0) {
        const fromDate = notif.dates[0]
        const toDate = notif.dates[notif.dates.length - 1]

        await api.teacherUnavailable.create({
          teacher: notif.teacher_id,
          from: fromDate,
          to: toDate
        })
      }
      await loadNotifications()
    } else {
      alert('Ошибка при одобрении: ' + (response.data?.message || 'Неизвестная ошибка'))
    }
  } catch (e) {
    console.error('Failed to approve:', e)
    alert('Ошибка при одобрении уведомления')
  }
}

function openRejectDialog(notif) {
  currentNotif.value = notif
  showRejectDialog.value = true
}

function closeRejectDialog() {
  showRejectDialog.value = false
  currentNotif.value = null
}

async function rejectNotification() {
  if (!currentNotif.value) return
  try {
    const response = await api.teacherNotifications.update(currentNotif.value.id, { status: 'rejected' })
    if (response.ok) {
      await loadNotifications()
    }
  } catch (e) {
    console.error('Failed to reject:', e)
  } finally {
    closeRejectDialog()
  }
}

function openEditDialog(notif) {
  currentNotif.value = notif
  editForm.value = {
    teacher_name: notif.teacher_name,
    datesStr: notif.dates ? notif.dates.join(', ') : '',
    reason: notif.reason
  }
  showEditDialog.value = true
}

function closeEditDialog() {
  showEditDialog.value = false
  currentNotif.value = null
}

async function saveEdit() {
  if (!currentNotif.value) return
  try {
    const dates = editForm.value.datesStr.split(',').map(d => d.trim()).filter(d => d)
    const response = await api.teacherNotifications.update(currentNotif.value.id, {
      teacher_name: editForm.value.teacher_name,
      dates: dates,
      reason: editForm.value.reason
    })
    if (response.ok) {
      await loadNotifications()
    }
  } catch (e) {
    console.error('Failed to save edit:', e)
  } finally {
    closeEditDialog()
  }
}
</script>

<style scoped>
.notifications-view { padding: 24px; }
.page-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 24px; }
.page-title { font-size: 24px; font-weight: 700; color: #E2E8F0; margin: 0; }
.refresh-btn { background: #1E2636; color: #E2E8F0; padding: 8px 16px; border-radius: 8px; border: none; cursor: pointer; }
.refresh-btn:hover { background: #2D3548; }

.loading-state, .empty-state { text-align: center; padding: 48px; color: #64748B; }

.notifications-list { display: flex; flex-direction: column; gap: 16px; }
.notification-card { background: #0F131C; border-radius: 12px; padding: 20px; border-left: 4px solid #1E2636; }
.notification-card.status-pending { border-left-color: #F59E0B; }
.notification-card.status-approved { border-left-color: #10B981; }
.notification-card.status-rejected { border-left-color: #EF4444; }

.notif-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px; }
.notif-teacher { font-size: 18px; font-weight: 600; color: #E2E8F0; }
.notif-status { padding: 4px 12px; border-radius: 999px; font-size: 13px; font-weight: 500; }
.notif-status.status-pending { background: #F59E0B; color: #0A0D12; }
.notif-status.status-approved { background: #10B981; color: #0A0D12; }
.notif-status.status-rejected { background: #EF4444; color: #FFFFFF; }

.notif-dates, .notif-reason { margin-bottom: 12px; color: #94A3B8; font-size: 14px; }
.notif-dates strong, .notif-reason strong { color: #E2E8F0; }

.notif-photo { margin: 16px 0; }
.notif-photo img { max-width: 300px; border-radius: 8px; }

.notif-meta { color: #64748B; font-size: 12px; margin-bottom: 16px; }

.notif-actions { display: flex; gap: 8px; margin-top: 16px; padding-top: 16px; border-top: 1px solid #1E2636; }
.btn-approve { background: #10B981; color: #FFFFFF; padding: 8px 16px; border-radius: 8px; border: none; cursor: pointer; font-weight: 500; }
.btn-approve:hover { background: #059669; }
.btn-reject { background: #EF4444; color: #FFFFFF; padding: 8px 16px; border-radius: 8px; border: none; cursor: pointer; font-weight: 500; }
.btn-reject:hover { background: #DC2626; }
.btn-edit { background: #3B82F6; color: #FFFFFF; padding: 8px 16px; border-radius: 8px; border: none; cursor: pointer; font-weight: 500; }
.btn-edit:hover { background: #2563EB; }

.dialog-overlay { position: fixed; inset: 0; background: rgba(0, 0, 0, 0.7); display: flex; align-items: center; justify-content: center; z-index: 1000; }
.dialog-card { background: #0F131C; border-radius: 16px; padding: 32px; max-width: 500px; width: 90%; }
.dialog-title { font-size: 20px; font-weight: 600; color: #E2E8F0; margin: 0 0 24px; }
.dialog-card p { color: #94A3B8; margin-bottom: 24px; }

.form-group { margin-bottom: 20px; }
.form-group label { display: block; color: #94A3B8; font-size: 14px; margin-bottom: 8px; }
.form-input, .form-textarea { width: 100%; background: #161D2B; border: 1px solid #1E2636; border-radius: 8px; padding: 12px; color: #E2E8F0; font-size: 14px; }
.form-input:focus, .form-textarea:focus { outline: none; border-color: #38BDF8; }

.dialog-actions { display: flex; gap: 12px; justify-content: flex-end; }
.btn-primary { background: #38BDF8; color: #0A0D12; padding: 10px 20px; border-radius: 8px; border: none; cursor: pointer; font-weight: 600; }
.btn-primary:hover { background: #0EA5E9; }
.btn-secondary { background: #1E2636; color: #E2E8F0; padding: 10px 20px; border-radius: 8px; border: none; cursor: pointer; }
.btn-secondary:hover { background: #2D3548; }
.btn-danger { background: #EF4444; color: #FFFFFF; padding: 10px 20px; border-radius: 8px; border: none; cursor: pointer; font-weight: 600; }
.btn-danger:hover { background: #DC2626; }
</style>
