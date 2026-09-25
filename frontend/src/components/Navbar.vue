<template>
  <nav class="navbar">
    <div class="nav-inner">
      <RouterLink to="/schedule" class="nav-logo">
        <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">
          <rect x="3" y="4" width="18" height="18" rx="2" ry="2"/>
          <path d="M16 2v4M8 2v4M3 10h18"/>
        </svg>
        <span>Расписание</span>
      </RouterLink>

      <div class="nav-links wide-only">
        <RouterLink v-for="l in links" :key="l.to" :to="l.to" class="nav-link">
          <component :is="l.icon" :size="18" />
          <span>{{ l.label }}</span>
        </RouterLink>
        <button class="nav-logout" type="button" :title="`Выйти (${auth.username})`" @click="logout">
          <LogOut :size="18" />
          <span>Выйти</span>
        </button>
      </div>

      <button class="btn btn-icon btn-ghost mobile-only" @click="drawerOpen = !drawerOpen" aria-label="Меню">
        <Menu :size="20" />
      </button>
    </div>
  </nav>

  <Transition name="drawer">
    <div v-if="drawerOpen" class="drawer-backdrop" @click="drawerOpen = false">
      <div class="drawer" @click.stop>
        <div class="drawer-header">
          <span class="nav-logo-text">Расписание</span>
          <button class="btn btn-icon btn-ghost" @click="drawerOpen = false">
            <X :size="20" />
          </button>
        </div>
        <div class="drawer-links">
          <RouterLink
            v-for="l in links" :key="l.to" :to="l.to"
            class="drawer-link" @click="drawerOpen = false"
          >
            <component :is="l.icon" :size="20" />
            <span>{{ l.label }}</span>
          </RouterLink>
          <button class="drawer-link drawer-logout" type="button" @click="logout">
            <LogOut :size="20" />
            <span>Выйти ({{ auth.username }})</span>
          </button>
        </div>
      </div>
    </div>
  </Transition>
</template>

<script setup>
import { ref, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useAuthStore } from '../stores/auth.js'
import { CalendarRange, Wrench, Users, BookOpen, DoorOpen, Bell, Database, Settings, LogOut, Menu, X, User } from 'lucide-vue-next'

const drawerOpen = ref(false)
const route = useRoute()
const router = useRouter()
const auth = useAuthStore()
watch(route, () => { drawerOpen.value = false })

const links = [
  { to: '/schedule',    label: 'Расписание',    icon: CalendarRange },
  { to: '/constructor', label: 'Конструктор',   icon: Wrench },
  { to: '/teachers',    label: 'Преподаватели', icon: User },
  { to: '/groups',      label: 'Группы',        icon: Users },
  { to: '/lessons',     label: 'Пары',          icon: BookOpen },
  { to: '/rooms',       label: 'Аудитории',     icon: DoorOpen },
  { to: '/teacher-notifications', label: 'Уведомления', icon: Bell },
  { to: '/data-tools',  label: 'Данные',         icon: Database },
  { to: '/settings',    label: 'Настройки',     icon: Settings },
]

async function logout() {
  drawerOpen.value = false
  await auth.logout()
  await router.replace('/')
}
</script>

<style scoped>
.navbar {
  position: sticky; top: 0; z-index: 100;
  background: rgba(11, 14, 20, 0.85);
  backdrop-filter: blur(20px) saturate(180%);
  border-bottom: 1px solid var(--border);
  height: var(--nav-height);
}
.nav-inner {
  max-width: 1400px; margin: 0 auto;
  height: 100%; padding: 0 24px;
  display: flex; align-items: center; justify-content: space-between;
}
.nav-logo {
  display: flex; align-items: center; gap: 10px;
  font-size: 16px; font-weight: 600; color: var(--text-primary);
  text-decoration: none; letter-spacing: -0.02em;
  transition: all var(--transition);
}
.nav-logo:hover { color: var(--accent); transform: translateY(-1px); }
.nav-logo svg { flex-shrink: 0; }
.nav-logo-text { font-size: 16px; font-weight: 600; letter-spacing: -0.02em; }

.nav-links { display: flex; align-items: center; gap: 4px; }
.nav-link {
  display: flex; align-items: center; gap: 8px;
  padding: 9px 14px; border-radius: var(--radius-sm);
  font-size: 14px; font-weight: 500;
  color: var(--text-secondary);
  transition: all var(--transition);
  text-decoration: none;
  border: 1px solid transparent;
}
.nav-link:hover {
  color: var(--text-primary);
  background: var(--bg-tertiary);
  border-color: var(--border);
}
.nav-link.router-link-active {
  color: var(--accent);
  background: var(--accent-light);
  border-color: rgba(251, 146, 60, 0.2);
}
.nav-logout {
  display: flex; align-items: center; gap: 8px;
  border: 1px solid transparent;
  background: transparent;
  color: var(--text-muted);
  padding: 9px 14px;
  border-radius: var(--radius-sm);
  cursor: pointer;
  font: inherit;
  font-size: 14px;
  font-weight: 500;
  transition: all var(--transition);
}
.nav-logout:hover {
  color: var(--error);
  background: var(--error-light);
  border-color: rgba(239, 68, 68, 0.2);
}

.drawer-backdrop {
  position: fixed; inset: 0; z-index: 200;
  background: rgba(0,0,0,0.75);
  backdrop-filter: blur(8px);
}
.drawer {
  position: absolute; top: 0; right: 0; bottom: 0;
  width: min(320px, 85vw);
  background: var(--bg-secondary);
  border-left: 1px solid var(--border);
  display: flex; flex-direction: column;
  box-shadow: -12px 0 48px rgba(0,0,0,0.6);
}
.drawer-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 20px; border-bottom: 1px solid var(--border);
  background: var(--bg-primary);
}
.drawer-links {
  padding: 16px 12px;
  display: flex; flex-direction: column; gap: 4px;
  overflow-y: auto;
}
.drawer-link {
  display: flex; align-items: center; gap: 12px;
  padding: 14px 16px; border-radius: var(--radius-sm);
  font-size: 15px; font-weight: 500;
  color: var(--text-secondary);
  text-decoration: none;
  transition: all var(--transition);
  border: 1px solid transparent;
}
.drawer-link:hover {
  color: var(--text-primary);
  background: var(--bg-tertiary);
  border-color: var(--border);
  transform: translateX(-2px);
}
.drawer-link.router-link-active {
  color: var(--accent);
  background: var(--accent-light);
  border-color: rgba(251, 146, 60, 0.2);
}
.drawer-logout {
  border: 0; width: 100%;
  background: transparent;
  cursor: pointer;
  text-align: left;
  color: var(--error);
  font-weight: 500;
  margin-top: 8px;
}
.drawer-logout:hover {
  background: var(--error-light);
  border-color: rgba(239, 68, 68, 0.2);
}

.drawer-enter-active, .drawer-leave-active {
  transition: opacity 0.25s ease;
}
.drawer-enter-from, .drawer-leave-to { opacity: 0; }
.drawer-enter-active .drawer, .drawer-leave-active .drawer {
  transition: transform 0.3s cubic-bezier(0.4, 0, 0.2, 1);
}
.drawer-enter-from .drawer { transform: translateX(100%); }
.drawer-leave-to .drawer { transform: translateX(100%); }

.wide-only { display: flex; }
.mobile-only  { display: none; }

@media (max-width: 900px) {
  .wide-only { display: none; }
  .mobile-only  { display: flex; }
  .nav-inner { padding: 0 16px; }
}
</style>
