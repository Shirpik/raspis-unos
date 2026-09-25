<template>
  <div class="input-wrapper">
    <label v-if="label" class="form-label">{{ label }}</label>
    <div class="input-with-icon">
      <Calendar :size="18" class="input-icon" />
      <input
        type="date"
        :value="modelValue"
        @input="emit('update:modelValue', $event.target.value)"
        :placeholder="placeholder"
        :disabled="disabled"
        class="form-input"
      />
    </div>
  </div>
</template>

<script setup>
import { Calendar } from 'lucide-vue-next'

defineProps({
  modelValue: String,
  label: String,
  placeholder: String,
  disabled: Boolean
})

const emit = defineEmits(['update:modelValue'])
</script>

<style scoped>
.input-wrapper {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.input-with-icon {
  position: relative;
  display: flex;
  align-items: center;
}

.input-icon {
  position: absolute;
  left: 12px;
  color: var(--text-muted);
  pointer-events: none;
}

.input-with-icon .form-input {
  padding-left: 40px;
}

input[type="date"]::-webkit-calendar-picker-indicator {
  filter: invert(0.7);
  cursor: pointer;
}
</style>
