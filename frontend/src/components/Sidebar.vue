<template>
  <div class="sidebar-overlay" :class="{ active: isOpen }" @click="closeSidebar"></div>
  <aside class="sidebar" :class="{ open: isOpen, collapsed: isCollapsed }">
    <div class="sidebar-header">
      <slot name="header">
        <div class="sidebar-branding">
          <div class="brand-logo">
            <Calendar :size="24" />
          </div>
          <h2 v-if="!isCollapsed" class="brand-title">Расписание</h2>
        </div>
      </slot>
    </div>

    <nav class="sidebar-nav">
      <slot :collapsed="isCollapsed" />
    </nav>

    <div class="sidebar-footer">
      <slot name="footer" :collapsed="isCollapsed" />
    </div>

    <button
      v-if="collapsible"
      class="sidebar-toggle"
      @click="toggleCollapse"
      :title="isCollapsed ? 'Развернуть' : 'Свернуть'"
    >
      <PanelLeftClose v-if="!isCollapsed" :size="18" />
      <PanelLeftOpen v-else :size="18" />
    </button>
  </aside>
</template>

<script setup>
import { ref, computed, watch } from 'vue'
import { Calendar, PanelLeftClose, PanelLeftOpen } from 'lucide-vue-next'

const props = defineProps({
  modelValue: { type: Boolean, default: false },
  collapsible: { type: Boolean, default: true }
})

const emit = defineEmits(['update:modelValue', 'update:collapsed'])

const isCollapsed = ref(false)
const isOpen = computed(() => props.modelValue)

const toggleCollapse = () => {
  isCollapsed.value = !isCollapsed.value
  emit('update:collapsed', isCollapsed.value)
}

const closeSidebar = () => {
  emit('update:modelValue', false)
}
</script>

<style scoped>
.sidebar-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.5);
  backdrop-filter: blur(4px);
  opacity: 0;
  pointer-events: none;
  transition: opacity 0.2s;
  z-index: 998;
}

.sidebar-overlay.active {
  opacity: 1;
  pointer-events: all;
}

.sidebar {
  position: fixed;
  top: 0;
  left: 0;
  bottom: 0;
  width: 280px;
  background: rgba(17, 24, 39, 0.6);
  backdrop-filter: blur(12px);
  border-right: 1px solid var(--border);
  transform: translateX(-100%);
  transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
  z-index: 999;
  display: flex;
  flex-direction: column;
}

.sidebar.open {
  transform: translateX(0);
}

.sidebar.collapsed {
  width: 72px;
}

.sidebar-header {
  padding: 16px;
  border-bottom: 1px solid var(--border);
}

.sidebar-branding {
  display: flex;
  align-items: center;
  gap: 12px;
}

.brand-logo {
  width: 40px;
  height: 40px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--accent-light);
  color: var(--accent);
  border-radius: var(--radius);
  flex-shrink: 0;
}

.brand-title {
  font-size: 18px;
  font-weight: 600;
  color: var(--text-primary);
  white-space: nowrap;
  overflow: hidden;
}

.sidebar-nav {
  flex: 1;
  overflow-y: auto;
  padding: 12px 8px;
}

.sidebar-footer {
  padding: 12px 8px;
  border-top: 1px solid var(--border);
}

.sidebar-toggle {
  position: absolute;
  bottom: 16px;
  right: -12px;
  width: 24px;
  height: 24px;
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--text-secondary);
  cursor: pointer;
  transition: all var(--transition);
}

.sidebar-toggle:hover {
  background: var(--bg-tertiary);
  color: var(--text-primary);
  border-color: var(--border-strong);
}

@media (max-width: 899px) {
  .sidebar-toggle {
    display: none;
  }
}

@media (min-width: 900px) {
  .sidebar-overlay {
    display: none;
  }

  .sidebar {
    position: static;
    transform: translateX(0);
  }
}
</style>
