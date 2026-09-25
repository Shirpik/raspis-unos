<template>
  <Teleport to="body">
    <Transition name="modal-fade">
      <div v-if="modelValue" class="modal-backdrop" @click.self="close">
        <div :class="['modal-box', `modal-${size}`]" role="dialog" :aria-label="title">
          <div class="modal-header">
            <span class="modal-title">{{ title }}</span>
            <button class="btn-close" @click="close" aria-label="Закрыть">
              <X :size="18" />
            </button>
          </div>
          <div class="modal-body">
            <slot />
          </div>
          <div v-if="$slots.footer" class="modal-footer">
            <slot name="footer" />
          </div>
        </div>
      </div>
    </Transition>
  </Teleport>
</template>

<script setup>
import { onMounted, onUnmounted } from 'vue'
import { X } from 'lucide-vue-next'

defineProps({ modelValue: Boolean, title: String, size: { type: String, default: 'normal' } })
const emit = defineEmits(['update:modelValue'])

function close() { emit('update:modelValue', false) }

function onKey(e) { if (e.key === 'Escape') close() }
onMounted(() => document.addEventListener('keydown', onKey))
onUnmounted(() => document.removeEventListener('keydown', onKey))
</script>

<style scoped>
.modal-backdrop {
  position: fixed; inset: 0; z-index: 1000;
  background: rgba(0,0,0,0.75);
  backdrop-filter: blur(8px) saturate(120%);
  display: flex; align-items: center; justify-content: center;
  padding: 20px;
}
.modal-box {
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  width: 100%; max-width: 500px;
  max-height: 90vh;
  display: flex; flex-direction: column;
  box-shadow: 0 24px 80px rgba(0,0,0,0.7), 0 0 0 1px rgba(255,255,255,0.05);
  overflow: hidden;
}
.modal-wide { max-width: 1200px; }

.modal-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 20px 24px;
  border-bottom: 1px solid var(--border);
  background: var(--bg-primary);
  flex-shrink: 0;
}
.modal-title {
  font-size: 18px; font-weight: 600;
  letter-spacing: -0.02em;
  color: var(--text-primary);
}
.btn-close {
  display: flex; align-items: center; justify-content: center;
  width: 32px; height: 32px;
  border-radius: var(--radius-sm);
  background: transparent;
  border: 1px solid transparent;
  color: var(--text-muted);
  cursor: pointer;
  transition: all var(--transition);
}
.btn-close:hover {
  background: var(--bg-tertiary);
  border-color: var(--border);
  color: var(--text-primary);
  transform: rotate(90deg);
}

.modal-body {
  padding: 24px;
  overflow-y: auto;
  flex: 1;
  display: flex; flex-direction: column; gap: 20px;
}
.modal-footer {
  padding: 16px 24px;
  border-top: 1px solid var(--border);
  background: var(--bg-primary);
  display: flex; gap: 10px; justify-content: flex-end;
  flex-shrink: 0;
}

.modal-fade-enter-active, .modal-fade-leave-active {
  transition: opacity 0.3s ease;
}
.modal-fade-enter-from, .modal-fade-leave-to {
  opacity: 0;
}
.modal-fade-enter-active .modal-box, .modal-fade-leave-active .modal-box {
  transition: transform 0.3s cubic-bezier(0.4, 0, 0.2, 1);
}
.modal-fade-enter-from .modal-box {
  transform: scale(0.95) translateY(20px);
}
.modal-fade-leave-to .modal-box {
  transform: scale(0.97) translateY(-10px);
}

@media (max-width: 640px) {
  .modal-backdrop { padding: 12px; align-items: flex-end; }
  .modal-box {
    max-width: 100%;
    max-height: 95vh;
    border-bottom-left-radius: 0;
    border-bottom-right-radius: 0;
  }
  .modal-header { padding: 16px 18px; }
  .modal-title { font-size: 16px; }
  .modal-body { padding: 18px; gap: 16px; }
  .modal-footer { padding: 14px 18px; }
  .modal-fade-enter-from .modal-box {
    transform: translateY(100%);
  }
  .modal-fade-leave-to .modal-box {
    transform: translateY(100%);
  }
}
</style>
