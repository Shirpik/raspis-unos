<template>
  <div class="space-y-4">
    <div v-if="loading" class="text-center py-8">
      <div class="inline-block w-8 h-8 border-4 border-slate-600 border-t-orange-500 rounded-full animate-spin"></div>
    </div>

    <div v-else-if="error" class="p-4 bg-red-500/10 border border-red-500/20 rounded-xl">
      <p class="text-sm text-red-400">{{ error }}</p>
    </div>

    <div v-else-if="announcements.length === 0" class="text-center py-12">
      <div class="w-16 h-16 mx-auto mb-4 bg-[#161D2B] rounded-full flex items-center justify-center">
        <svg class="w-8 h-8 text-slate-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4" />
        </svg>
      </div>
      <p class="text-slate-400">Пока нет уведомлений</p>
    </div>

    <div v-else class="space-y-3">
      <div
        v-for="announcement in announcements"
        :key="announcement.id"
        class="p-4 bg-[#0F131C] border border-[#1E2636] rounded-xl hover:border-[#1E2636]/80 transition-colors"
      >
        <div class="flex items-start gap-3">
          <div class="w-10 h-10 bg-gradient-to-br from-orange-400 to-orange-600 rounded-full flex items-center justify-center flex-shrink-0">
            <svg class="w-5 h-5 text-white" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M15 17h5l-1.405-1.405A2.032 2.032 0 0118 14.158V11a6.002 6.002 0 00-4-5.659V5a2 2 0 10-4 0v.341C7.67 6.165 6 8.388 6 11v3.159c0 .538-.214 1.055-.595 1.436L4 17h5m6 0v1a3 3 0 11-6 0v-1m6 0H9" />
            </svg>
          </div>

          <div class="flex-1 min-w-0">
            <div class="flex items-start justify-between gap-2 mb-1">
              <h3 class="text-white font-medium">{{ announcement.title }}</h3>
              <span class="text-xs text-slate-500 whitespace-nowrap">
                {{ formatDate(announcement.created_at) }}
              </span>
            </div>
            
            <p class="text-sm text-slate-400 leading-relaxed">
              {{ announcement.message }}
            </p>

            <div v-if="announcement.target_course || announcement.target_group" class="mt-2 flex gap-2">
              <span v-if="announcement.target_course" class="inline-flex items-center px-2 py-1 bg-[#161D2B] rounded-lg text-xs text-slate-400">
                {{ announcement.target_course }} курс
              </span>
              <span v-if="announcement.target_group" class="inline-flex items-center px-2 py-1 bg-[#161D2B] rounded-lg text-xs text-slate-400">
                {{ announcement.target_group }}
              </span>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, computed } from 'vue'
import { getAnnouncements } from '@/api/announcements'

const props = defineProps({
  course: {
    type: [String, Number],
    default: null
  },
  groupId: {
    type: String,
    default: null
  }
})

const announcements = ref([])
const loading = ref(false)
const error = ref(null)

async function loadAnnouncements() {
  try {
    loading.value = true
    error.value = null
    announcements.value = await getAnnouncements(props.course, props.groupId)
  } catch (err) {
    error.value = 'Не удалось загрузить уведомления'
    console.error('Failed to load announcements:', err)
  } finally {
    loading.value = false
  }
}

function formatDate(dateString) {
  const date = new Date(dateString)
  const now = new Date()
  const diffMs = now - date
  const diffMins = Math.floor(diffMs / 60000)
  const diffHours = Math.floor(diffMs / 3600000)
  const diffDays = Math.floor(diffMs / 86400000)

  if (diffMins < 1) return 'Только что'
  if (diffMins < 60) return `${diffMins} мин назад`
  if (diffHours < 24) return `${diffHours} ч назад`
  if (diffDays < 7) return `${diffDays} д назад`
  
  return date.toLocaleDateString('ru-RU', {
    day: 'numeric',
    month: 'short'
  })
}

onMounted(() => {
  loadAnnouncements()
})

defineExpose({
  refresh: loadAnnouncements
})
</script>
