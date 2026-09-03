import { createRouter, createWebHistory } from 'vue-router'
import HomeView from '../views/HomeView.vue'
import StudentView from '../views/StudentView.vue'
import ScheduleView from '../views/ScheduleView.vue'
import TeachersView from '../views/TeachersView.vue'
import GroupsView from '../views/GroupsView.vue'
import LessonsView from '../views/LessonsView.vue'
import SettingsView from '../views/SettingsView.vue'
import ConstructorView from '../views/ConstructorView.vue'
import RoomsView from '../views/RoomsView.vue'
import DataToolsView from '../views/DataToolsView.vue'
import LoginView from '../views/LoginView.vue'
import { useAuthStore } from '../stores/auth.js'

const routes = [
  { path: '/', component: HomeView, meta: { title: 'Выбор режима', hideNav: true, public: true } },
  { path: '/student', component: StudentView, meta: { title: 'Расписание', hideNav: true, public: true } },
  { path: '/login', component: LoginView, meta: { title: 'Вход диспетчера', hideNav: true, public: true } },
  { path: '/schedule', component: ScheduleView, meta: { title: 'Расписание' } },
  { path: '/constructor', component: ConstructorView, meta: { title: 'Конструктор' } },
  { path: '/teachers', component: TeachersView, meta: { title: 'Преподаватели' } },
  { path: '/groups', component: GroupsView, meta: { title: 'Группы' } },
  { path: '/lessons', component: LessonsView, meta: { title: 'Пары' } },
  { path: '/rooms', component: RoomsView, meta: { title: 'Аудитории' } },
  { path: '/data-tools', component: DataToolsView, meta: { title: 'Данные и контроль' } },
  { path: '/settings', component: SettingsView, meta: { title: 'Настройки' } },
]

const router = createRouter({ history: createWebHistory(), routes })
router.beforeEach(async (to) => {
  const auth = useAuthStore()
  if (!auth.initialized) await auth.check()
  if (!to.meta.public && !auth.authenticated) {
    return { path: '/login', query: { redirect: to.fullPath } }
  }
  if (to.path === '/login' && auth.authenticated) return '/schedule'
})
router.afterEach((to) => {
  document.title = to.meta.title ? `${to.meta.title} — Расписание` : 'Расписание'
})
export default router
