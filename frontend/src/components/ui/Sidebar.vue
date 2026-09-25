<template>
  <div :class="cn('flex h-screen', className)">
    <!-- Sidebar -->
    <aside
      :class="cn(
        'fixed inset-y-0 left-0 z-50 w-64 transform bg-slate-900/95 backdrop-blur-md border-r border-slate-800 transition-transform duration-300 ease-in-out',
        isOpen ? 'translate-x-0' : '-translate-x-full',
        'md:relative md:translate-x-0'
      )"
    >
      <div class="flex h-full flex-col">
        <!-- Header -->
        <div class="flex h-16 items-center justify-between px-6 border-b border-slate-800">
          <slot name="header">
            <h2 class="text-lg font-semibold text-white">Меню</h2>
          </slot>
          <button
            @click="close"
            class="md:hidden p-2 hover:bg-slate-800 rounded-lg transition-colors"
          >
            <X :size="20" class="text-slate-400" />
          </button>
        </div>

        <!-- Content -->
        <div class="flex-1 overflow-y-auto py-4">
          <slot />
        </div>

        <!-- Footer -->
        <div v-if="$slots.footer" class="border-t border-slate-800 p-4">
          <slot name="footer" />
        </div>
      </div>
    </aside>

    <!-- Overlay for mobile -->
    <div
      v-if="isOpen"
      @click="close"
      class="fixed inset-0 z-40 bg-black/50 backdrop-blur-sm md:hidden"
    />

    <!-- Main content -->
    <div class="flex-1 flex flex-col min-w-0">
      <slot name="content" />
    </div>
  </div>
</template>

<script setup>
import { ref, watch } from 'vue'
import { X } from 'lucide-vue-next'
import { cn } from '../../lib/utils'

const props = defineProps({
  modelValue: {
    type: Boolean,
    default: false
  },
  className: {
    type: String,
    default: ''
  }
})

const emit = defineEmits(['update:modelValue'])

const isOpen = ref(props.modelValue)

watch(() => props.modelValue, (value) => {
  isOpen.value = value
})

const close = () => {
  isOpen.value = false
  emit('update:modelValue', false)
}
</script>
