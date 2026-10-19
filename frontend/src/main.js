import { createApp } from 'vue'
import { createPinia } from 'pinia'
import router from './router/index.js'
import App from './App.vue'
import './tailwind.css'
import './style.css'

// A tab left open during a deployment can reference deleted hashed chunks.
// Recover once; the guard prevents a reload loop during a network failure.
window.addEventListener('vite:preloadError', event => {
  const key = 'raspis-preload-recovery'
  const last = Number(sessionStorage.getItem(key) || 0)
  if (Date.now() - last < 60000) return
  event.preventDefault()
  sessionStorage.setItem(key, String(Date.now()))
  window.location.reload()
})

const app = createApp(App)
app.use(createPinia())
app.use(router)
app.mount('#app')
