<template>
  <Teleport to="body">
    <div v-if="showPrompt" class="pwa-prompt-overlay">
      <div class="pwa-prompt">
        <div class="prompt-header">
          <div class="prompt-icon">
            <Smartphone :size="32" />
          </div>
          <button class="close-btn" @click="dismiss">
            <X :size="20" />
          </button>
        </div>

        <div class="prompt-content">
          <h2>Установите приложение</h2>
          <p>Получайте уведомления об изменениях в расписании и новостях для вашей группы</p>

          <ul class="benefits-list">
            <li>
              <Bell :size="18" />
              <span>Push-уведомления о новостях</span>
            </li>
            <li>
              <Zap :size="18" />
              <span>Быстрый доступ с домашнего экрана</span>
            </li>
            <li>
              <Wifi :size="18" />
              <span>Работает офлайн</span>
            </li>
          </ul>
        </div>

        <div class="prompt-actions">
          <button class="btn btn-secondary" @click="dismiss">
            Позже
          </button>
          <button class="btn btn-primary" @click="install">
            Установить
          </button>
        </div>
      </div>
    </div>
  </Teleport>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { Smartphone, Bell, Zap, Wifi, X } from 'lucide-vue-next'
import { getPwaInstallPrompt, showPwaInstallPrompt } from '../utils/pwa.js'

const router = useRouter()
const showPrompt = ref(false)

const PWA_PROMPT_KEY = 'pwa_prompt_dismissed_count'
const PWA_PROMPT_LAST = 'pwa_prompt_last_shown'

onMounted(() => {
  checkAndShowPrompt()
})

function checkAndShowPrompt() {
  const dismissCount = parseInt(localStorage.getItem(PWA_PROMPT_KEY) || '0')
  const lastShown = parseInt(localStorage.getItem(PWA_PROMPT_LAST) || '0')
  const now = Date.now()

  // Показывать каждый 3-й раз после отказа, но не чаще раза в день
  if (dismissCount % 3 === 0 && (now - lastShown) > 86400000) {
    const deferredPrompt = getPwaInstallPrompt()
    if (deferredPrompt) {
      showPrompt.value = true
      localStorage.setItem(PWA_PROMPT_LAST, now.toString())
    }
  }
}

async function install() {
  showPrompt.value = false

  const accepted = await showPwaInstallPrompt()

  if (accepted) {
    localStorage.removeItem(PWA_PROMPT_KEY)
    localStorage.removeItem(PWA_PROMPT_LAST)
  } else {
    // Если браузерный промпт не сработал, отправляем на страницу инструкций
    router.push('/pwa-install')
  }
}

function dismiss() {
  showPrompt.value = false
  const count = parseInt(localStorage.getItem(PWA_PROMPT_KEY) || '0')
  localStorage.setItem(PWA_PROMPT_KEY, (count + 1).toString())
}
</script>

<style scoped>
.pwa-prompt-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.7);
  backdrop-filter: blur(4px);
  display: flex;
  align-items: flex-end;
  justify-content: center;
  z-index: 2000;
  padding: 24px;
  animation: fadeIn 0.3s ease;
}

@keyframes fadeIn {
  from {
    opacity: 0;
  }
  to {
    opacity: 1;
  }
}

.pwa-prompt {
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  width: 100%;
  max-width: 480px;
  padding: 24px;
  animation: slideUp 0.3s ease;
}

@keyframes slideUp {
  from {
    transform: translateY(100%);
  }
  to {
    transform: translateY(0);
  }
}

.prompt-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  margin-bottom: 20px;
}

.prompt-icon {
  width: 56px;
  height: 56px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--accent-light);
  border-radius: var(--radius);
  color: var(--accent);
}

.close-btn {
  width: 32px;
  height: 32px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: transparent;
  border: none;
  color: var(--text-muted);
  cursor: pointer;
  border-radius: var(--radius);
  transition: all var(--transition);
}

.close-btn:hover {
  background: var(--bg-tertiary);
  color: var(--text-primary);
}

.prompt-content h2 {
  font-size: 24px;
  font-weight: 600;
  color: var(--text-primary);
  margin: 0 0 12px;
  letter-spacing: -0.02em;
}

.prompt-content p {
  font-size: 15px;
  color: var(--text-secondary);
  line-height: 1.6;
  margin: 0 0 20px;
}

.benefits-list {
  list-style: none;
  display: flex;
  flex-direction: column;
  gap: 12px;
  margin-bottom: 24px;
}

.benefits-list li {
  display: flex;
  align-items: center;
  gap: 12px;
  font-size: 14px;
  color: var(--text-secondary);
  padding: 10px 12px;
  background: var(--bg-tertiary);
  border: 1px solid var(--border);
  border-radius: var(--radius);
}

.benefits-list li svg {
  color: var(--accent);
  flex-shrink: 0;
}

.prompt-actions {
  display: flex;
  gap: 12px;
}

.btn {
  flex: 1;
  justify-content: center;
}

@media (max-width: 640px) {
  .pwa-prompt-overlay {
    padding: 0;
    align-items: flex-end;
  }

  .pwa-prompt {
    max-width: 100%;
    border-radius: var(--radius-lg) var(--radius-lg) 0 0;
  }
}
</style>
