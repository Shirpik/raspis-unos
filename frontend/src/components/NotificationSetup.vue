<template>
  <div v-if="show" class="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/60 backdrop-blur-sm">
    <div class="w-full max-w-lg bg-[#0F131C] border border-[#1E2636] rounded-3xl shadow-2xl overflow-hidden">
      <div class="p-6">
        <!-- Заголовок -->
        <div class="text-center mb-6">
          <div class="w-20 h-20 mx-auto mb-4 bg-gradient-to-br from-orange-400 to-orange-600 rounded-full flex items-center justify-center">
            <svg class="w-10 h-10 text-white" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M15 17h5l-1.405-1.405A2.032 2.032 0 0118 14.158V11a6.002 6.002 0 00-4-5.659V5a2 2 0 10-4 0v.341C7.67 6.165 6 8.388 6 11v3.159c0 .538-.214 1.055-.595 1.436L4 17h5m6 0v1a3 3 0 11-6 0v-1m6 0H9" />
            </svg>
          </div>
          <h2 class="text-2xl font-bold text-white mb-2">
            Добро пожаловать!
          </h2>
          <p class="text-slate-400 text-sm">
            Настройте уведомления и выберите свою группу
          </p>
        </div>

        <!-- Форма -->
        <div class="space-y-4">
          <!-- Курс -->
          <div>
            <label class="block text-sm font-medium text-slate-300 mb-2">
              Курс
            </label>
            <select
              v-model="selectedCourse"
              class="w-full px-4 py-2.5 bg-[#161D2B] border border-[#1E2636] rounded-xl text-white focus:outline-none focus:ring-2 focus:ring-orange-500/50 transition-all"
            >
              <option :value="null">Не выбран</option>
              <option value="1">1 курс</option>
              <option value="2">2 курс</option>
              <option value="3">3 курс</option>
              <option value="4">4 курс</option>
            </select>
          </div>

          <!-- Группа -->
          <div>
            <label class="block text-sm font-medium text-slate-300 mb-2">
              Группа
            </label>
            <input
              v-model="selectedGroup"
              type="text"
              placeholder="Например: ИС-21"
              class="w-full px-4 py-2.5 bg-[#161D2B] border border-[#1E2636] rounded-xl text-white placeholder-slate-500 focus:outline-none focus:ring-2 focus:ring-orange-500/50 transition-all"
            />
          </div>

          <!-- Уведомления -->
          <div class="p-4 bg-[#161D2B] rounded-xl border border-[#1E2636]">
            <div class="flex items-start gap-3">
              <input
                v-model="enableNotifications"
                type="checkbox"
                id="notifications-checkbox"
                class="mt-0.5 w-5 h-5 rounded border-[#1E2636] bg-[#0F131C] text-orange-500 focus:ring-2 focus:ring-orange-500/50 transition-all"
              />
              <div class="flex-1">
                <label for="notifications-checkbox" class="block text-sm font-medium text-white cursor-pointer">
                  Получать уведомления
                </label>
                <p class="text-xs text-slate-400 mt-1">
                  Мы будем уведомлять вас об изменениях в расписании и важных новостях
                </p>
              </div>
            </div>
          </div>
        </div>

        <!-- Кнопки -->
        <div class="mt-6 flex gap-3">
          <button
            @click="skip"
            class="flex-1 px-4 py-2.5 bg-[#161D2B] text-slate-300 rounded-full hover:bg-[#1E2636] transition-colors text-sm font-medium"
          >
            Пропустить
          </button>
          <button
            @click="save"
            :disabled="loading"
            class="flex-1 px-4 py-2.5 bg-gradient-to-r from-orange-400 to-orange-500 text-white rounded-full hover:from-orange-500 hover:to-orange-600 transition-all text-sm font-medium shadow-lg shadow-orange-500/25 disabled:opacity-50 disabled:cursor-not-allowed"
          >
            {{ loading ? 'Сохранение...' : 'Сохранить' }}
          </button>
        </div>

        <!-- Ошибка -->
        <div v-if="error" class="mt-4 p-3 bg-red-500/10 border border-red-500/20 rounded-xl">
          <p class="text-sm text-red-400">{{ error }}</p>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref } from 'vue'

const props = defineProps({
  show: {
    type: Boolean,
    required: true
  }
})

const emit = defineEmits(['save', 'skip'])

const selectedCourse = ref(null)
const selectedGroup = ref('')
const enableNotifications = ref(true)
const loading = ref(false)
const error = ref(null)

function skip() {
  emit('skip')
}

function save() {
  error.value = null
  emit('save', {
    course: selectedCourse.value,
    group: selectedGroup.value,
    enableNotifications: enableNotifications.value
  })
}
</script>
