import { defineStore } from 'pinia'
import { ref } from 'vue'
import { api } from '../api/index.js'

export const useAuthStore = defineStore('auth', () => {
  const authenticated = ref(false)
  const username = ref('')
  const user = ref(null)
  const initialized = ref(false)

  async function check() {
    const result = await api.auth.status()
    authenticated.value = Boolean(result.ok && result.data?.authenticated)
    username.value = authenticated.value ? (result.data.username || '') : ''
    user.value = authenticated.value ? {
      name: result.data.username || '',
      role: result.data.role || 'dispatcher'
    } : null
    initialized.value = true
    return authenticated.value
  }

  async function login(loginName, password) {
    const result = await api.auth.login(loginName, password)
    if (result.ok) {
      authenticated.value = true
      username.value = result.data.username || loginName
      user.value = {
        name: result.data.username || loginName,
        role: result.data.role || 'dispatcher'
      }
      initialized.value = true
    }
    return result
  }

  async function logout() {
    const result = await api.auth.logout()
    authenticated.value = false
    username.value = ''
    user.value = null
    initialized.value = true
    return result
  }

  async function changeCredentials(payload) {
    const result = await api.auth.changeCredentials(payload)
    if (result.ok) username.value = result.data.username || payload.new_username
    return result
  }

  return { authenticated, username, user, initialized, check, login, logout, changeCredentials }
})
