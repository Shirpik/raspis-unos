<template>
  <Transition name="slide-up">
    <div v-if="showBanner" class="pwa-install-banner">
      <div class="banner-content">
        <div class="banner-icon">
          <svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"></path>
            <polyline points="7.5 4.21 12 6.81 16.5 4.21"></polyline>
            <polyline points="7.5 19.79 7.5 14.6 3 12"></polyline>
            <polyline points="21 12 16.5 14.6 16.5 19.79"></polyline>
            <polyline points="3.27 6.96 12 12.01 20.73 6.96"></polyline>
            <line x1="12" y1="22.08" x2="12" y2="12"></line>
          </svg>
        </div>
        <div class="banner-text">
          <div class="banner-title">Установить приложение</div>
          <div class="banner-description">
            Получайте уведомления об изменениях в расписании и новости для своей группы
          </div>
        </div>
        <div class="banner-actions">
          <button @click="handleInstall" class="btn-install">Установить</button>
          <button @click="handleDismiss" class="btn-dismiss">Позже</button>
        </div>
      </div>
    </div>
  </Transition>
</template>

<script setup>
import { ref, onMounted } from 'vue';
import { usePWA as usePwa } from '@/composables/usePwa';
import { useRouter } from 'vue-router';

const router = useRouter();
const { isInstallable, isInstalled, shouldShowPrompt, showInstallPrompt, dismissPrompt } = usePwa();
const showBanner = ref(false);

onMounted(() => {
  // Show banner if not installed and should show prompt
  setTimeout(() => {
    if (!isInstalled.value && isInstallable.value && shouldShowPrompt()) {
      showBanner.value = true;
    }
  }, 2000); // Show after 2 seconds
});

const handleInstall = async () => {
  showBanner.value = false;

  // Navigate to install instructions
  router.push('/pwa-install');
};

const handleDismiss = () => {
  dismissPrompt();
  showBanner.value = false;
};
</script>

<style scoped>
.pwa-install-banner {
  position: fixed;
  bottom: 0;
  left: 0;
  right: 0;
  background: linear-gradient(135deg, #1E2636 0%, #0F131C 100%);
  border-top: 1px solid rgba(56, 189, 248, 0.2);
  padding: 1rem;
  box-shadow: 0 -4px 20px rgba(0, 0, 0, 0.3);
  z-index: 9999;
}

.banner-content {
  max-width: 1200px;
  margin: 0 auto;
  display: grid;
  grid-template-columns: auto 1fr auto;
  gap: 1rem;
  align-items: center;
}

.banner-icon {
  width: 48px;
  height: 48px;
  background: rgba(56, 189, 248, 0.1);
  border-radius: 999px;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #38BDF8;
}

.banner-text {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
}

.banner-title {
  font-size: 1rem;
  font-weight: 600;
  color: #F8FAFC;
}

.banner-description {
  font-size: 0.875rem;
  color: #94A3B8;
  line-height: 1.4;
}

.banner-actions {
  display: flex;
  gap: 0.75rem;
}

.btn-install,
.btn-dismiss {
  padding: 0.625rem 1.25rem;
  border-radius: 999px;
  font-size: 0.875rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
  border: none;
}

.btn-install {
  background: #38BDF8;
  color: #0A0D12;
}

.btn-install:hover {
  background: #0EA5E9;
  transform: translateY(-1px);
}

.btn-dismiss {
  background: transparent;
  color: #94A3B8;
  border: 1px solid rgba(148, 163, 184, 0.3);
}

.btn-dismiss:hover {
  background: rgba(148, 163, 184, 0.1);
  color: #F8FAFC;
}

.slide-up-enter-active,
.slide-up-leave-active {
  transition: transform 0.3s ease-out, opacity 0.3s ease-out;
}

.slide-up-enter-from,
.slide-up-leave-to {
  transform: translateY(100%);
  opacity: 0;
}

@media (max-width: 768px) {
  .banner-content {
    grid-template-columns: auto 1fr;
    gap: 0.75rem;
  }

  .banner-actions {
    grid-column: 1 / -1;
    justify-content: stretch;
  }

  .btn-install,
  .btn-dismiss {
    flex: 1;
  }
}
</style>
