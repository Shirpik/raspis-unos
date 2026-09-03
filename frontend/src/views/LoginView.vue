<template>
  <div class="login-page">
    <form class="card login-card" @submit.prevent="submit">
      <RouterLink to="/" class="back-link">← На главную</RouterLink>
      <div class="login-icon">🔐</div>
      <h1>Вход диспетчера</h1>
      <p>Введите учётные данные для управления расписанием.</p>

      <div class="form-group">
        <label class="form-label" for="dispatcher-login">Логин</label>
        <input
          id="dispatcher-login" v-model.trim="form.username" class="form-input"
          autocomplete="username" autofocus required maxlength="64"
        />
      </div>
      <div class="form-group">
        <label class="form-label" for="dispatcher-password">Пароль</label>
        <input
          id="dispatcher-password" v-model="form.password" type="password" class="form-input"
          autocomplete="current-password" required maxlength="128"
        />
      </div>
      <div v-if="error" class="login-error" role="alert">{{ error }}</div>
      <button class="btn btn-primary btn-lg" type="submit" :disabled="loading">
        <span v-if="loading" class="spinner spinner-sm" />
        {{ loading ? 'Проверка…' : 'Войти' }}
      </button>
    </form>
  </div>
</template>

<script setup>
import { reactive, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useAuthStore } from '../stores/auth.js'

const route = useRoute()
const router = useRouter()
const auth = useAuthStore()
const form = reactive({ username: '', password: '' })
const loading = ref(false)
const error = ref(route.query.expired ? 'Сессия завершена. Войдите снова.' : '')

async function submit() {
  error.value = ''
  loading.value = true
  const result = await auth.login(form.username, form.password)
  loading.value = false
  if (!result.ok) {
    error.value = result.data?.message || 'Не удалось войти'
    form.password = ''
    return
  }
  const target = typeof route.query.redirect === 'string' && route.query.redirect.startsWith('/')
    ? route.query.redirect : '/schedule'
  await router.replace(target)
}
</script>

<style scoped>
.login-page { min-height: 100vh; display: grid; place-items: center; padding: 24px; }
.login-card { width: min(100%, 420px); padding: 36px; display: flex; flex-direction: column; gap: 16px; }
.login-card h1 { margin: 0; text-align: center; font-size: 25px; }
.login-card p { margin: -6px 0 8px; color: var(--text-muted); text-align: center; font-size: 14px; }
.login-icon { font-size: 42px; text-align: center; }
.back-link { color: var(--text-muted); text-decoration: none; font-size: 13px; align-self: flex-start; }
.login-error { padding: 10px 12px; border: 1px solid rgba(239,68,68,.4); border-radius: var(--radius-sm); color: var(--error); background: rgba(239,68,68,.08); font-size: 13px; }
@media (max-width: 480px) { .login-card { padding: 26px 20px; } }
</style>
