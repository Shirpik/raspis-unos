// Утилиты для работы с Push уведомлениями

const API_BASE = '/api'

/**
 * Преобразует base64url в Uint8Array
 */
function urlBase64ToUint8Array(base64String) {
  const padding = '='.repeat((4 - base64String.length % 4) % 4)
  const base64 = (base64String + padding)
    .replace(/-/g, '+')
    .replace(/_/g, '/')

  const rawData = window.atob(base64)
  const outputArray = new Uint8Array(rawData.length)

  for (let i = 0; i < rawData.length; ++i) {
    outputArray[i] = rawData.charCodeAt(i)
  }
  return outputArray
}

/**
 * Проверяет, поддерживает ли браузер Push API
 */
export function isPushSupported() {
  return 'serviceWorker' in navigator && 
         'PushManager' in window && 
         'Notification' in window
}

/**
 * Запрашивает разрешение на уведомления
 */
export async function requestNotificationPermission() {
  if (!isPushSupported()) {
    throw new Error('Push notifications are not supported')
  }

  const permission = await Notification.requestPermission()
  return permission === 'granted'
}

/**
 * Получает текущий статус разрешения
 */
export function getNotificationPermission() {
  if (!isPushSupported()) return 'unsupported'
  return Notification.permission
}

/**
 * Регистрирует service worker
 */
export async function registerServiceWorker() {
  if (!('serviceWorker' in navigator)) {
    throw new Error('Service Workers are not supported')
  }

  try {
    const registration = await navigator.serviceWorker.register('/sw.js', {
      scope: '/'
    })
    console.log('Service Worker registered:', registration)
    return registration
  } catch (error) {
    console.error('Service Worker registration failed:', error)
    throw error
  }
}

/**
 * Получает VAPID публичный ключ с сервера
 */
async function getVapidPublicKey() {
  const response = await fetch(`${API_BASE}/push/vapid-public-key`)
  if (!response.ok) {
    throw new Error('Failed to get VAPID public key')
  }
  const data = await response.json()
  return data.publicKey
}

/**
 * Подписывается на push уведомления
 */
export async function subscribeToPush(course = null, groupId = null) {
  if (!isPushSupported()) {
    throw new Error('Push notifications are not supported')
  }

  // Проверяем разрешение
  const permission = await requestNotificationPermission()
  if (!permission) {
    throw new Error('Notification permission denied')
  }

  // Получаем или регистрируем service worker
  let registration = await navigator.serviceWorker.getRegistration('/')
  if (!registration) {
    registration = await registerServiceWorker()
  }

  // Получаем VAPID ключ
  const vapidPublicKey = await getVapidPublicKey()
  const applicationServerKey = urlBase64ToUint8Array(vapidPublicKey)

  // Подписываемся на push
  const subscription = await registration.pushManager.subscribe({
    userVisibleOnly: true,
    applicationServerKey
  })

  // Отправляем подписку на сервер
  const subscriptionData = {
    endpoint: subscription.endpoint,
    keys: {
      p256dh: btoa(String.fromCharCode(...new Uint8Array(subscription.getKey('p256dh')))),
      auth: btoa(String.fromCharCode(...new Uint8Array(subscription.getKey('auth'))))
    },
    course,
    group_id: groupId
  }

  const response = await fetch(`${API_BASE}/push/subscribe`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify(subscriptionData)
  })

  if (!response.ok) {
    throw new Error('Failed to save subscription on server')
  }

  return await response.json()
}

/**
 * Отписывается от push уведомлений
 */
export async function unsubscribeFromPush() {
  if (!isPushSupported()) {
    return
  }

  const registration = await navigator.serviceWorker.getRegistration('/')
  if (!registration) {
    return
  }

  const subscription = await registration.pushManager.getSubscription()
  if (!subscription) {
    return
  }

  // Отписываемся локально
  await subscription.unsubscribe()

  // Удаляем с сервера
  await fetch(`${API_BASE}/push/unsubscribe`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      endpoint: subscription.endpoint
    })
  })
}

/**
 * Проверяет, подписан ли пользователь
 */
export async function isSubscribed() {
  if (!isPushSupported()) {
    return false
  }

  const registration = await navigator.serviceWorker.getRegistration('/')
  if (!registration) {
    return false
  }

  const subscription = await registration.pushManager.getSubscription()
  return subscription !== null
}

/**
 * Обновляет подписку с новыми параметрами курса/группы
 */
export async function updateSubscription(course = null, groupId = null) {
  // Переподписываемся с новыми параметрами
  await subscribeToPush(course, groupId)
}
