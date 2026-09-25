<template>
  <div class="app-root">
    <component :is="layout">
      <RouterView />
    </component>
    <Toast />
  </div>
</template>

<script setup>
import { computed, onMounted, onBeforeUnmount } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useAuthStore } from './stores/auth.js'
import AdminLayout from './layouts/AdminLayout.vue'
import Toast from './components/Toast.vue'

const route = useRoute()
const router = useRouter()
const auth = useAuthStore()

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
onMounted(() => window.addEventListener('auth:unauthorized', onUnauthorized))
onBeforeUnmount(() => window.removeEventListener('auth:unauthorized', onUnauthorized))
</script>

<style scoped>
.app-root { min-height: 100vh; display: flex; flex-direction: column; }
.main-content { flex: 1; display: flex; flex-direction: column; }
</style>
