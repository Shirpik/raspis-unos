<template>
  <div class="app-root">
    <component :is="layout">
      <RouterView />
    </component>
    <Toast />
    <PwaInstallBanner />
    <NotificationSetup />
    <WelcomeModal
      :show="showWelcome"
      :groups="availableGroups"
      @close="showWelcome = false"
      @complete="onWelcomeComplete"
    />
  </div>
</template>

<script setup>
import { computed, ref, onMounted, onBeforeUnmount } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useAuthStore } from './stores/auth.js'
import { useFirstLaunch } from './composables/useFirstLaunch.js'
import AdminLayout from './layouts/AdminLayout.vue'
import Toast from './components/Toast.vue'
import PwaInstallBanner from './components/PwaInstallBanner.vue'
import NotificationSetup from './components/NotificationSetup.vue'
import WelcomeModal from './components/WelcomeModal.vue'
import { api } from './api/index.js'

const route = useRoute()
const router = useRouter()
const auth = useAuthStore()
const { showWelcome: shouldShowWelcome, completeFirstLaunch } = useFirstLaunch()

const showWelcome = ref(false)
const availableGroups = ref([])

const layout = computed(() => {
  if (route.meta.hideNav) {
    return { template: '<main class="main-content"><slot /></main>' }
  }
  return AdminLayout
})

function onUnauthorized() {
  auth.authenticated = false
  auth.username = ''
  if (!route.meta.public) router.replace({ path: '/login', query: { expired: '1', redirect: route.fullPath } })
}

async function loadGroups() {
  try {
    const result = await api.groups.list()
    if (result.success && result.data) {
      availableGroups.value = result.data.map(g => ({
        year: g.year,
        group_name: g.group_name,
        group_index: g.group_index
      }))
    }
  } catch (err) {
    console.error('Failed to load groups:', err)
  }
}

function onWelcomeComplete(data) {
  console.log('Welcome complete:', data)
  completeFirstLaunch()
  showWelcome.value = false
}

onMounted(async () => {
  // Register service worker
  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('/sw.js')
      .then(registration => {
        console.log('Service Worker registered:', registration)
      })
      .catch(error => {
        console.error('Service Worker registration failed:', error)
      })
  }

  // Load groups for welcome modal
  await loadGroups()

  // Show welcome modal if needed
  setTimeout(() => {
    if (shouldShowWelcome.value) {
      showWelcome.value = true
    }
  }, 1000)

  window.addEventListener('auth:unauthorized', onUnauthorized)
})

onBeforeUnmount(() => window.removeEventListener('auth:unauthorized', onUnauthorized))
</script>

<style scoped>
.app-root { min-height: 100vh; display: flex; flex-direction: column; }
.main-content { flex: 1; display: flex; flex-direction: column; }
</style>
