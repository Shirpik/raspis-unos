import { ref, onMounted } from 'vue'

const FIRST_LAUNCH_KEY = 'pwa_first_launch_completed'
const STANDALONE_MODE_KEY = 'pwa_was_standalone'

export function useFirstLaunch() {
  const isFirstLaunch = ref(false)
  const showWelcome = ref(false)

  function checkFirstLaunch() {
    // Проверяем, запущено ли приложение в standalone режиме (как PWA)
    const isStandalone = window.matchMedia('(display-mode: standalone)').matches ||
                        window.navigator.standalone ||
                        document.referrer.includes('android-app://')

    // Проверяем, был ли уже первый запуск
    const wasFirstLaunchCompleted = localStorage.getItem(FIRST_LAUNCH_KEY) === 'true'
    const wasStandaloneBefore = localStorage.getItem(STANDALONE_MODE_KEY) === 'true'

    // Если приложение запущено как PWA и это первый запуск в PWA режиме
    if (isStandalone && !wasStandaloneBefore) {
      isFirstLaunch.value = true
      showWelcome.value = !wasFirstLaunchCompleted
      localStorage.setItem(STANDALONE_MODE_KEY, 'true')
    }

    return {
      isFirstLaunch: isFirstLaunch.value,
      showWelcome: showWelcome.value,
      isStandalone
    }
  }

  function completeFirstLaunch() {
    localStorage.setItem(FIRST_LAUNCH_KEY, 'true')
    isFirstLaunch.value = false
    showWelcome.value = false
  }

  function resetFirstLaunch() {
    localStorage.removeItem(FIRST_LAUNCH_KEY)
    localStorage.removeItem(STANDALONE_MODE_KEY)
    isFirstLaunch.value = false
    showWelcome.value = false
  }

  onMounted(() => {
    checkFirstLaunch()
  })

  return {
    isFirstLaunch,
    showWelcome,
    checkFirstLaunch,
    completeFirstLaunch,
    resetFirstLaunch
  }
}
