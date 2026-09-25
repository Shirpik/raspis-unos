<template>
  <div class="file-upload" :class="{ dragging: isDragging, disabled }">
    <input
      ref="fileInput"
      type="file"
      :accept="accept"
      :multiple="multiple"
      @change="handleFileChange"
      @click="(e) => e.target.value = ''"
      style="display: none"
    />

    <div
      class="upload-area"
      @click="!disabled && fileInput.click()"
      @dragover.prevent="handleDragOver"
      @dragleave.prevent="handleDragLeave"
      @drop.prevent="handleDrop"
    >
      <div class="upload-icon">
        <Upload :size="32" />
      </div>
      <div class="upload-text">
        <p class="upload-title">{{ title || 'Нажмите для загрузки или перетащите файл' }}</p>
        <p class="upload-subtitle">{{ subtitle || `Поддерживаются: ${accept || 'все форматы'}` }}</p>
      </div>
    </div>

    <div v-if="files.length > 0" class="file-list">
      <div v-for="(file, index) in files" :key="index" class="file-item">
        <FileText :size="18" />
        <div class="file-info">
          <span class="file-name">{{ file.name }}</span>
          <span class="file-size">{{ formatFileSize(file.size) }}</span>
        </div>
        <button
          class="file-remove"
          @click.stop="removeFile(index)"
          :disabled="disabled"
        >
          <X :size="16" />
        </button>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref } from 'vue'
import { Upload, FileText, X } from 'lucide-vue-next'

const props = defineProps({
  modelValue: Array,
  accept: String,
  multiple: Boolean,
  disabled: Boolean,
  title: String,
  subtitle: String,
  maxSize: { type: Number, default: 10 * 1024 * 1024 } // 10MB default
})

const emit = defineEmits(['update:modelValue', 'error'])

const fileInput = ref(null)
const files = ref(props.modelValue || [])
const isDragging = ref(false)

const formatFileSize = (bytes) => {
  if (bytes === 0) return '0 Bytes'
  const k = 1024
  const sizes = ['Bytes', 'KB', 'MB', 'GB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return Math.round(bytes / Math.pow(k, i) * 100) / 100 + ' ' + sizes[i]
}

const validateFile = (file) => {
  if (file.size > props.maxSize) {
    emit('error', `Файл ${file.name} слишком большой. Максимум: ${formatFileSize(props.maxSize)}`)
    return false
  }
  return true
}

const handleFileChange = (e) => {
  const newFiles = Array.from(e.target.files).filter(validateFile)
  if (props.multiple) {
    files.value = [...files.value, ...newFiles]
  } else {
    files.value = newFiles.slice(0, 1)
  }
  emit('update:modelValue', files.value)
}

const handleDragOver = (e) => {
  if (!props.disabled) {
    isDragging.value = true
  }
}

const handleDragLeave = (e) => {
  isDragging.value = false
}

const handleDrop = (e) => {
  isDragging.value = false
  if (props.disabled) return

  const newFiles = Array.from(e.dataTransfer.files).filter(validateFile)
  if (props.multiple) {
    files.value = [...files.value, ...newFiles]
  } else {
    files.value = newFiles.slice(0, 1)
  }
  emit('update:modelValue', files.value)
}

const removeFile = (index) => {
  files.value.splice(index, 1)
  emit('update:modelValue', files.value)
}
</script>

<style scoped>
.file-upload {
  width: 100%;
}

.upload-area {
  border: 2px dashed var(--border);
  border-radius: var(--radius-lg);
  padding: 32px 24px;
  text-align: center;
  cursor: pointer;
  transition: all var(--transition);
  background: var(--bg-secondary);
}

.upload-area:hover {
  border-color: var(--accent);
  background: var(--bg-tertiary);
}

.file-upload.dragging .upload-area {
  border-color: var(--accent);
  background: var(--accent-light);
}

.file-upload.disabled .upload-area {
  opacity: 0.5;
  cursor: not-allowed;
}

.upload-icon {
  color: var(--accent);
  margin-bottom: 16px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.upload-title {
  font-size: 14px;
  font-weight: 500;
  color: var(--text-primary);
  margin-bottom: 4px;
}

.upload-subtitle {
  font-size: 13px;
  color: var(--text-muted);
}

.file-list {
  margin-top: 16px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.file-item {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 16px;
  background: var(--bg-tertiary);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  transition: all var(--transition);
}

.file-item:hover {
  border-color: var(--border-strong);
}

.file-item > svg {
  color: var(--accent);
  flex-shrink: 0;
}

.file-info {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 2px;
  min-width: 0;
}

.file-name {
  font-size: 14px;
  font-weight: 500;
  color: var(--text-primary);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.file-size {
  font-size: 12px;
  color: var(--text-muted);
}

.file-remove {
  background: none;
  border: none;
  color: var(--text-muted);
  cursor: pointer;
  padding: 4px;
  border-radius: var(--radius-sm);
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all var(--transition);
  flex-shrink: 0;
}

.file-remove:hover {
  color: var(--error);
  background: rgba(239, 68, 68, 0.1);
}

.file-remove:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
</style>
