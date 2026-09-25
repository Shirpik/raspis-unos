<template>
  <div class="sidebar-item">
    <component
      :is="to ? 'RouterLink' : 'button'"
      :to="to"
      class="sidebar-link"
      :class="{ active: isActive, collapsed }"
      @click="handleClick"
    >
      <component v-if="icon" :is="icon" :size="20" class="sidebar-icon" />
      <span v-if="!collapsed" class="sidebar-label">{{ label }}</span>
      <span v-if="badge && !collapsed" class="sidebar-badge">{{ badge }}</span>
      <ChevronDown
        v-if="children && !collapsed"
        :size="16"
        class="sidebar-chevron"
        :class="{ open: isExpanded }"
      />
    </component>

    <div v-if="children && isExpanded && !collapsed" class="sidebar-children">
      <SidebarLink
        v-for="child in children"
        :key="child.to || child.label"
        v-bind="child"
        :collapsed="collapsed"
      />
    </div>
  </div>
</template>

<script setup>
import { computed, ref } from 'vue'
import { useRoute } from 'vue-router'
import { ChevronDown } from 'lucide-vue-next'

const props = defineProps({
  to: String,
  label: { type: String, required: true },
  icon: Object,
  badge: [String, Number],
  collapsed: Boolean,
  children: Array,
  defaultOpen: Boolean
})

const emit = defineEmits(['click'])
const route = useRoute()
const isExpanded = ref(props.defaultOpen || false)

const isActive = computed(() => {
  if (!props.to) return false
  return route.path === props.to
})

const handleClick = () => {
  if (props.children) {
    isExpanded.value = !isExpanded.value
  }
  emit('click')
}
</script>

<style scoped>
.sidebar-item {
  margin-bottom: 4px;
}

.sidebar-link {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 12px;
  border-radius: var(--radius);
  color: var(--text-secondary);
  text-decoration: none;
  font-size: 14px;
  font-weight: 500;
  transition: all var(--transition);
  position: relative;
  width: 100%;
  background: none;
  border: none;
  cursor: pointer;
  text-align: left;
}

.sidebar-link.collapsed {
  justify-content: center;
  padding: 10px;
}

.sidebar-link:hover {
  background: var(--bg-tertiary);
  color: var(--text-primary);
}

.sidebar-link.active {
  background: var(--accent-light);
  color: var(--accent);
}

.sidebar-link.active::before {
  content: '';
  position: absolute;
  left: 0;
  top: 50%;
  transform: translateY(-50%);
  width: 3px;
  height: 20px;
  background: var(--accent);
  border-radius: 0 2px 2px 0;
}

.sidebar-icon {
  flex-shrink: 0;
}

.sidebar-label {
  flex: 1;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.sidebar-badge {
  background: var(--accent);
  color: var(--bg-primary);
  font-size: 11px;
  font-weight: 600;
  padding: 2px 6px;
  border-radius: 10px;
  min-width: 20px;
  text-align: center;
  flex-shrink: 0;
}

.sidebar-chevron {
  flex-shrink: 0;
  transition: transform var(--transition);
}

.sidebar-chevron.open {
  transform: rotate(180deg);
}

.sidebar-children {
  padding-left: 32px;
  margin-top: 4px;
}

.sidebar-children .sidebar-link {
  padding: 8px 12px;
  font-size: 13px;
}
</style>
