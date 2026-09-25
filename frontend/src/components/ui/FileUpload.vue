<template>
  <div class="w-full">
    <label v-if="label" class="form-label">{{ label }}</label>

    <div
      @drop.prevent="handleDrop"
      @dragover.prevent="isDragging = true"
      @dragleave.prevent="isDragging = false"
      :class="cn(
        'relative border-2 border-dashed rounded-xl transition-all duration-200',
        isDragging
          ? 'border-orange-500 bg-orange-500/5'
          : 'border-slate-700 hover:border-slate-600',
        disabled && 'opacity-50 cursor-not-allowed',
        className
      )"
    >
      <input
        ref="fileInput"
        type="file"
        :accept="accept"
        :multiple="multiple"
        @change="handleFileSelect"
        class="sr-only"
        :disabled="disabled"
      />

      <!-- Upload Area -->
      <div
        v-if="!files.length"
        @click="triggerFileInput"
        class="flex flex-col items-center justify-center py-12 px-6 cursor-pointer"
      >
        <div class="mb-4 p-4 rounded-full bg-slate-800/50">
          <Upload :size="24" class="text-orange-400" />
        </div>
        <p class="text-sm font-medium text-slate-200 mb-1">
          {{ dragText || 'Перетащите файл сюда' }}
        </p>
        <p class="text-xs text-slate-400">
          или <span class="text-orange-400">выберите файл</span>
        </p>
        <p v-if="accept" class="text-xs text-slate-500 mt-2">
          {{ accept }}
        </p>
      </div>

      <!-- Files List -->
      <div v-else class="p-4 space-y-2">
        <div
          v-for="(file, index) in files"
          :key="index"
          class="flex items-center gap-3 p-3 bg-slate-800/30 rounded-lg border border-slate-700/50 hover:border-slate-600 transition-colors"
        >
          <div class="p-2 rounded-lg bg-orange-500/10">
            <FileText :size="20" class="text-orange-400" />
          </div>

          <div class="flex-1 min-w-0">
            <p class="text-sm font-medium text-slate-200 truncate">
              {{ file.name }}
            </p>
            <p class="text-xs text-slate-400">
              {{ formatFileSize(file.size) }}
            </p>
          </div>

          <button
            @click="removeFile(index)"
            class="p-1.5 hover:bg-slate-700 rounded-lg transition-colors"
            type="button"
          >
            <X :size="16" class="text-slate-400" />
          </button>
        </div>

        <!-- Add more button -->
        <button
          v-if="multiple"
          @click="triggerFileInput"
          class="w-full py-2 px-4 border border-dashed border-slate-700 rounded-lg text-sm text-slate-400 hover:text-slate-300 hover:border-slate-600 transition-colors"
          type="button"
        >
          <Plus :size="16" class="inline mr-2" />
          Добавить еще
        </button>
      </div>
    </div>

    <!-- Error message -->
    <p v-if="error" class="mt-2 text-sm text-red-400 flex items-center gap-1">
      <AlertCircle :size="14" />
      {{ error }}
    </p>
  </div>
</template>

<script setup>
import { ref, watch } from 'vue'
import { Upload, FileText, X, Plus, AlertCircle } from 'lucide-vue-next'
import { cn } from '../../lib/utils'

const props = defineProps({
  modelValue: {
    type: [File, Array],
    default: null
  },
  accept: {
    type: String,
    default: ''
  },
  multiple: {
    type: Boolean,
    default: false
  },
  label: {
    type: String,
    default: ''
  },
  dragText: {
    type: String,
    default: ''
  },
  disabled: {
    type: Boolean,
    default: false
  },
  maxSize: {
    type: Number,
    default: 10 * 1024 * 1024 // 10MB
  },
  className: {
    type: String,
    default: ''
  }
})

const emit = defineEmits(['update:modelValue', 'error'])

const fileInput = ref(null)
const files = ref([])
const isDragging = ref(false)
const error = ref('')

watch(() => props.modelValue, (value) => {
  if (!value) {
    files.value = []
  } else if (Array.isArray(value)) {
    files.value = value
  } else {
    files.value = [value]
  }
}, { immediate: true })

const triggerFileInput = () => {
  if (!props.disabled) {
    fileInput.value?.click()
  }
}

const validateFile = (file) => {
  if (file.size > props.maxSize) {
    error.value = `Файл слишком большой. Максимальный размер: ${formatFileSize(props.maxSize)}`
    emit('error', error.value)
    return false
  }
  error.value = ''
  return true
}

const handleFileSelect = (event) => {
  const selectedFiles = Array.from(event.target.files || [])
  addFiles(selectedFiles)
}

const handleDrop = (event) => {
  isDragging.value = false
  if (props.disabled) return

  const droppedFiles = Array.from(event.dataTransfer?.files || [])
  addFiles(droppedFiles)
}

const addFiles = (newFiles) => {
  const validFiles = newFiles.filter(validateFile)

  if (props.multiple) {
    files.value = [...files.value, ...validFiles]
    emit('update:modelValue', files.value)
  } else if (validFiles.length > 0) {
    files.value = [validFiles[0]]
    emit('update:modelValue', validFiles[0])
  }
}

const removeFile = (index) => {
  files.value.splice(index, 1)

  if (props.multiple) {
    emit('update:modelValue', files.value)
  } else {
    emit('update:modelValue', null)
  }
}

const formatFileSize = (bytes) => {
  if (bytes === 0) return '0 Bytes'
  const k = 1024
  const sizes = ['Bytes', 'KB', 'MB', 'GB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return Math.round(bytes / Math.pow(k, i) * 100) / 100 + ' ' + sizes[i]
}
</script>
