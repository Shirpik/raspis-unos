<template>
  <div v-if="show" class="fixed inset-0 z-50 flex items-end sm:items-center justify-center p-4 bg-black/50 backdrop-blur-sm">
    <div class="w-full max-w-md bg-[#0F131C] border border-[#1E2636] rounded-3xl shadow-2xl overflow-hidden">
      <div class="p-6">
        <div class="flex items-start gap-4">
          <div class="w-16 h-16 bg-gradient-to-br from-orange-400 to-orange-600 rounded-2xl flex items-center justify-center flex-shrink-0">
            <svg class="w-8 h-8 text-white" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 18h.01M8 21h8a2 2 0 002-2V5a2 2 0 00-2-2H8a2 2 0 00-2 2v14a2 2 0 002 2z" />
            </svg>
          </div>
          
          <div class="flex-1 min-w-0">
            <h3 class="text-lg font-semibold text-white mb-1">
              Установите приложение
            </h3>
            <p class="text-sm text-slate-400 leading-relaxed">
              Получайте уведомления об изменениях в расписании и быстрый доступ с главного экрана
            </p>
          </div>
          
          <button 
            @click="$emit('dismiss')"
            class="text-slate-400 hover:text-white transition-colors flex-shrink-0"
            aria-label="Закрыть"
          >
            <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        </div>

        <div class="mt-6 flex gap-3">
          <button
            @click="$emit('dismiss')"
            class="flex-1 px-4 py-2.5 bg-[#161D2B] text-slate-300 rounded-full hover:bg-[#1E2636] transition-colors text-sm font-medium"
          >
            Позже
          </button>
          <button
            @click="$emit('install')"
            class="flex-1 px-4 py-2.5 bg-gradient-to-r from-orange-400 to-orange-500 text-white rounded-full hover:from-orange-500 hover:to-orange-600 transition-all text-sm font-medium shadow-lg shadow-orange-500/25"
          >
            Установить
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
defineProps({
  show: {
    type: Boolean,
    required: true
  }
})

defineEmits(['install', 'dismiss'])
</script>
