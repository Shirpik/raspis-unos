import { ref } from 'vue';

const API_URL = import.meta.env.VITE_API_URL || 'http://localhost:8080';

export function usePushNotifications() {
  const isSupported = ref('Notification' in window && 'serviceWorker' in navigator && 'PushManager' in window);
  const permission = ref(Notification.permission);
  const isSubscribed = ref(false);

  // Request notification permission
  const requestPermission = async () => {
    if (!isSupported.value) {
      return false;
    }

    try {
      const result = await Notification.requestPermission();
      permission.value = result;
      return result === 'granted';
    } catch (error) {
      console.error('Permission request error:', error);
      return false;
    }
  };

  // Subscribe to push notifications
  const subscribe = async (course = null, group = null) => {
    if (!isSupported.value || permission.value !== 'granted') {
      return false;
    }

    try {
      const registration = await navigator.serviceWorker.ready;

      // Get public VAPID key from server
      const response = await fetch(`${API_URL}/api/push/vapid-key`);
      const { publicKey } = await response.json();

      // Subscribe to push
      const subscription = await registration.pushManager.subscribe({
        userVisibleOnly: true,
        applicationServerKey: urlBase64ToUint8Array(publicKey)
      });

      // Send subscription to server
      const saveResponse = await fetch(`${API_URL}/api/push/subscribe`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({
          subscription,
          course,
          group
        })
      });

      if (saveResponse.ok) {
        isSubscribed.value = true;
        localStorage.setItem('push_subscribed', 'true');
        if (course) localStorage.setItem('user_course', course.toString());
        if (group) localStorage.setItem('user_group', group.toString());
        return true;
      }

      return false;
    } catch (error) {
      console.error('Subscription error:', error);
      return false;
    }
  };

  // Unsubscribe from push notifications
  const unsubscribe = async () => {
    try {
      const registration = await navigator.serviceWorker.ready;
      const subscription = await registration.pushManager.getSubscription();

      if (subscription) {
        const endpoint = subscription.endpoint;

        // Unsubscribe from push
        await subscription.unsubscribe();

        // Remove from server
        await fetch(`${API_URL}/api/push/unsubscribe`, {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json'
          },
          body: JSON.stringify({ endpoint })
        });

        isSubscribed.value = false;
        localStorage.removeItem('push_subscribed');
        return true;
      }

      return false;
    } catch (error) {
      console.error('Unsubscribe error:', error);
      return false;
    }
  };

  // Check if already subscribed
  const checkSubscription = async () => {
    if (!isSupported.value) {
      return false;
    }

    try {
      const registration = await navigator.serviceWorker.ready;
      const subscription = await registration.pushManager.getSubscription();
      isSubscribed.value = !!subscription;
      return !!subscription;
    } catch (error) {
      console.error('Check subscription error:', error);
      return false;
    }
  };

  // Helper function to convert VAPID key
  function urlBase64ToUint8Array(base64String) {
    const padding = '='.repeat((4 - base64String.length % 4) % 4);
    const base64 = (base64String + padding)
      .replace(/\-/g, '+')
      .replace(/_/g, '/');

    const rawData = window.atob(base64);
    const outputArray = new Uint8Array(rawData.length);

    for (let i = 0; i < rawData.length; ++i) {
      outputArray[i] = rawData.charCodeAt(i);
    }
    return outputArray;
  }

  return {
    isSupported,
    permission,
    isSubscribed,
    requestPermission,
    subscribe,
    unsubscribe,
    checkSubscription
  };
}
