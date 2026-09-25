<template>
  <component
    :is="to ? 'router-link' : 'button'"
    :to="to"
    :class="cn(
      'flex items-center gap-3 px-4 py-2.5 mx-2 rounded-lg text-sm font-medium transition-all',
      'hover:bg-slate-800/70 active:scale-[0.98]',
      active
        ? 'bg-orange-500/10 text-orange-400 border border-orange-500/20'
        : 'text-slate-300 hover:text-white',
      disabled && 'opacity-50 cursor-not-allowed hover:bg-transparent',
      className
    )"
    :disabled="disabled"
    @click="handleClick"
  >
    <component v-if="icon" :is="icon" :size="18" />
    <span class="flex-1">{{ label }}</span>
    <span v-if="badge" class="badge badge-sm badge-accent">{{ badge }}</span>
  </component>
</template>

<script setup>
import { cn } from '../../lib/utils'

const props = defineProps({
  to: {
    type: [String, Object],
    default: null
  },
  label: {
    type: String,
    required: true
  },
  icon: {
    type: Object,
    default: null
  },
  active: {
    type: Boolean,
    default: false
  },
  badge: {
    type: [String, Number],
    default: null
  },
  disabled: {
    type: Boolean,
    default: false
  },
  className: {
    type: String,
    default: ''
  }
})

const emit = defineEmits(['click'])

const handleClick = (e) => {
  if (!props.disabled) {
    emit('click', e)
  }
}
</script>
