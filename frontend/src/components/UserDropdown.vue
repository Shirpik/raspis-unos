<template>
  <div class="user-dropdown" :class="{ collapsed }">
    <button
      class="user-button"
      @click="isOpen = !isOpen"
      :title="collapsed ? user.name : ''"
    >
      <div class="user-avatar">
        <User :size="20" />
      </div>
      <div v-if="!collapsed" class="user-info">
        <span class="user-name">{{ user.name }}</span>
        <span class="user-role">{{ user.role }}</span>
      </div>
      <ChevronDown v-if="!collapsed" :size="16" class="user-chevron" :class="{ open: isOpen }" />
    </button>

    <Teleport to="body">
      <div v-if="isOpen" class="dropdown-overlay" @click="isOpen = false"></div>
      <div v-if="isOpen" class="dropdown-menu" :style="menuStyle">
        <div class="dropdown-section">
          <button
            v-for="item in topItems"
            :key="item.label"
            class="dropdown-item"
            @click="handleItemClick(item)"
          >
            <component :is="item.icon" :size="18" />
            <span>{{ item.label }}</span>
          </button>
        </div>

        <div class="dropdown-divider"></div>

        <div class="dropdown-section">
          <button class="dropdown-item" @click="handleLogout">
            <LogOut :size="18" />
            <span>Выйти</span>
          </button>
        </div>
      </div>
    </Teleport>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import { useAuthStore } from '../stores/auth.js'
import { User, ChevronDown, Settings, LogOut } from 'lucide-vue-next'

const props = defineProps({
  collapsed: Boolean
})

const router = useRouter()
const auth = useAuthStore()
const isOpen = ref(false)
const menuStyle = ref({})

const user = computed(() => ({
  name: auth.username || 'Диспетчер',
  role: 'Администратор'
}))

const topItems = [
  { label: 'Настройки', icon: Settings, action: 'settings' }
]

const updateMenuPosition = () => {
  const button = document.querySelector('.user-button')
  if (button) {
    const rect = button.getBoundingClientRect()
    menuStyle.value = {
      position: 'fixed',
      bottom: `${window.innerHeight - rect.top + 8}px`,
      left: `${rect.left}px`,
      minWidth: `${rect.width}px`
    }
  }
}

const handleItemClick = (item) => {
  isOpen.value = false
  if (item.action === 'settings') {
    router.push('/settings')
  }
}

const handleLogout = async () => {
  isOpen.value = false
  await auth.logout()
  router.push('/')
}

onMounted(() => {
  updateMenuPosition()
  window.addEventListener('resize', updateMenuPosition)
})

onUnmounted(() => {
  window.removeEventListener('resize', updateMenuPosition)
})
</script>

<style scoped>
.user-dropdown {
  width: 100%;
}

.user-button {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 12px;
  background: none;
  border: 1px solid var(--border);
  border-radius: var(--radius);
  cursor: pointer;
  transition: all var(--transition);
  color: var(--text-secondary);
}

.user-dropdown.collapsed .user-button {
  justify-content: center;
  padding: 10px;
}

.user-button:hover {
  background: var(--bg-tertiary);
  border-color: var(--border-strong);
}

.user-avatar {
  width: 32px;
  height: 32px;
  border-radius: 50%;
  background: var(--accent-light);
  color: var(--accent);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.user-info {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  min-width: 0;
}

.user-name {
  font-size: 14px;
  font-weight: 500;
  color: var(--text-primary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  width: 100%;
}

.user-role {
  font-size: 12px;
  color: var(--text-muted);
}

.user-chevron {
  flex-shrink: 0;
  transition: transform var(--transition);
}

.user-chevron.open {
  transform: rotate(180deg);
}

.dropdown-overlay {
  position: fixed;
  inset: 0;
  z-index: 1000;
}

.dropdown-menu {
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  box-shadow: 0 10px 40px rgba(0, 0, 0, 0.5);
  z-index: 1001;
  min-width: 200px;
  backdrop-filter: blur(12px);
  animation: dropdown-appear 0.15s ease-out;
}

@keyframes dropdown-appear {
  from {
    opacity: 0;
    transform: translateY(8px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

.dropdown-section {
  padding: 8px;
}

.dropdown-item {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 12px;
  background: none;
  border: none;
  border-radius: var(--radius);
  color: var(--text-secondary);
  font-size: 14px;
  cursor: pointer;
  transition: all var(--transition);
  text-align: left;
}

.dropdown-item:hover {
  background: var(--bg-tertiary);
  color: var(--text-primary);
}

.dropdown-divider {
  height: 1px;
  background: var(--border);
  margin: 4px 0;
}
</style>
