import { ref, onMounted } from 'vue'

const INSTALL_PROMPT_KEY = 'pwa_install_prompt_count'
const MAX_PROMPT_COUNT = 3

export function usePWA() {
  const deferredPrompt = ref(null)
  const showInstallPrompt = ref(false)
  const isInstalled = ref(false)
  const isIOS = ref(false)
  const isAndroid = ref(false)

  // Проверяем количество показов промпта
  function shouldShowInstallPrompt() {
    const count = parseInt(localStorage.getItem(INSTALL_PROMPT_KEY) || '0')
    return count < MAX_PROMPT_COUNT
  }

  // Увеличиваем счетчик показов
  function incrementPromptCount() {
    const count = parseInt(localStorage.getItem(INSTALL_PROMPT_KEY) || '0')
    localStorage.setItem(INSTALL_PROMPT_KEY, (count + 1).toString())
  }

  // Определяем платформу
  function detectPlatform() {
    const userAgent = window.navigator.userAgent.toLowerCase()
    const isIOSDevice = /iphone|ipad|ipod/.test(userAgent)
    const isInStandaloneMode = ('standalone' in window.navigator) && (window.navigator.standalone)
    
    isIOS.value = isIOSDevice
    isAndroid.value = /android/.test(userAgent)
    isInstalled.value = isInStandaloneMode || window.matchMedia('(display-mode: standalone)').matches
  }

  // Показываем промпт установки
  function showInstall() {
    if (!shouldShowInstallPrompt()) {
      return
    }

    if (isInstalled.value) {
      return
    }

    // Для iOS показываем инструкцию
    if (isIOS.value) {
      showInstallPrompt.value = true
      incrementPromptCount()
      return
    }

    // Для Android используем встроенный промпт
    if (deferredPrompt.value) {
      showInstallPrompt.value = true
      incrementPromptCount()
    }
  }

  // Устанавливаем PWA (для Android)
  async function install() {
    if (!deferredPrompt.value) {
      return false
    }

    deferredPrompt.value.prompt()
    const { outcome } = await deferredPrompt.value.userChoice
    
    if (outcome === 'accepted') {
      deferredPrompt.value = null
      showInstallPrompt.value = false
      return true
    }
    
    return false
  }

  // Закрываем промпт
  function dismissPrompt() {
    showInstallPrompt.value = false
    incrementPromptCount()
  }

  // Получаем инструкцию для текущей платформы
  function getInstallInstructions() {
    if (isIOS.value) {
      return {
        platform: 'iOS',
        steps: [
          'Нажмите кнопку "Поделиться" (квадрат со стрелкой вверх)',
          'Прокрутите вниз и выберите "На экран Домой"',
          'Нажмите "Добавить" в правом верхнем углу'
        ]
      }
    }

    if (isAndroid.value) {
      return {
        platform: 'Android',
        steps: [
          'Нажмите на три точки в правом верхнем углу браузера',
          'Выберите "Установить приложение" или "Добавить на главный экран"',
          'Подтвердите установку'
        ]
      }
    }

    return {
      platform: 'Desktop',
      steps: [
        'Нажмите на иконку установки в адресной строке',
        'Подтвердите установку приложения'
      ]
    }
  }

  onMounted(() => {
    detectPlatform()

    // Слушаем событие beforeinstallprompt
    window.addEventListener('beforeinstallprompt', (e) => {
      e.preventDefault()
      deferredPrompt.value = e
      
      // Показываем промпт при первом заходе или каждый 3-й раз
      const visitCount = parseInt(localStorage.getItem('visit_count') || '0') + 1
      localStorage.setItem('visit_count', visitCount.toString())
      
      if (visitCount === 1 || visitCount % 3 === 0) {
        setTimeout(() => showInstall(), 2000)
      }
    })

    // Отслеживаем успешную установку
    window.addEventListener('appinstalled', () => {
      isInstalled.value = true
      deferredPrompt.value = null
      showInstallPrompt.value = false
      console.log('PWA installed successfully')
    })
  })

  return {
    deferredPrompt,
    showInstallPrompt,
    isInstalled,
    isIOS,
    isAndroid,
    showInstall,
    install,
    dismissPrompt,
    getInstallInstructions
  }
}
