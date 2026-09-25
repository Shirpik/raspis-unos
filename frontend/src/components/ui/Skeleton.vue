<template>
  <div :class="['skeleton', className]" />
</template>

<script setup>
defineProps({
  className: {
    type: String,
    default: ''
  }
})
</script>

<style scoped>
.skeleton {
  background: linear-gradient(
    90deg,
    var(--bg-tertiary) 0%,
    rgba(255, 255, 255, 0.05) 50%,
    var(--bg-tertiary) 100%
  );
  background-size: 200% 100%;
  animation: shimmer 1.5s ease-in-out infinite;
  border-radius: var(--radius);
  height: 20px;
}

@keyframes shimmer {
  0% { background-position: 200% 0; }
  100% { background-position: -200% 0; }
}

/* Utility classes */
.skeleton.w-4 { width: 1rem; }
.skeleton.w-12 { width: 3rem; }
.skeleton.w-16 { width: 4rem; }
.skeleton.w-20 { width: 5rem; }
.skeleton.w-24 { width: 6rem; }
.skeleton.w-28 { width: 7rem; }
.skeleton.w-32 { width: 8rem; }
.skeleton.w-36 { width: 9rem; }
.skeleton.w-40 { width: 10rem; }
.skeleton.w-48 { width: 12rem; }

.skeleton.h-3 { height: 0.75rem; }
.skeleton.h-4 { height: 1rem; }
.skeleton.h-5 { height: 1.25rem; }
.skeleton.h-6 { height: 1.5rem; }
.skeleton.h-8 { height: 2rem; }
.skeleton.h-12 { height: 3rem; }

.skeleton.rounded-full { border-radius: 9999px; }

.skeleton.mb-2 { margin-bottom: 0.5rem; }
.skeleton.mb-3 { margin-bottom: 0.75rem; }

.skeleton.flex-1 { flex: 1; }
</style>
