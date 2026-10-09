import { api } from '../api/index.js'

let cachedVapidKey = null

export async function getVapidPublicKey() {
  if (cachedVapidKey) {
    return cachedVapidKey
  }

  try {
    const response = await fetch('/api/push/vapid-public-key')
    if (response.ok) {
      const data = await response.json()
      cachedVapidKey = data.public_key
      return cachedVapidKey
    }
  } catch (error) {
    console.error('Failed to fetch VAPID key:', error)
  }
  
  return null
}

export async function urlBase64ToUint8Array(base64String) {
  const padding = '='.repeat((4 - base64String.length % 4) % 4)
  const base64 = (base64String + padding)
    .replace(/\-/g, '+')
    .replace(/_/g, '/')

  const rawData = window.atob(base64)
  const outputArray = new Uint8Array(rawData.length)

  for (let i = 0; i < rawData.length; ++i) {
    outputArray[i] = rawData.charCodeAt(i)
  }
  return outputArray
}

export async function registerServiceWorker() {
  if (!('serviceWorker' in navigator)) {
    console.log('Service Worker not supported')
    return null
  }

  try {
    const registration = await navigator.serviceWorker.register('/sw.js', { scope: '/' })
    console.log('Service Worker registered:', registration)
    return registration
  } catch (error) {
    console.error('Service Worker registration failed:', error)
    return null
  }
}

export async function requestNotificationPermission() {
  if (!('Notification' in window)) {
    console.log('Notifications not supported')
    return false
  }

  if (Notification.permission === 'granted') {
    return true
  }

  if (Notification.permission !== 'denied') {
    const permission = await Notification.requestPermission()
    return permission === 'granted'
  }

  return false
}

export async function subscribeToPush(course = null, groupId = null) {
  try {
    const vapidKey = await getVapidPublicKey()
    if (!vapidKey) {
      console.error('VAPID key not available')
      return false
    }

    const registration = await navigator.serviceWorker.ready

    const existingSubscription = await registration.pushManager.getSubscription()
    if (existingSubscription) {
      await existingSubscription.unsubscribe()
    }

    const subscription = await registration.pushManager.subscribe({
      userVisibleOnly: true,
      applicationServerKey: await urlBase64ToUint8Array(vapidKey)
    })

    const subscriptionJson = subscription.toJSON()
    const payload = {
      endpoint: subscriptionJson.endpoint,
      keys: subscriptionJson.keys,
      course: course,
      group_id: groupId
    }

    const result = await api.push.subscribe(payload)
    return result.ok
  } catch (error) {
    console.error('Failed to subscribe to push:', error)
    return false
  }
}

export async function unsubscribeFromPush() {
  try {
    const registration = await navigator.serviceWorker.ready
    const subscription = await registration.pushManager.getSubscription()

    if (subscription) {
      const subscriptionJson = subscription.toJSON()
      await api.push.unsubscribe(subscriptionJson.endpoint)
      await subscription.unsubscribe()
      return true
    }

    return false
  } catch (error) {
    console.error('Failed to unsubscribe from push:', error)
    return false
  }
}

export async function isPushSubscribed() {
  try {
    if (!('serviceWorker' in navigator) || !('PushManager' in window)) {
      return false
    }

    const registration = await navigator.serviceWorker.ready
    const subscription = await registration.pushManager.getSubscription()
    return subscription !== null
  } catch (error) {
    console.error('Failed to check push subscription:', error)
    return false
  }
}

export function isStandalone() {
  return window.matchMedia('(display-mode: standalone)').matches ||
    window.navigator.standalone === true
}

export function canInstallPwa() {
  return !isStandalone() && 'serviceWorker' in navigator
}

let deferredPrompt = null

export function setupPwaInstallPrompt() {
  window.addEventListener('beforeinstallprompt', (e) => {
    e.preventDefault()
    deferredPrompt = e
    console.log('PWA install prompt ready')
  })

  window.addEventListener('appinstalled', () => {
    console.log('PWA installed')
    deferredPrompt = null
  })
}

export async function showPwaInstallPrompt() {
  if (!deferredPrompt) {
    return false
  }

  deferredPrompt.prompt()
  const { outcome } = await deferredPrompt.userChoice
  console.log(`User response to install prompt: ${outcome}`)
  deferredPrompt = null
  return outcome === 'accepted'
}

export function getPwaInstallPrompt() {
  return deferredPrompt
}
