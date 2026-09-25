<template>
  <div class="admin-layout">
    <aside class="sidebar" :class="{ collapsed: !sidebarOpen }">
      <div class="sidebar-header">
        <div class="sidebar-logo">
          <Calendar :size="24" />
          <span v-if="sidebarOpen" class="logo-text">Распис</span>
        </div>
        <button v-if="sidebarOpen" class="collapse-btn" @click="toggleSidebar">
          <PanelLeftClose :size="20" />
        </button>
      </div>

      <nav class="sidebar-nav">
        <template v-for="item in navItems" :key="item.to || item.label">
          <div v-if="item.children" class="nav-group">
            <button
              class="nav-item nav-parent"
              :class="{
                active: isGroupActive(item),
                expanded: expandedGroups[item.label]
              }"
              @click="toggleGroup(item.label)"
            >
              <component :is="item.icon" :size="20" />
              <span v-if="sidebarOpen" class="nav-label">{{ item.label }}</span>
              <ChevronDown
                v-if="sidebarOpen"
                :size="16"
                class="nav-chevron"
                :class="{ rotated: expandedGroups[item.label] }"
              />
            </button>
            <Transition name="submenu">
              <div v-if="expandedGroups[item.label] && sidebarOpen" class="nav-children">
                <router-link
                  v-for="child in item.children"
                  :key="child.to"
                  :to="child.to"
                  class="nav-item nav-child"
                  :class="{ active: isChildActive(child) }"
                >
                  <span class="nav-label">{{ child.label }}</span>
                </router-link>
              </div>
            </Transition>
          </div>
          <router-link
            v-else
            :to="item.to"
            class="nav-item"
            :class="{ active: $route.path === item.to }"
          >
            <component :is="item.icon" :size="20" />
            <span v-if="sidebarOpen" class="nav-label">{{ item.label }}</span>
            <span v-if="item.badge && sidebarOpen" class="nav-badge">{{ item.badge }}</span>
          </router-link>
        </template>
      </nav>

      <div class="sidebar-footer">
        <div v-if="user" class="user-menu">
          <button class="user-button" @click="toggleUserMenu">
            <div class="user-avatar">
              {{ userInitials }}
            </div>
            <div v-if="sidebarOpen" class="user-info">
              <div class="user-name">{{ user.name }}</div>
              <div class="user-role">{{ userRoleLabel }}</div>
            </div>
            <ChevronUp v-if="sidebarOpen" :size="16" class="chevron" :class="{ rotated: userMenuOpen }" />
          </button>

          <Transition name="dropdown">
            <div v-if="userMenuOpen" class="user-dropdown">
              <button class="dropdown-item" @click="goToSettings">
                <Settings :size="16" />
                <span>Настройки</span>
              </button>
              <button class="dropdown-item" @click="logout">
                <LogOut :size="16" />
                <span>Выйти</span>
              </button>
            </div>
          </Transition>
        </div>
      </div>
    </aside>

    <main class="main-content">
      <div class="main-header">
        <button v-if="!sidebarOpen" class="expand-btn" @click="toggleSidebar">
          <PanelLeft :size="20" />
        </button>
      </div>

      <div class="content-wrapper">
        <slot />
      </div>
    </main>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onBeforeUnmount, watch } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useAuthStore } from '../stores/auth'
import {
  Calendar,
  Users,
  GraduationCap,
  BookOpen,
  DoorClosed,
  ClipboardList,
  Settings,
  LogOut,
  PanelLeft,
  PanelLeftClose,
  ChevronUp,
  ChevronDown,
  Database
} from 'lucide-vue-next'

const router = useRouter()
const route = useRoute()
const authStore = useAuthStore()

const sidebarOpen = ref(true)
const userMenuOpen = ref(false)
const expandedGroups = ref({})

const user = computed(() => authStore.user)

const userInitials = computed(() => {
  if (!user.value?.name) return 'U'
  const parts = user.value.name.trim().split(' ')
  if (parts.length >= 2) {
    return (parts[0][0] + parts[1][0]).toUpperCase()
  }
  return parts[0][0].toUpperCase()
})

const userRoleLabel = computed(() => {
  const role = user.value?.role
  if (role === 'teacher') return 'Преподаватель'
  if (role === 'dispatcher') return 'Диспетчер'
  return 'Пользователь'
})

const navItems = computed(() => [
  { to: '/schedule', label: 'Расписание', icon: Calendar },
  { to: '/constructor', label: 'Конструктор', icon: ClipboardList },
  { to: '/teachers', label: 'Преподаватели', icon: Users },
  { to: '/groups', label: 'Группы', icon: GraduationCap },
  { to: '/lessons', label: 'Пары', icon: BookOpen },
  { to: '/rooms', label: 'Аудитории', icon: DoorClosed },
  {
    label: 'Данные',
    icon: Database,
    children: [
      { to: '/data?tab=transfer', label: 'Резервная копия' },
      { to: '/data?tab=import', label: 'Нагрузка Excel' },
      { to: '/data?tab=facts', label: 'Проведённые пары' },
      { to: '/data?tab=audit', label: 'Аудит' },
      { to: '/data?tab=hours', label: 'Учёт часов' },
      { to: '/data?tab=substitutions', label: 'Замены' },
      { to: '/data?tab=occupancy', label: 'Занятость' },
      { to: '/data?tab=unavailable', label: 'Недоступность' },
      { to: '/data?tab=notifications', label: 'Сообщения' },
      { to: '/data?tab=history', label: 'История' },
    ]
  },
  { to: '/settings', label: 'Настройки', icon: Settings },
])

function isGroupActive(group) {
  if (!group.children) return false
  return group.children.some(child => isChildActive(child))
}

function isChildActive(child) {
  if (child.to.includes('?')) {
    const [path, query] = child.to.split('?')
    if (route.path !== path) return false
    const params = new URLSearchParams(query)
    const tab = params.get('tab')
    return tab && route.query.tab === tab
  }
  return route.path === child.to
}

function toggleGroup(label) {
  expandedGroups.value[label] = !expandedGroups.value[label]
}

function toggleSidebar() {
  sidebarOpen.value = !sidebarOpen.value
  localStorage.setItem('sidebarOpen', sidebarOpen.value ? '1' : '0')
}

function toggleUserMenu() {
  userMenuOpen.value = !userMenuOpen.value
}

function goToSettings() {
  userMenuOpen.value = false
  router.push('/settings')
}

function logout() {
  userMenuOpen.value = false
  authStore.logout()
  router.push('/login')
}

function handleClickOutside(e) {
  if (userMenuOpen.value && !e.target.closest('.user-menu')) {
    userMenuOpen.value = false
  }
}

onMounted(() => {
  const saved = localStorage.getItem('sidebarOpen')
  if (saved !== null) {
    sidebarOpen.value = saved === '1'
  }

  // Auto-expand active group
  navItems.value.forEach(item => {
    if (item.children && isGroupActive(item)) {
      expandedGroups.value[item.label] = true
    }
  })

  document.addEventListener('click', handleClickOutside)
})

watch(() => route.path, () => {
  // Auto-expand active group when route changes
  navItems.value.forEach(item => {
    if (item.children && isGroupActive(item)) {
      expandedGroups.value[item.label] = true
    }
  })
})

onBeforeUnmount(() => {
  document.removeEventListener('click', handleClickOutside)
})
</script>

<style scoped>
.admin-layout {
  display: flex;
  height: 100vh;
  background: var(--bg-primary);
  overflow: hidden;
}

.sidebar {
  width: 260px;
  background: var(--bg-secondary);
  border-right: 1px solid var(--border-primary);
  display: flex;
  flex-direction: column;
  transition: width 0.2s ease;
  flex-shrink: 0;
  position: fixed;
  left: 0;
  top: 0;
  bottom: 0;
  z-index: 100;
}

.sidebar.collapsed {
  width: 72px;
}

.sidebar-header {
  height: 64px;
  padding: 16px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  border-bottom: 1px solid var(--border-primary);
  flex-shrink: 0;
}

.sidebar-logo {
  display: flex;
  align-items: center;
  gap: 12px;
  color: var(--text-primary);
  font-weight: 600;
  font-size: 18px;
  min-width: 0;
}

.logo-text {
  white-space: nowrap;
  overflow: hidden;
}

.collapse-btn, .expand-btn {
  background: none;
  border: none;
  color: var(--text-secondary);
  cursor: pointer;
  padding: 6px;
  border-radius: 6px;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all 0.2s;
}

.collapse-btn:hover, .expand-btn:hover {
  background: var(--bg-tertiary);
  color: var(--text-primary);
}

.sidebar-nav {
  flex: 1;
  padding: 16px 12px;
  overflow-y: auto;
  overflow-x: hidden;
}

.nav-group {
  margin-bottom: 4px;
}

.nav-item {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 12px;
  border-radius: 8px;
  color: var(--text-secondary);
  text-decoration: none;
  transition: all 0.15s;
  margin-bottom: 4px;
  position: relative;
  white-space: nowrap;
  min-width: 0;
  border: none;
  background: none;
  width: 100%;
  cursor: pointer;
  font-size: 14px;
}

.nav-item:hover {
  background: var(--bg-tertiary);
  color: var(--text-primary);
}

.nav-item.active {
  background: var(--bg-tertiary);
  color: #F97316;
  font-weight: 500;
}

.nav-parent {
  font-weight: 500;
}

.nav-parent.active {
  color: var(--text-primary);
  background: var(--bg-tertiary);
}

.nav-chevron {
  margin-left: auto;
  transition: transform 0.2s;
  flex-shrink: 0;
}

.nav-chevron.rotated {
  transform: rotate(180deg);
}

.nav-children {
  padding-left: 32px;
  overflow: hidden;
}

.nav-child {
  padding: 8px 12px;
  font-size: 13px;
}

.submenu-enter-active, .submenu-leave-active {
  transition: all 0.2s ease;
}

.submenu-enter-from, .submenu-leave-to {
  opacity: 0;
  max-height: 0;
}

.submenu-enter-to, .submenu-leave-from {
  opacity: 1;
  max-height: 500px;
}

.nav-label {
  overflow: hidden;
  text-overflow: ellipsis;
}

.nav-badge {
  background: var(--bg-accent);
  color: var(--accent-primary);
  padding: 2px 8px;
  border-radius: 12px;
  font-size: 12px;
  font-weight: 600;
}

.sidebar-footer {
  border-top: 1px solid var(--border-primary);
  padding: 12px;
  flex-shrink: 0;
}

.user-menu {
  position: relative;
}

.user-button {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px;
  background: none;
  border: none;
  border-radius: 8px;
  cursor: pointer;
  transition: background 0.2s;
  color: var(--text-primary);
  min-width: 0;
}

.user-button:hover {
  background: var(--bg-tertiary);
}

.user-avatar {
  width: 36px;
  height: 36px;
  border-radius: 50%;
  background: var(--accent-primary);
  color: white;
  display: flex;
  align-items: center;
  justify-content: center;
  font-weight: 600;
  font-size: 14px;
  flex-shrink: 0;
}

.user-info {
  flex: 1;
  text-align: left;
  min-width: 0;
  overflow: hidden;
}

.user-name {
  font-weight: 500;
  font-size: 14px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.user-role {
  font-size: 12px;
  color: var(--text-tertiary);
}

.chevron {
  transition: transform 0.2s;
  flex-shrink: 0;
}

.chevron.rotated {
  transform: rotate(180deg);
}

.user-dropdown {
  position: absolute;
  bottom: 100%;
  left: 12px;
  right: 12px;
  margin-bottom: 8px;
  background: var(--bg-secondary);
  border: 1px solid var(--border-primary);
  border-radius: 8px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
  overflow: hidden;
  z-index: 100;
}

.dropdown-item {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 12px;
  background: none;
  border: none;
  color: var(--text-primary);
  cursor: pointer;
  transition: background 0.15s;
  text-align: left;
}

.dropdown-item:hover {
  background: var(--bg-tertiary);
}

.dropdown-enter-active, .dropdown-leave-active {
  transition: all 0.2s ease;
}

.dropdown-enter-from, .dropdown-leave-to {
  opacity: 0;
  transform: translateY(8px);
}

.main-content {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-width: 0;
  overflow: hidden;
  margin-left: 260px;
  transition: margin-left 0.2s ease;
}

.sidebar.collapsed ~ .main-content {
  margin-left: 72px;
}

.main-header {
  height: 64px;
  border-bottom: 1px solid var(--border-primary);
  display: flex;
  align-items: center;
  padding: 0 24px;
  flex-shrink: 0;
}

.content-wrapper {
  flex: 1;
  overflow-y: auto;
  padding: 24px;
}

@media (max-width: 768px) {
  .sidebar {
    transform: translateX(0);
  }

  .sidebar.collapsed {
    transform: translateX(-100%);
    width: 260px;
  }

  .main-content {
    margin-left: 0 !important;
  }
}
</style>
