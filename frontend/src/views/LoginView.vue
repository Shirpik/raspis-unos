<template>
  <div class="login-page">
    <form class="card login-card" @submit.prevent="submit">
      <RouterLink to="/" class="back-link">
        <ArrowLeft :size="16" />
        <span>На главную</span>
      </RouterLink>
      <div class="login-icon">
        <Lock :size="36" stroke-width="1.5" />
      </div>
      <h1>Вход диспетчера</h1>
      <p>Введите учётные данные для управления расписанием</p>

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
      <div v-if="error" class="login-error" role="alert">
        <AlertCircle :size="16" />
        <span>{{ error }}</span>
      </div>
      <button class="btn btn-primary btn-lg" type="submit" :disabled="loading">
        <span v-if="loading" class="spinner spinner-sm" />
        <span>{{ loading ? 'Проверка…' : 'Войти' }}</span>
      </button>
    </form>
  </div>
</template>

<script setup>
import { reactive, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useAuthStore } from '../stores/auth.js'
import { ArrowLeft, Lock, AlertCircle } from 'lucide-vue-next'

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
.login-page {
  min-height: 100vh;
  display: grid;
  place-items: center;
  padding: 24px;
  background:
    radial-gradient(circle at 50% 50%, rgba(251, 146, 60, 0.03) 0%, transparent 50%),
    var(--bg-primary);
}
.login-card {
  width: min(100%, 440px);
  padding: 44px;
  display: flex;
  flex-direction: column;
  gap: 20px;
  box-shadow: 0 8px 40px rgba(0, 0, 0, 0.5);
}
.login-card h1 {
  margin: 0;
  text-align: center;
  font-size: 26px;
  font-weight: 600;
  letter-spacing: -0.02em;
}
.login-card p {
  margin: -8px 0 8px;
  color: var(--text-muted);
  text-align: center;
  font-size: 14px;
  line-height: 1.5;
}
.login-icon {
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--accent);
  margin: 0 auto;
}
.back-link {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  color: var(--text-muted);
  text-decoration: none;
  font-size: 14px;
  font-weight: 500;
  align-self: flex-start;
  transition: all var(--transition);
  padding: 4px 0;
}
.back-link:hover {
  color: var(--text-primary);
  gap: 8px;
}
.login-error {
  padding: 12px 16px;
  border: 1px solid rgba(239, 68, 68, 0.3);
  border-radius: var(--radius);
  color: var(--error);
  background: rgba(239, 68, 68, 0.08);
  font-size: 14px;
  display: flex;
  align-items: center;
  gap: 10px;
  font-weight: 500;
}
@media (max-width: 480px) {
  .login-card { padding: 36px 28px; }
  .login-card h1 { font-size: 24px; }
}
</style>
