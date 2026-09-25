<template>
  <div class="file-uploader">
    <div
      class="upload-zone"
      :class="{ 'drag-over': isDragOver, 'has-file': selectedFile }"
      @dragover.prevent="isDragOver = true"
      @dragleave.prevent="isDragOver = false"
      @drop.prevent="handleDrop"
      @click="triggerFileInput"
    >
      <input
        ref="fileInput"
        type="file"
        :accept="accept"
        @change="handleFileSelect"
        style="display: none"
      />

      <div v-if="!selectedFile" class="upload-placeholder">
        <div class="upload-icon">
          <Upload :size="32" />
        </div>
        <div class="upload-text">
          <p class="upload-title">Перетащите файл или нажмите для выбора</p>
          <p class="upload-subtitle">{{ acceptLabel || 'Поддерживаемые форматы: ' + accept }}</p>
        </div>
      </div>

      <div v-else class="file-preview">
        <div class="file-info">
          <div class="file-icon">
            <FileText :size="24" />
          </div>
          <div class="file-details">
            <p class="file-name">{{ selectedFile.name }}</p>
            <p class="file-size">{{ formatFileSize(selectedFile.size) }}</p>
          </div>
        </div>
        <button
          type="button"
          class="btn btn-ghost btn-sm btn-icon remove-btn"
          @click.stop="removeFile"
        >
          <X :size="18" />
        </button>
      </div>
    </div>

    <button
      v-if="selectedFile && showUploadButton"
      type="button"
      class="btn btn-primary upload-btn"
      :disabled="uploading"
      @click="handleUpload"
    >
      <span v-if="!uploading">{{ uploadButtonText || 'Загрузить' }}</span>
      <span v-else class="spinner spinner-sm"></span>
    </button>
  </div>
</template>

<script setup>
import { ref } from 'vue'
import { Upload, FileText, X } from 'lucide-vue-next'

const props = defineProps({
  accept: {
    type: String,
    default: '.xlsx,.xls'
  },
  acceptLabel: {
    type: String,
    default: ''
  },
  showUploadButton: {
    type: Boolean,
    default: true
  },
  uploadButtonText: {
    type: String,
    default: 'Загрузить'
  }
})

const emit = defineEmits(['file-selected', 'upload'])

const fileInput = ref(null)
const selectedFile = ref(null)
const isDragOver = ref(false)
const uploading = ref(false)

const triggerFileInput = () => {
  if (!selectedFile.value) {
    fileInput.value?.click()
  }
}

const handleFileSelect = (event) => {
  const file = event.target.files?.[0]
  if (file) {
    selectedFile.value = file
    emit('file-selected', file)
  }
}

const handleDrop = (event) => {
  isDragOver.value = false
  const file = event.dataTransfer.files?.[0]
  if (file) {
    selectedFile.value = file
    emit('file-selected', file)
  }
}

const removeFile = () => {
  selectedFile.value = null
  if (fileInput.value) {
    fileInput.value.value = ''
  }
}

const handleUpload = () => {
  if (selectedFile.value) {
    uploading.value = true
    emit('upload', selectedFile.value)
    // Reset uploading state after a delay (parent should control this)
    setTimeout(() => {
      uploading.value = false
    }, 500)
  }
}

const formatFileSize = (bytes) => {
  if (bytes === 0) return '0 Bytes'
  const k = 1024
  const sizes = ['Bytes', 'KB', 'MB', 'GB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return Math.round(bytes / Math.pow(k, i) * 100) / 100 + ' ' + sizes[i]
}

defineExpose({
  removeFile,
  uploading
})
</script>

<style scoped>
.file-uploader {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.upload-zone {
  border: 2px dashed var(--border);
  border-radius: var(--radius);
  padding: 32px;
  background: var(--bg-secondary);
  cursor: pointer;
  transition: all var(--transition);
  position: relative;
  overflow: hidden;
}

.upload-zone::before {
  content: '';
  position: absolute;
  inset: 0;
  background: linear-gradient(135deg, transparent 0%, var(--accent) 100%);
  opacity: 0;
  transition: opacity var(--transition);
}

.upload-zone:hover::before {
  opacity: 0.03;
}

.upload-zone.drag-over {
  border-color: var(--accent);
  background: var(--bg-tertiary);
}

.upload-zone.drag-over::before {
  opacity: 0.05;
}

.upload-zone.has-file {
  cursor: default;
  border-style: solid;
  border-color: var(--accent);
  padding: 20px;
}

.upload-placeholder {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 16px;
  position: relative;
  z-index: 1;
}

.upload-icon {
  width: 64px;
  height: 64px;
  border-radius: var(--radius);
  background: var(--bg-tertiary);
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--accent);
  transition: all var(--transition);
}

.upload-zone:hover .upload-icon {
  transform: translateY(-4px);
  background: var(--accent-light);
}

.upload-text {
  text-align: center;
}

.upload-title {
  font-size: 15px;
  font-weight: 500;
  color: var(--text-primary);
  margin: 0 0 4px 0;
}

.upload-subtitle {
  font-size: 13px;
  color: var(--text-muted);
  margin: 0;
}

.file-preview {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  position: relative;
  z-index: 1;
}

.file-info {
  display: flex;
  align-items: center;
  gap: 12px;
  flex: 1;
  min-width: 0;
}

.file-icon {
  width: 48px;
  height: 48px;
  border-radius: var(--radius-sm);
  background: var(--accent-light);
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--accent);
  flex-shrink: 0;
}

.file-details {
  flex: 1;
  min-width: 0;
}

.file-name {
  font-size: 14px;
  font-weight: 500;
  color: var(--text-primary);
  margin: 0 0 4px 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.file-size {
  font-size: 13px;
  color: var(--text-muted);
  margin: 0;
}

.remove-btn {
  flex-shrink: 0;
}

.upload-btn {
  width: 100%;
}

@media (max-width: 640px) {
  .upload-zone {
    padding: 24px 16px;
  }

  .upload-icon {
    width: 56px;
    height: 56px;
  }

  .upload-title {
    font-size: 14px;
  }

  .upload-subtitle {
    font-size: 12px;
  }
}
</style>
