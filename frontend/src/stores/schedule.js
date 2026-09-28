import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { api } from '../api/index.js'
import { mergeConfirmedSchedule } from '../utils/scheduleLedger.js'

export const useScheduleStore = defineStore('schedule', () => {
  const scheduleData = ref(null)
  const loading = ref(false)
  const error = ref(null)
  const semester = ref(null)
  const generating = ref(false)
  const lastScoped = ref(false)

  // Прогресс недельной генерации
  const progress = ref(null)   // null | { state, total_weeks, current_week, solved_weeks, weeks, message, total_elapsed }
  let _pollTimer = null
  let _pollFailures = 0

  async function fetchSchedule() {
    loading.value = true
    error.value = null
    const [res, raw] = await Promise.all([api.schedule.get(), api.schedule.context()])
    loading.value = false
    if (raw.ok) {
      semester.value = raw.data?.settings || null
      scheduleData.value = mergeConfirmedSchedule(res.ok ? res.data : null, raw.data)
      if (!res.ok && res.status !== 404 && res.status !== 409)
        error.value = res.data?.message || 'Авторасписание недоступно'
    } else if (res.ok) {
      scheduleData.value = res.data
    } else {
      // Do not keep showing an old schedule after the backend reports that
      // the database revision changed.
      scheduleData.value = null
      error.value = res.data?.message || 'Ошибка загрузки расписания'
    }
  }

  async function fetchPublished() {
    loading.value = true
    error.value = null
    const res = await api.schedule.getPublished()
    loading.value = false
    if (res.ok) scheduleData.value = res.data
    else if (res.status === 404) scheduleData.value = null
    else {
      scheduleData.value = null
      error.value = res.data?.message || 'Опубликованное расписание недоступно'
    }
  }

  async function fetchPublishedGroups() {
    loading.value = true
    error.value = null
    const res = await api.schedule.getPublishedGroups()
    loading.value = false
    if (res.ok) return res.data
    else if (res.status === 404) return []
    else {
      error.value = res.data?.message || 'Список групп недоступен'
      return []
    }
  }

  async function fetchPublishedGroup(groupIndex) {
    loading.value = true
    error.value = null
    const res = await api.schedule.getPublishedGroup(groupIndex)
    loading.value = false
    if (res.ok) {
      scheduleData.value = res.data
      return res.data
    } else if (res.status === 404) {
      scheduleData.value = null
      error.value = 'Группа не найдена'
      return null
    } else {
      scheduleData.value = null
      error.value = res.data?.message || 'Ошибка загрузки расписания группы'
      return null
    }
  }

  function _stopPolling() {
    if (_pollTimer) { clearInterval(_pollTimer); _pollTimer = null }
  }

  async function _pollProgress() {
    const res = await api.schedule.progress()
    if (!res.ok) {
      _pollFailures++
      if (res.status === 401 || _pollFailures >= 5) {
        _stopPolling()
        generating.value = false
        error.value = res.data?.message || 'Потеряна связь с генератором расписания'
      }
      return
    }
    _pollFailures = 0
    progress.value = res.data

    const st = res.data?.state
    if (st === 'done' || st === 'failed' || st === 'cancelled' || st === 'idle') {
      _stopPolling()
      _pollFailures = 0
      generating.value = false
      // Обновляем расписание если успешно завершено
      if (st === 'done') await fetchSchedule()
    }
  }

  async function regenerate(opts = {}) {
    generating.value = true
    lastScoped.value = Boolean(opts.scope_from)
    progress.value = null

    const capability = await api.data.semesterReadout()
    if (!capability.ok || !(capability.data?.rules_version >= 3)) {
      generating.value = false
      return { ok: false, message: capability.data?.message || 'Перезапустите сайт с новой сборкой сервера: текущая версия не поддерживает полный контроль календаря практики.' }
    }

    const res = await api.schedule.regenerate(opts)

    if (!res.ok) {
      generating.value = false
      return { ok: false, message: res.data?.message || 'Ошибка генерации' }
    }

    if (res.data?.async) {
      // Async (weekly) — запускаем polling
      _stopPolling()
      _pollFailures = 0
      _pollTimer = setInterval(_pollProgress, 1000)
      return { ok: true, async: true, message: 'Генерация запущена' }
    }

    // Sync (monolithic) — старое поведение
    generating.value = false
    if (res.ok) {
      await fetchSchedule()
      const extra = res.data?.lock_source && res.data.lock_source !== 'none'
        ? ` (закреплено ${res.data.locked_count} слотов из «${res.data.lock_source}»)` : ''
      return { ok: true, message: (res.data?.message || 'Расписание сгенерировано') + extra }
    }
    return { ok: false, message: res.data?.message || 'Ошибка генерации' }
  }

  async function cancelGeneration() {
    const res = await api.schedule.cancel()
    return res.ok
  }

  const groups = computed(() => scheduleData.value?.groups || [])

  return {
    scheduleData, loading, error, generating, semester, lastScoped,
    progress,
    groups,
    fetchSchedule, fetchPublished, fetchPublishedGroups, fetchPublishedGroup, regenerate, cancelGeneration,
  }
})
