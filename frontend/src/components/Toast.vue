<template>
  <Teleport to="body">
    <div class="toast-container">
      <TransitionGroup name="toast">
        <div
          v-for="t in toasts"
          :key="t.id"
          class="toast"
          :class="`toast-${t.type}`"
        >
          <component :is="icons[t.type]" :size="18" class="toast-icon" />
          <span class="toast-msg">{{ t.message }}</span>
        </div>
      </TransitionGroup>
    </div>
  </Teleport>
</template>

<script setup>
import { useToast } from '../composables/useToast.js'
import { CheckCircle, XCircle, AlertTriangle, Info } from 'lucide-vue-next'

const { toasts } = useToast()
const icons = {
  success: CheckCircle,
  error: XCircle,
  warning: AlertTriangle,
  info: Info
}
</script>

<style scoped>
.toast-container {
  position: fixed; bottom: 24px; right: 24px; z-index: 2000;
  display: flex; flex-direction: column; gap: 10px;
  pointer-events: none;
}
.toast {
  display: flex; align-items: center; gap: 12px;
  padding: 14px 18px;
  border-radius: var(--radius);
  font-size: 14px; font-weight: 500;
  min-width: 280px; max-width: 400px;
  pointer-events: all;
  box-shadow: 0 12px 32px rgba(0,0,0,0.5), 0 0 0 1px rgba(255,255,255,0.05);
  border: 1px solid;
  backdrop-filter: blur(12px);
  animation: toast-appear 0.3s cubic-bezier(0.4, 0, 0.2, 1);
}
.toast-success {
  background: rgba(5, 46, 22, 0.95);
  border-color: rgba(34, 197, 94, 0.3);
  color: #86EFAC;
}
.toast-error {
  background: rgba(28, 10, 10, 0.95);
  border-color: rgba(239, 68, 68, 0.3);
  color: #FCA5A5;
}
.toast-warning {
  background: rgba(28, 18, 0, 0.95);
  border-color: rgba(234, 179, 8, 0.3);
  color: #FCD34D;
}
.toast-info {
  background: rgba(12, 26, 46, 0.95);
  border-color: rgba(59, 130, 246, 0.3);
  color: #7DD3FC;
}
.toast-icon { flex-shrink: 0; }
.toast-msg { line-height: 1.4; }

@keyframes toast-appear {
  from {
    opacity: 0;
    transform: translateX(100%) scale(0.95);
  }
  to {
    opacity: 1;
    transform: translateX(0) scale(1);
  }
}

.toast-enter-active {
  transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
}
.toast-leave-active {
  transition: all 0.25s ease;
}
.toast-enter-from {
  opacity: 0;
  transform: translateX(100%) scale(0.95);
}
.toast-leave-to {
  opacity: 0;
  transform: translateX(24px) scale(0.9);
}

@media (max-width: 480px) {
  .toast-container {
    left: 16px; right: 16px; bottom: 16px;
    align-items: stretch;
  }
  .toast {
    min-width: 0;
    padding: 12px 16px;
  }
}
</style>
