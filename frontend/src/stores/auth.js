import { defineStore } from 'pinia'
import { ref } from 'vue'
import { api } from '../api/index.js'

export const useAuthStore = defineStore('auth', () => {
  const authenticated = ref(false)
  const username = ref('')
  const initialized = ref(false)

  async function check() {
    const result = await api.auth.status()
    authenticated.value = Boolean(result.ok && result.data?.authenticated)
    username.value = authenticated.value ? (result.data.username || '') : ''
    initialized.value = true
    return authenticated.value
  }

  async function login(loginName, password) {
    const result = await api.auth.login(loginName, password)
    if (result.ok) {
      authenticated.value = true
      username.value = result.data.username || loginName
      initialized.value = true
    }
    return result
  }

  async function logout() {
    const result = await api.auth.logout()
    authenticated.value = false
    username.value = ''
    initialized.value = true
    return result
  }

  async function changeCredentials(payload) {
    const result = await api.auth.changeCredentials(payload)
    if (result.ok) username.value = result.data.username || payload.new_username
    return result
  }

  return { authenticated, username, initialized, check, login, logout, changeCredentials }
})
