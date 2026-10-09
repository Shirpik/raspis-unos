import { ref, computed } from 'vue'
import {
  isPushSupported,
  requestNotificationPermission,
  getNotificationPermission,
  subscribeToPush,
  unsubscribeFromPush,
  isSubscribed as checkSubscription,
  updateSubscription as updatePushSubscription
} from '@/utils/pushNotifications'

export function useNotifications() {
  const isSupported = ref(isPushSupported())
  const permission = ref(getNotificationPermission())
  const isSubscribed = ref(false)
  const isLoading = ref(false)
  const error = ref(null)

  const canSubscribe = computed(() => {
    return isSupported.value && permission.value !== 'denied'
  })

  const needsPermission = computed(() => {
    return isSupported.value && permission.value === 'default'
  })

  // Проверяем статус подписки
  async function checkStatus() {
    if (!isSupported.value) return
    
    try {
      isSubscribed.value = await checkSubscription()
      permission.value = getNotificationPermission()
    } catch (err) {
      console.error('Failed to check subscription status:', err)
    }
  }

  // Запрашиваем разрешение
  async function requestPermission() {
    if (!isSupported.value) {
      error.value = 'Ваш браузер не поддерживает уведомления'
      return false
    }

    try {
      isLoading.value = true
      error.value = null
      const granted = await requestNotificationPermission()
      permission.value = getNotificationPermission()
      return granted
    } catch (err) {
      error.value = 'Не удалось запросить разрешение на уведомления'
      console.error('Failed to request permission:', err)
      return false
    } finally {
      isLoading.value = false
    }
  }

  // Подписываемся на уведомления
  async function subscribe(course = null, groupId = null) {
    if (!canSubscribe.value) {
      error.value = 'Невозможно подписаться на уведомления'
      return false
    }

    try {
      isLoading.value = true
      error.value = null
      await subscribeToPush(course, groupId)
      isSubscribed.value = true
      return true
    } catch (err) {
      error.value = 'Не удалось подписаться на уведомления: ' + err.message
      console.error('Failed to subscribe:', err)
      return false
    } finally {
      isLoading.value = false
    }
  }

  // Отписываемся от уведомлений
  async function unsubscribe() {
    try {
      isLoading.value = true
      error.value = null
      await unsubscribeFromPush()
      isSubscribed.value = false
      return true
    } catch (err) {
      error.value = 'Не удалось отписаться от уведомлений'
      console.error('Failed to unsubscribe:', err)
      return false
    } finally {
      isLoading.value = false
    }
  }

  // Обновляем подписку с новыми параметрами
  async function updateSubscription(course = null, groupId = null) {
    try {
      isLoading.value = true
      error.value = null
      await updatePushSubscription(course, groupId)
      return true
    } catch (err) {
      error.value = 'Не удалось обновить подписку'
      console.error('Failed to update subscription:', err)
      return false
    } finally {
      isLoading.value = false
    }
  }

  return {
    isSupported,
    permission,
    isSubscribed,
    isLoading,
    error,
    canSubscribe,
    needsPermission,
    checkStatus,
    requestPermission,
    subscribe,
    unsubscribe,
    updateSubscription
  }
}
