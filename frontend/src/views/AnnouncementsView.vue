<template>
  <div class="announcements-page">
    <div class="page-header">
      <div>
        <h1 class="page-title">Рассылки</h1>
        <p class="page-desc">Создание и управление уведомлениями для студентов</p>
      </div>
      <button class="btn btn-primary" @click="showCreateModal = true">
        <Plus :size="18" />
        <span>Создать рассылку</span>
      </button>
    </div>

    <!-- Loading -->
    <div v-if="loading" class="loading-grid">
      <Skeleton v-for="i in 4" :key="i" class="h-32" />
    </div>

    <!-- Empty state -->
    <div v-else-if="announcements.length === 0" class="empty-state">
      <MessageSquare :size="48" style="color: var(--text-muted)" />
      <h3>Рассылок пока нет</h3>
      <p>Создайте первую рассылку для студентов</p>
    </div>

    <!-- List -->
    <div v-else class="announcements-grid">
      <div v-for="item in sortedAnnouncements" :key="item.id" class="announcement-card">
        <div class="card-header">
          <h3>{{ item.title }}</h3>
          <div class="card-actions">
            <button class="icon-btn" @click="sendPush(item.id)" title="Отправить уведомление">
              <Send :size="16" />
            </button>
            <button class="icon-btn" @click="editAnnouncement(item)" title="Редактировать">
              <Edit2 :size="16" />
            </button>
            <button class="icon-btn danger" @click="deleteAnnouncement(item.id)" title="Удалить">
              <Trash2 :size="16" />
            </button>
          </div>
        </div>

        <p class="card-message">{{ item.message }}</p>

        <div class="card-footer">
          <div class="card-target">
            <Users :size="14" />
            <span>{{ getTargetLabel(item) }}</span>
          </div>
          <span class="card-date">{{ formatDate(item.created_at) }}</span>
        </div>
      </div>
    </div>

    <!-- Create/Edit Modal -->
    <Teleport to="body">
      <div v-if="showCreateModal" class="modal-overlay" @click.self="closeModal">
        <div class="modal-content">
          <div class="modal-header">
            <h2>{{ editingId ? 'Редактировать рассылку' : 'Новая рассылка' }}</h2>
            <button class="icon-btn" @click="closeModal">
              <X :size="20" />
            </button>
          </div>

          <div class="modal-body">
            <div class="form-group">
              <label class="form-label">Заголовок</label>
              <input
                v-model="form.title"
                type="text"
                class="form-input"
                placeholder="Введите заголовок"
                maxlength="100"
              />
            </div>

            <div class="form-group">
              <label class="form-label">Сообщение</label>
              <textarea
                v-model="form.message"
                class="form-textarea"
                rows="5"
                placeholder="Введите текст сообщения"
                maxlength="500"
              ></textarea>
              <span class="char-count">{{ form.message.length }}/500</span>
            </div>

            <div class="form-group">
              <label class="form-label">Получатели</label>
              <div class="radio-group">
                <label class="radio-item">
                  <input v-model="form.target" type="radio" value="all" />
                  <span>Все студенты</span>
                </label>
                <label class="radio-item">
                  <input v-model="form.target" type="radio" value="course" />
                  <span>Конкретный курс</span>
                </label>
                <label class="radio-item">
                  <input v-model="form.target" type="radio" value="groups" />
                  <span>Конкретные группы</span>
                </label>
              </div>
            </div>

            <div v-if="form.target === 'course'" class="form-group">
              <label class="form-label">Курс</label>
              <select v-model.number="form.targetCourse" class="form-select">
                <option :value="null">Выберите курс</option>
                <option :value="1">1 курс</option>
                <option :value="2">2 курс</option>
                <option :value="3">3 курс</option>
                <option :value="4">4 курс</option>
              </select>
            </div>

            <div v-if="form.target === 'groups'" class="form-group">
              <label class="form-label">Группы</label>
              <div v-if="loadingGroups" class="info-text">Загрузка групп...</div>
              <div v-else class="checkbox-group">
                <label v-for="group in groups" :key="group.group_index" class="checkbox-item">
                  <input
                    v-model="form.targetGroups"
                    type="checkbox"
                    :value="group.group_index"
                  />
                  <span>{{ group.group_name }}</span>
                </label>
              </div>
            </div>
          </div>

          <div class="modal-footer">
            <button class="btn btn-secondary" @click="closeModal">
              Отмена
            </button>
            <button
              class="btn btn-primary"
              @click="saveAnnouncement"
              :disabled="!isFormValid || saving"
            >
              {{ saving ? 'Сохранение...' : (editingId ? 'Сохранить' : 'Создать') }}
            </button>
          </div>
        </div>
      </div>
    </Teleport>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { api } from '../api/index.js'
import { Plus, MessageSquare, Users, Edit2, Trash2, X, Send } from 'lucide-vue-next'
import Skeleton from '../components/ui/Skeleton.vue'
import { useToast } from '../composables/useToast.js'

const toast = useToast()

const loading = ref(false)
const loadingGroups = ref(false)
const saving = ref(false)
const showCreateModal = ref(false)
const editingId = ref(null)
const announcements = ref([])
const groups = ref([])

const form = ref({
  title: '',
  message: '',
  target: 'all',
  targetCourse: null,
  targetGroups: []
})

onMounted(() => {
  loadAnnouncements()
  loadGroups()
})

async function loadAnnouncements() {
  loading.value = true
  try {
    const result = await api.announcements.list()
    if (result.ok) {
      announcements.value = result.data || []
    } else {
      toast.error('Не удалось загрузить рассылки')
    }
  } catch (err) {
    console.error('Failed to load announcements:', err)
    toast.error('Ошибка при загрузке рассылок')
  } finally {
    loading.value = false
  }
}

async function loadGroups() {
  loadingGroups.value = true
  try {
    const result = await api.groups.list()
    if (result.ok) {
      groups.value = result.data || []
    }
  } catch (err) {
    console.error('Failed to load groups:', err)
  } finally {
    loadingGroups.value = false
  }
}

const sortedAnnouncements = computed(() => {
  return [...announcements.value].sort((a, b) => (b.created_at || 0) - (a.created_at || 0))
})

const isFormValid = computed(() => {
  if (!form.value.title.trim() || !form.value.message.trim()) return false
  if (form.value.target === 'course' && form.value.targetCourse === null) return false
  if (form.value.target === 'groups' && form.value.targetGroups.length === 0) return false
  return true
})

function getTargetLabel(item) {
  if (item.all_groups) return 'Все студенты'
  if (item.target_course >= 0) return `${item.target_course} курс`
  if (item.target_groups && item.target_groups.length > 0) {
    return `${item.target_groups.length} ${pluralize(item.target_groups.length, 'группа', 'группы', 'групп')}`
  }
  return 'Не указано'
}

function pluralize(n, one, few, many) {
  if (n % 10 === 1 && n % 100 !== 11) return one
  if ([2, 3, 4].includes(n % 10) && ![12, 13, 14].includes(n % 100)) return few
  return many
}

function formatDate(timestamp) {
  if (!timestamp) return ''
  const date = new Date(parseInt(timestamp) * 1000)
  return date.toLocaleString('ru-RU', {
    day: 'numeric',
    month: 'short',
    year: 'numeric',
    hour: '2-digit',
    minute: '2-digit'
  })
}

function editAnnouncement(item) {
  editingId.value = item.id
  form.value = {
    title: item.title,
    message: item.message,
    target: item.all_groups ? 'all' : (item.target_course >= 0 ? 'course' : 'groups'),
    targetCourse: item.target_course >= 0 ? item.target_course : null,
    targetGroups: item.target_groups || []
  }
  showCreateModal.value = true
}

async function saveAnnouncement() {
  if (!isFormValid.value) return

  saving.value = true
  try {
    const payload = {
      title: form.value.title.trim(),
      message: form.value.message.trim(),
      all_groups: form.value.target === 'all',
      target_course: form.value.target === 'course' ? form.value.targetCourse : -1,
      target_groups: form.value.target === 'groups' ? form.value.targetGroups : []
    }

    const result = editingId.value
      ? await api.announcements.update(editingId.value, payload)
      : await api.announcements.create(payload)

    if (result.ok) {
      toast.success(editingId.value ? 'Рассылка обновлена' : 'Рассылка создана')
      closeModal()
      loadAnnouncements()
    } else {
      toast.error(result.data?.message || 'Не удалось сохранить')
    }
  } catch (err) {
    console.error('Save announcement error:', err)
    toast.error('Ошибка при сохранении')
  } finally {
    saving.value = false
  }
}

async function deleteAnnouncement(id) {
  if (!confirm('Удалить эту рассылку?')) return

  try {
    const result = await api.announcements.remove(id)
    if (result.ok) {
      toast.success('Рассылка удалена')
      announcements.value = announcements.value.filter(a => a.id !== id)
    } else {
      toast.error('Не удалось удалить')
    }
  } catch (err) {
    console.error('Delete announcement error:', err)
    toast.error('Ошибка при удалении')
  }
}

async function sendPush(id) {
  if (!confirm('Отправить push-уведомление всем подписчикам?')) return

  try {
    const result = await api.push.send(id)
    if (result.ok) {
      const count = result.data?.sent || 0
      toast.success(`Отправлено ${count} ${pluralize(count, 'уведомление', 'уведомления', 'уведомлений')}`)
    } else {
      toast.error(result.data?.message || 'Не удалось отправить')
    }
  } catch (err) {
    console.error('Send push error:', err)
    toast.error('Ошибка при отправке')
  }
}

function closeModal() {
  showCreateModal.value = false
  editingId.value = null
  form.value = {
    title: '',
    message: '',
    target: 'all',
    targetCourse: null,
    targetGroups: []
  }
}
</script>

<style scoped>
.announcements-page {
  max-width: 1200px;
  margin: 0 auto;
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 24px;
  margin-bottom: 32px;
}

.page-title {
  font-size: clamp(24px, 3vw, 32px);
  font-weight: 600;
  color: var(--text-primary);
  margin: 0 0 8px;
  letter-spacing: -0.02em;
}

.page-desc {
  font-size: 15px;
  color: var(--text-muted);
  margin: 0;
}

.btn {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 10px 20px;
  border: none;
  border-radius: var(--radius);
  font-size: 14px;
  font-weight: 500;
  cursor: pointer;
  transition: all var(--transition);
  white-space: nowrap;
}

.btn-primary {
  background: var(--accent);
  color: white;
}

.btn-primary:hover:not(:disabled) {
  background: #e9965c;
}

.btn-primary:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.btn-secondary {
  background: var(--bg-tertiary);
  color: var(--text-primary);
  border: 1px solid var(--border);
}

.btn-secondary:hover {
  background: var(--bg-quaternary);
}

.loading-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
  gap: 16px;
}

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 80px 24px;
  text-align: center;
}

.empty-state h3 {
  font-size: 20px;
  font-weight: 600;
  color: var(--text-primary);
  margin: 16px 0 8px;
}

.empty-state p {
  font-size: 15px;
  color: var(--text-muted);
  margin: 0;
}

.announcements-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
  gap: 16px;
}

.announcement-card {
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  padding: 20px;
  display: flex;
  flex-direction: column;
  gap: 12px;
  transition: all var(--transition);
}

.announcement-card:hover {
  border-color: var(--border-strong);
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 12px;
}

.card-header h3 {
  font-size: 16px;
  font-weight: 600;
  color: var(--text-primary);
  margin: 0;
  flex: 1;
  line-height: 1.4;
}

.card-actions {
  display: flex;
  gap: 4px;
}

.icon-btn {
  width: 32px;
  height: 32px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: transparent;
  border: none;
  color: var(--text-secondary);
  cursor: pointer;
  border-radius: var(--radius);
  transition: all var(--transition);
}

.icon-btn:hover {
  background: var(--bg-tertiary);
  color: var(--text-primary);
}

.icon-btn.danger:hover {
  background: rgba(239, 68, 68, 0.1);
  color: #ef4444;
}

.card-message {
  font-size: 14px;
  color: var(--text-secondary);
  line-height: 1.6;
  margin: 0;
  white-space: pre-wrap;
}

.card-footer {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
  margin-top: auto;
  padding-top: 12px;
  border-top: 1px solid var(--border);
}

.card-target {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
  color: var(--text-muted);
}

.card-date {
  font-size: 12px;
  color: var(--text-muted);
  white-space: nowrap;
}

/* Modal */
.modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.6);
  backdrop-filter: blur(4px);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
  padding: 24px;
}

.modal-content {
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  width: 100%;
  max-width: 600px;
  max-height: 90vh;
  display: flex;
  flex-direction: column;
}

.modal-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 20px 24px;
  border-bottom: 1px solid var(--border);
}

.modal-header h2 {
  font-size: 20px;
  font-weight: 600;
  color: var(--text-primary);
  margin: 0;
}

.modal-body {
  padding: 24px;
  overflow-y: auto;
}

.modal-footer {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
  padding: 16px 24px;
  border-top: 1px solid var(--border);
}

.form-group {
  margin-bottom: 20px;
}

.form-group:last-child {
  margin-bottom: 0;
}

.form-label {
  display: block;
  font-size: 14px;
  font-weight: 500;
  color: var(--text-primary);
  margin-bottom: 8px;
}

.form-input,
.form-textarea,
.form-select {
  width: 100%;
  padding: 10px 12px;
  background: var(--bg-primary);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  color: var(--text-primary);
  font-size: 14px;
  transition: all var(--transition);
}

.form-input:focus,
.form-textarea:focus,
.form-select:focus {
  outline: none;
  border-color: var(--accent);
}

.form-textarea {
  resize: vertical;
  font-family: inherit;
  line-height: 1.5;
}

.char-count {
  display: block;
  text-align: right;
  font-size: 12px;
  color: var(--text-muted);
  margin-top: 4px;
}

.radio-group,
.checkbox-group {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.radio-item,
.checkbox-item {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 10px 12px;
  background: var(--bg-primary);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  cursor: pointer;
  transition: all var(--transition);
}

.radio-item:hover,
.checkbox-item:hover {
  border-color: var(--border-strong);
  background: var(--bg-tertiary);
}

.radio-item input,
.checkbox-item input {
  cursor: pointer;
}

.info-text {
  font-size: 14px;
  color: var(--text-muted);
  padding: 12px;
  background: var(--bg-primary);
  border-radius: var(--radius);
}

@media (max-width: 768px) {
  .page-header {
    flex-direction: column;
    align-items: stretch;
  }

  .announcements-grid {
    grid-template-columns: 1fr;
  }

  .modal-overlay {
    padding: 0;
  }

  .modal-content {
    max-height: 100vh;
    border-radius: 0;
  }
}
</style>
