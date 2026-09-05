<template>
  <section v-if="result" class="validation-panel" :class="result.ok ? 'validation-ok' : 'validation-failed'">
    <div class="validation-head">
      <div>
        <strong>{{ result.ok ? '✓ Проверка пройдена' : '✕ Найдены нарушения' }}</strong>
        <p>{{ result.message }}</p>
      </div>
      <span class="badge" :class="result.ok ? 'badge-success' : 'badge-error'">
        {{ result.ok ? 'ГОТОВО' : 'НЕ ПУБЛИКОВАТЬ' }}
      </span>
    </div>

    <div class="validation-summary">
      <span><b>{{ summary.hard_errors ?? 0 }}</b> нарушений</span>
      <span><b>{{ summary.warnings ?? 0 }}</b> рекомендаций</span>
      <span><b>{{ summary.events ?? 0 }}</b> событий</span>
      <span><b>{{ summary.scheduled_occurrences ?? 0 }}/{{ summary.planned_occurrences ?? 0 }}</b> квот</span>
      <span><b>{{ summary.remaining_hours ?? 0 }}</b> часов осталось в квоте периода</span>
    </div>

    <div class="validation-categories">
      <span
        v-for="category in result.categories || []"
        :key="category.id"
        class="validation-category"
        :class="category.passed ? 'category-ok' : 'category-error'"
      >
        {{ category.passed ? '✓' : '✕' }} {{ category.label }}
        <template v-if="category.hard_errors"> · {{ category.hard_errors }}</template>
        <template v-if="category.warnings"> · ⚠ {{ category.warnings }}</template>
      </span>
    </div>

    <details v-if="(result.issues || []).length" class="validation-issues" :open="!result.ok">
      <summary>Подробности ({{ result.issues.length }})</summary>
      <div class="issue-list">
        <div v-for="(issue, index) in result.issues.slice(0, 200)" :key="`${issue.code}-${index}`" class="issue" :class="`issue-${issue.severity}`">
          <div class="issue-title">
            <span>{{ issue.severity === 'error' ? '✕' : '⚠' }}</span>
            <strong>{{ issue.message }}</strong>
            <code>{{ issue.code }}</code>
          </div>
          <div v-if="Object.keys(issue.context || {}).length" class="issue-context">
            <span v-for="(value, key) in issue.context" :key="key">{{ key }}: {{ displayValue(value) }}</span>
          </div>
        </div>
        <p v-if="result.issues.length > 200" class="issues-truncated">
          Показаны первые 200 из {{ result.issues.length }}. Полный отчёт сохранён рядом с schedule_all.json.
        </p>
      </div>
    </details>
  </section>
</template>

<script setup>
import { computed } from 'vue'

const props = defineProps({ result: { type: Object, default: null } })
const summary = computed(() => props.result?.summary || {})

function displayValue(value) {
  return Array.isArray(value) ? value.join(', ') : String(value)
}
</script>

<style scoped>
.validation-panel { margin: 0 0 18px; padding: 16px 18px; border: 1px solid var(--border); border-radius: var(--radius); background: var(--bg-secondary); }
.validation-ok { border-color: color-mix(in srgb, var(--success) 55%, var(--border)); }
.validation-failed { border-color: color-mix(in srgb, var(--error) 60%, var(--border)); }
.validation-head { display: flex; justify-content: space-between; align-items: flex-start; gap: 16px; }
.validation-head strong { font-size: 16px; }
.validation-head p { margin: 5px 0 0; color: var(--text-secondary); font-size: 13px; }
.validation-summary { display: flex; flex-wrap: wrap; gap: 8px; margin: 14px 0; }
.validation-summary span { padding: 6px 9px; border-radius: 6px; background: var(--bg-tertiary); color: var(--text-secondary); font-size: 12px; }
.validation-summary b { color: var(--text-primary); }
.validation-categories { display: flex; flex-wrap: wrap; gap: 7px; }
.validation-category { padding: 5px 8px; border: 1px solid var(--border); border-radius: 999px; font-size: 11px; }
.category-ok { color: var(--success); }
.category-error { color: var(--error); border-color: color-mix(in srgb, var(--error) 50%, var(--border)); }
.validation-issues { margin-top: 14px; }
.validation-issues summary { cursor: pointer; color: var(--text-secondary); font-size: 13px; }
.issue-list { margin-top: 10px; display: flex; flex-direction: column; gap: 7px; max-height: 430px; overflow: auto; }
.issue { padding: 9px 10px; border-left: 3px solid var(--border); border-radius: 4px; background: var(--bg-primary); }
.issue-error { border-left-color: var(--error); }
.issue-warning { border-left-color: var(--warning, #f59e0b); }
.issue-title { display: flex; align-items: center; gap: 7px; font-size: 12px; }
.issue-title strong { flex: 1; }
.issue-title code { color: var(--text-muted); font-size: 10px; }
.issue-context { display: flex; flex-wrap: wrap; gap: 5px 12px; margin-top: 5px; color: var(--text-muted); font-size: 10px; }
.issues-truncated { color: var(--text-muted); font-size: 12px; }
@media (max-width: 640px) { .validation-head { flex-direction: column; } }
</style>
