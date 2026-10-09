<template>
  <div class="p-6 bg-[#0F131C] border border-[#1E2636] rounded-2xl">
    <h2 class="text-xl font-bold text-white mb-6">Создать рассылку</h2>

    <form @submit.prevent="handleSubmit" class="space-y-4">
      <!-- Заголовок -->
      <div>
        <label class="block text-sm font-medium text-slate-300 mb-2">
          Заголовок
        </label>
        <input
          v-model="form.title"
          type="text"
          required
          placeholder="Например: Изменение в расписании"
          class="w-full px-4 py-2.5 bg-[#161D2B] border border-[#1E2636] rounded-xl text-white placeholder-slate-500 focus:outline-none focus:ring-2 focus:ring-orange-500/50 transition-all"
        />
      </div>

      <!-- Сообщение -->
      <div>
        <label class="block text-sm font-medium text-slate-300 mb-2">
          Сообщение
        </label>
        <textarea
          v-model="form.message"
          required
          rows="4"
          placeholder="Введите текст сообщения..."
          class="w-full px-4 py-2.5 bg-[#161D2B] border border-[#1E2636] rounded-xl text-white placeholder-slate-500 focus:outline-none focus:ring-2 focus:ring-orange-500/50 transition-all resize-none"
        ></textarea>
      </div>

      <!-- Целевая аудитория -->
      <div class="p-4 bg-[#161D2B] rounded-xl border border-[#1E2636] space-y-3">
        <h3 class="text-sm font-medium text-white">Целевая аудитория</h3>
        
        <div class="flex items-center gap-3">
          <input
            v-model="targetAll"
            type="checkbox"
            id="target-all"
            class="w-5 h-5 rounded border-[#1E2636] bg-[#0F131C] text-orange-500 focus:ring-2 focus:ring-orange-500/50 transition-all"
          />
          <label for="target-all" class="text-sm text-slate-300 cursor-pointer">
            Для всех студентов
          </label>
        </div>

        <div v-if="!targetAll" class="space-y-3 pt-3 border-t border-[#1E2636]">
          <div>
            <label class="block text-xs font-medium text-slate-400 mb-2">
              Курс
            </label>
            <select
              v-model="form.target_course"
              class="w-full px-3 py-2 bg-[#0F131C] border border-[#1E2636] rounded-lg text-white text-sm focus:outline-none focus:ring-2 focus:ring-orange-500/50 transition-all"
            >
              <option :value="null">Все курсы</option>
              <option value="1">1 курс</option>
              <option value="2">2 курс</option>
              <option value="3">3 курс</option>
              <option value="4">4 курс</option>
            </select>
          </div>

          <div>
            <label class="block text-xs font-medium text-slate-400 mb-2">
              Группа
            </label>
            <input
              v-model="form.target_group"
              type="text"
              placeholder="Например: ИС-21"
              class="w-full px-3 py-2 bg-[#0F131C] border border-[#1E2636] rounded-lg text-white placeholder-slate-500 text-sm focus:outline-none focus:ring-2 focus:ring-orange-500/50 transition-all"
            />
          </div>
        </div>
      </div>

      <!-- Кнопки -->
      <div class="flex gap-3 pt-2">
        <button
          type="button"
          @click="reset"
          class="flex-1 px-4 py-2.5 bg-[#161D2B] text-slate-300 rounded-full hover:bg-[#1E2636] transition-colors text-sm font-medium"
        >
          Очистить
        </button>
        <button
          type="submit"
          :disabled="loading"
          class="flex-1 px-4 py-2.5 bg-gradient-to-r from-orange-400 to-orange-500 text-white rounded-full hover:from-orange-500 hover:to-orange-600 transition-all text-sm font-medium shadow-lg shadow-orange-500/25 disabled:opacity-50 disabled:cursor-not-allowed"
        >
          {{ loading ? 'Отправка...' : 'Отправить' }}
        </button>
      </div>

      <!-- Ошибка/Успех -->
      <div v-if="error" class="p-3 bg-red-500/10 border border-red-500/20 rounded-xl">
        <p class="text-sm text-red-400">{{ error }}</p>
      </div>
      <div v-if="success" class="p-3 bg-green-500/10 border border-green-500/20 rounded-xl">
        <p class="text-sm text-green-400">Рассылка успешно отправлена!</p>
      </div>
    </form>
  </div>
</template>

<script setup>
import { ref, watch } from 'vue'
import { createAnnouncement } from '@/api/announcements'

const emit = defineEmits(['created'])

const form = ref({
  title: '',
  message: '',
  target_course: null,
  target_group: ''
})

const targetAll = ref(true)
const loading = ref(false)
const error = ref(null)
const success = ref(false)

watch(targetAll, (value) => {
  if (value) {
    form.value.target_course = null
    form.value.target_group = ''
  }
})

async function handleSubmit() {
  try {
    loading.value = true
    error.value = null
    success.value = false

    const data = {
      title: form.value.title,
      message: form.value.message
    }

    if (!targetAll.value) {
      if (form.value.target_course) {
        data.target_course = parseInt(form.value.target_course)
      }
      if (form.value.target_group) {
        data.target_group = form.value.target_group
      }
    }

    await createAnnouncement(data)
    
    success.value = true
    emit('created')
    
    // Очистить форму через 2 секунды
    setTimeout(() => {
      reset()
      success.value = false
    }, 2000)
  } catch (err) {
    error.value = 'Не удалось отправить рассылку: ' + err.message
    console.error('Failed to create announcement:', err)
  } finally {
    loading.value = false
  }
}

function reset() {
  form.value = {
    title: '',
    message: '',
    target_course: null,
    target_group: ''
  }
  targetAll.value = true
  error.value = null
  success.value = false
}
</script>
