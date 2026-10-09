<template>
  <Teleport to="body">
    <div v-if="show" class="modal-overlay">
      <div class="welcome-modal" @click.stop>
        <div class="welcome-header">
          <div class="welcome-icon">
            <Sparkles :size="48" />
          </div>
          <h2>Добро пожаловать!</h2>
          <p>Настройте приложение для персонализированного опыта</p>
        </div>

        <div class="welcome-content">
          <!-- Шаг 1: Выбор курса -->
          <div v-if="step === 1" class="welcome-step">
            <div class="step-header">
              <div class="step-icon">
                <GraduationCap :size="24" />
              </div>
              <h3>Выберите ваш курс</h3>
              <p>Это поможет показывать актуальное расписание</p>
            </div>

            <div class="year-grid">
              <button
                v-for="year in [1, 2, 3, 4]"
                :key="year"
                class="year-option"
                :class="{ selected: selectedYear === year }"
                @click="selectedYear = year"
              >
                <span class="year-number">{{ year }}</span>
                <span class="year-label">курс</span>
              </button>
            </div>
          </div>

          <!-- Шаг 2: Выбор группы -->
          <div v-if="step === 2" class="welcome-step">
            <div class="step-header">
              <div class="step-icon">
                <Users :size="24" />
              </div>
              <h3>Выберите вашу группу</h3>
              <p>Можно изменить позже в настройках</p>
            </div>

            <div v-if="availableGroups.length > 0" class="group-list">
              <button
                v-for="group in availableGroups"
                :key="group"
                class="group-option"
                :class="{ selected: selectedGroup === group }"
                @click="selectedGroup = group"
              >
                {{ group }}
              </button>
            </div>
            <div v-else class="empty-notice">
              <p>Загрузка групп...</p>
            </div>
          </div>

          <!-- Шаг 3: Настройка уведомлений -->
          <div v-if="step === 3" class="welcome-step">
            <div class="step-header">
              <div class="step-icon">
                <Bell :size="24" />
              </div>
              <h3>Включить уведомления?</h3>
              <p>Получайте важную информацию об изменениях в расписании и новости вашей группы</p>
            </div>

            <div class="notification-benefits">
              <div class="benefit-item">
                <CheckCircle2 :size="20" />
                <span>Уведомления об изменениях в расписании</span>
              </div>
              <div class="benefit-item">
                <CheckCircle2 :size="20" />
                <span>Новости и объявления для вашей группы</span>
              </div>
              <div class="benefit-item">
                <CheckCircle2 :size="20" />
                <span>Важные сообщения от администрации</span>
              </div>
            </div>

            <div class="notification-note">
              <Info :size="16" />
              <span>Вы сможете изменить настройки позже</span>
            </div>
          </div>
        </div>

        <div class="welcome-footer">
          <div class="step-indicators">
            <span
              v-for="s in 3"
              :key="s"
              class="step-dot"
              :class="{ active: s === step }"
            />
          </div>

          <div class="action-buttons">
            <button
              v-if="step > 1"
              class="btn btn-secondary"
              @click="step--"
            >
              <ChevronLeft :size="18" />
              <span>Назад</span>
            </button>

            <button
              v-if="step < 3"
              class="btn btn-primary"
              :disabled="!canProceed"
              @click="nextStep"
            >
              <span>Далее</span>
              <ChevronRight :size="18" />
            </button>

            <button
              v-if="step === 3"
              class="btn btn-secondary"
              @click="finish(false)"
            >
              <span>Пропустить</span>
            </button>

            <button
              v-if="step === 3"
              class="btn btn-primary"
              @click="finish(true)"
            >
              <Bell :size="18" />
              <span>Включить</span>
            </button>
          </div>
        </div>
      </div>
    </div>
  </Teleport>
</template>

<script setup>
import { ref, computed, watch } from 'vue'
import { Sparkles, GraduationCap, Users, Bell, CheckCircle2, Info, ChevronLeft, ChevronRight } from 'lucide-vue-next'
import { subscribeToPush } from '../utils/pwa.js'

const props = defineProps({
  show: Boolean,
  groups: {
    type: Array,
    default: () => []
  }
})

const emit = defineEmits(['close', 'complete'])

const step = ref(1)
const selectedYear = ref(null)
const selectedGroup = ref(null)

const availableGroups = computed(() => {
  if (!selectedYear.value || !props.groups.length) return []
  return props.groups
    .filter(g => g.year === selectedYear.value)
    .map(g => g.group_name)
})

const canProceed = computed(() => {
  if (step.value === 1) return selectedYear.value !== null
  if (step.value === 2) return selectedGroup.value !== null
  return true
})

watch(() => props.show, (newVal) => {
  if (newVal) {
    step.value = 1
    selectedYear.value = null
    selectedGroup.value = null
  }
})

watch(selectedYear, () => {
  selectedGroup.value = null
})

function nextStep() {
  if (!canProceed.value) return

  if (step.value === 1) {
    step.value = 2
  } else if (step.value === 2) {
    if (selectedYear.value) {
      localStorage.setItem('user_year', selectedYear.value.toString())
    }
    if (selectedGroup.value) {
      localStorage.setItem('user_group', selectedGroup.value)
    }
    step.value = 3
  }
}

async function finish(enableNotifications) {
  if (enableNotifications) {
    try {
      await subscribeToPush()
      console.log('Push notifications enabled')
    } catch (err) {
      console.error('Failed to enable notifications:', err)
    }
  }

  emit('complete', {
    year: selectedYear.value,
    group: selectedGroup.value,
    notifications: enableNotifications
  })
  emit('close')
}
</script>


<style scoped>
.modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(5, 7, 12, 0.85);
  backdrop-filter: blur(8px);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
  padding: 20px;
  animation: fadeIn 0.2s ease-out;
}

@keyframes fadeIn {
  from { opacity: 0; }
  to { opacity: 1; }
}

.welcome-modal {
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  width: 100%;
  max-width: 500px;
  max-height: 90vh;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  animation: slideUp 0.3s ease-out;
}

@keyframes slideUp {
  from {
    opacity: 0;
    transform: translateY(20px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

.welcome-header {
  padding: 32px 24px 24px;
  text-align: center;
  border-bottom: 1px solid var(--border);
}

.welcome-icon {
  width: 80px;
  height: 80px;
  margin: 0 auto 20px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: linear-gradient(135deg, var(--accent-light), var(--accent));
  border-radius: var(--radius-lg);
  color: white;
}

.welcome-header h2 {
  font-size: 24px;
  font-weight: 600;
  color: var(--text-primary);
  margin: 0 0 8px;
  letter-spacing: -0.02em;
}

.welcome-header p {
  font-size: 14px;
  color: var(--text-muted);
  margin: 0;
}

.welcome-content {
  flex: 1;
  padding: 24px;
  overflow-y: auto;
}

.welcome-step {
  animation: fadeIn 0.2s ease-out;
}

.step-header {
  text-align: center;
  margin-bottom: 24px;
}

.step-icon {
  width: 56px;
  height: 56px;
  margin: 0 auto 16px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--accent-light);
  border-radius: var(--radius);
  color: var(--accent);
}

.step-header h3 {
  font-size: 20px;
  font-weight: 600;
  color: var(--text-primary);
  margin: 0 0 8px;
}

.step-header p {
  font-size: 14px;
  color: var(--text-muted);
  margin: 0;
}

.year-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 12px;
}

.year-option {
  padding: 24px 16px;
  background: var(--bg-tertiary);
  border: 2px solid var(--border);
  border-radius: var(--radius);
  cursor: pointer;
  transition: all var(--transition);
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 4px;
}

.year-option:hover {
  border-color: var(--border-strong);
  transform: translateY(-2px);
}

.year-option.selected {
  background: var(--accent-light);
  border-color: var(--accent);
}

.year-number {
  font-size: 32px;
  font-weight: 700;
  color: var(--text-primary);
  line-height: 1;
}

.year-label {
  font-size: 14px;
  color: var(--text-muted);
}

.group-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
  max-height: 300px;
  overflow-y: auto;
}

.group-option {
  padding: 14px 16px;
  background: var(--bg-tertiary);
  border: 2px solid var(--border);
  border-radius: var(--radius);
  cursor: pointer;
  transition: all var(--transition);
  text-align: left;
  font-size: 15px;
  font-weight: 500;
  color: var(--text-secondary);
}

.group-option:hover {
  border-color: var(--border-strong);
  background: var(--bg-primary);
}

.group-option.selected {
  background: var(--accent-light);
  border-color: var(--accent);
  color: var(--accent);
}

.notification-benefits {
  display: flex;
  flex-direction: column;
  gap: 16px;
  margin-bottom: 20px;
}

.benefit-item {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px;
  background: var(--bg-tertiary);
  border-radius: var(--radius);
  color: var(--text-secondary);
  font-size: 14px;
}

.benefit-item svg {
  color: var(--accent);
  flex-shrink: 0;
}

.notification-note {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 12px;
  background: var(--bg-primary);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  color: var(--text-muted);
  font-size: 13px;
}

.notification-note svg {
  color: var(--accent);
  flex-shrink: 0;
}

.empty-notice {
  text-align: center;
  padding: 40px 20px;
  color: var(--text-muted);
}

.welcome-footer {
  padding: 20px 24px;
  border-top: 1px solid var(--border);
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.step-indicators {
  display: flex;
  justify-content: center;
  gap: 8px;
}

.step-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--border);
  transition: all var(--transition);
}

.step-dot.active {
  width: 24px;
  border-radius: 999px;
  background: var(--accent);
}

.action-buttons {
  display: flex;
  gap: 12px;
  justify-content: flex-end;
}

.btn {
  padding: 10px 20px;
  border-radius: var(--radius);
  font-size: 15px;
  font-weight: 500;
  display: flex;
  align-items: center;
  gap: 8px;
  cursor: pointer;
  transition: all var(--transition);
  border: none;
}

.btn-primary {
  background: var(--accent);
  color: white;
}

.btn-primary:hover:not(:disabled) {
  background: var(--accent-hover);
  transform: translateY(-1px);
}

.btn-primary:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.btn-secondary {
  background: var(--bg-tertiary);
  color: var(--text-secondary);
  border: 1px solid var(--border);
}

.btn-secondary:hover {
  background: var(--bg-primary);
  border-color: var(--border-strong);
}

@media (max-width: 640px) {
  .modal-overlay {
    padding: 0;
  }

  .welcome-modal {
    max-width: 100%;
    max-height: 100vh;
    border-radius: 0;
  }

  .year-grid {
    grid-template-columns: repeat(2, 1fr);
  }

  .action-buttons {
    flex-direction: column;
  }

  .btn {
    width: 100%;
    justify-content: center;
  }
}
</style>
