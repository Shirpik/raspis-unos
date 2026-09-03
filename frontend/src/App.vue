<template>
  <div class="app-root">
    <Navbar v-if="!route.meta.hideNav" />
    <main class="main-content">
      <RouterView />
    </main>
    <Toast />
  </div>
</template>

<script setup>
import { onMounted, onBeforeUnmount } from 'vue'
import { useRoute } from 'vue-router'
import { useRouter } from 'vue-router'
import { useAuthStore } from './stores/auth.js'
import Navbar from './components/Navbar.vue'
import Toast from './components/Toast.vue'

const route = useRoute()
const router = useRouter()
const auth = useAuthStore()

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
