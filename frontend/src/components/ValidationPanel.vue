<template>
  <section v-if="result" class="validation-panel" :class="result.ok ? 'validation-ok' : 'validation-failed'">
    <div class="validation-head">
      <div class="validation-head-content">
        <div class="validation-title">
          <CheckCircle v-if="result.ok" :size="20" />
          <XCircle v-else :size="20" />
          <strong>{{ result.ok ? 'Проверка пройдена' : 'Найдены нарушения' }}</strong>
        </div>
        <p>{{ result.message }}</p>
      </div>
      <span class="badge" :class="result.ok ? 'badge-success' : 'badge-error'">
        {{ result.ok ? 'ГОТОВО' : 'НЕ ПУБЛИКОВАТЬ' }}
      </span>
    </div>

    <div class="validation-summary">
      <div class="summary-item">
        <AlertCircle :size="14" />
        <span><b>{{ summary.hard_errors ?? 0 }}</b> нарушений</span>
      </div>
      <div class="summary-item">
        <AlertTriangle :size="14" />
        <span><b>{{ summary.warnings ?? 0 }}</b> рекомендаций</span>
      </div>
      <div class="summary-item">
        <Calendar :size="14" />
        <span><b>{{ summary.events ?? 0 }}</b> событий</span>
      </div>
      <div class="summary-item">
        <CheckSquare :size="14" />
        <span><b>{{ summary.scheduled_occurrences ?? 0 }}/{{ summary.planned_occurrences ?? 0 }}</b> квот</span>
      </div>
      <div class="summary-item">
        <Clock :size="14" />
        <span><b>{{ summary.remaining_hours ?? 0 }}</b> часов осталось</span>
      </div>
    </div>

    <div class="validation-categories">
      <span
        v-for="category in result.categories || []"
        :key="category.id"
        class="validation-category"
        :class="category.passed ? 'category-ok' : 'category-error'"
      >
        <Check v-if="category.passed" :size="12" />
        <X v-else :size="12" />
        <span>{{ category.label }}</span>
        <template v-if="category.hard_errors">
          <span class="category-count">{{ category.hard_errors }}</span>
        </template>
        <template v-if="category.warnings">
          <AlertTriangle :size="10" />
          <span class="category-count">{{ category.warnings }}</span>
        </template>
      </span>
    </div>

    <details v-if="(result.issues || []).length" class="validation-issues" :open="!result.ok">
      <summary>
        <ChevronRight :size="16" class="chevron" />
        <span>Подробности ({{ result.issues.length }})</span>
      </summary>
      <div class="issue-list">
        <div v-for="(issue, index) in result.issues.slice(0, 200)" :key="`${issue.code}-${index}`" class="issue" :class="`issue-${issue.severity}`">
          <div class="issue-title">
            <XCircle v-if="issue.severity === 'error'" :size="16" />
            <AlertTriangle v-else :size="16" />
            <strong>{{ issue.message }}</strong>
            <code>{{ issue.code }}</code>
          </div>
          <div v-if="Object.keys(issue.context || {}).length" class="issue-context">
            <span v-for="(value, key) in issue.context" :key="key">{{ key }}: {{ displayValue(value) }}</span>
          </div>
        </div>
        <p v-if="result.issues.length > 200" class="issues-truncated">
          <Info :size="16" />
          <span>Показаны первые 200 из {{ result.issues.length }}. Полный отчёт сохранён рядом с schedule_all.json.</span>
        </p>
      </div>
    </details>
  </section>
</template>

<script setup>
import { computed } from 'vue'
import { CheckCircle, XCircle, AlertCircle, AlertTriangle, Calendar, CheckSquare, Clock, Check, X, ChevronRight, Info } from 'lucide-vue-next'

const props = defineProps({ result: { type: Object, default: null } })
const summary = computed(() => props.result?.summary || {})

function displayValue(value) {
  return Array.isArray(value) ? value.join(', ') : String(value)
}
</script>

<style scoped>
.validation-panel {
  margin: 0 0 20px; padding: 20px;
  border: 1px solid var(--border);
  border-radius: var(--radius);
  background: var(--bg-secondary);
  transition: all var(--transition);
}
.validation-ok {
  border-color: rgba(34, 197, 94, 0.3);
  background: linear-gradient(135deg, var(--bg-secondary) 0%, rgba(34, 197, 94, 0.03) 100%);
}
.validation-failed {
  border-color: rgba(239, 68, 68, 0.3);
  background: linear-gradient(135deg, var(--bg-secondary) 0%, rgba(239, 68, 68, 0.03) 100%);
}
.validation-head {
  display: flex; justify-content: space-between;
  align-items: flex-start; gap: 16px;
}
.validation-head-content { flex: 1; }
.validation-title {
  display: flex; align-items: center; gap: 10px;
  margin-bottom: 6px;
}
.validation-title strong { font-size: 16px; font-weight: 600; letter-spacing: -0.01em; }
.validation-ok .validation-title { color: var(--success); }
.validation-failed .validation-title { color: var(--error); }
.validation-head p {
  margin: 0; padding-left: 30px;
  color: var(--text-secondary); font-size: 13px; line-height: 1.5;
}

.validation-summary {
  display: flex; flex-wrap: wrap; gap: 10px;
  margin: 16px 0; padding-top: 16px;
  border-top: 1px solid var(--border);
}
.summary-item {
  display: flex; align-items: center; gap: 6px;
  padding: 8px 12px; border-radius: var(--radius-sm);
  background: var(--bg-tertiary);
  color: var(--text-secondary); font-size: 13px;
  border: 1px solid var(--border);
}
.summary-item b { color: var(--text-primary); font-weight: 600; }

.validation-categories {
  display: flex; flex-wrap: wrap; gap: 8px;
  padding-top: 16px; border-top: 1px solid var(--border);
}
.validation-category {
  display: flex; align-items: center; gap: 6px;
  padding: 6px 12px;
  border: 1px solid var(--border);
  border-radius: 999px;
  font-size: 12px; font-weight: 500;
  transition: all var(--transition);
}
.category-ok {
  color: var(--success);
  background: var(--success-light);
  border-color: rgba(34, 197, 94, 0.2);
}
.category-error {
  color: var(--error);
  background: var(--error-light);
  border-color: rgba(239, 68, 68, 0.2);
}
.category-count {
  display: inline-flex; align-items: center; justify-content: center;
  min-width: 18px; height: 18px; padding: 0 5px;
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.1);
  font-size: 11px; font-weight: 600;
}

.validation-issues {
  margin-top: 16px;
  padding-top: 16px;
  border-top: 1px solid var(--border);
}
.validation-issues summary {
  display: flex; align-items: center; gap: 8px;
  cursor: pointer;
  color: var(--text-primary);
  font-size: 14px; font-weight: 500;
  user-select: none;
  padding: 8px 0;
  transition: color var(--transition);
}
.validation-issues summary:hover { color: var(--accent); }
.validation-issues summary .chevron {
  transition: transform var(--transition);
}
.validation-issues[open] summary .chevron {
  transform: rotate(90deg);
}

.issue-list {
  margin-top: 12px;
  display: flex; flex-direction: column; gap: 8px;
  max-height: 450px; overflow-y: auto;
  padding-right: 4px;
}
.issue {
  padding: 12px 14px;
  border-left: 3px solid var(--border);
  border-radius: var(--radius-sm);
  background: var(--bg-primary);
  border: 1px solid var(--border);
  transition: all var(--transition);
}
.issue:hover {
  background: var(--bg-tertiary);
  border-color: var(--border-strong);
}
.issue-error {
  border-left-color: var(--error);
  background: rgba(239, 68, 68, 0.03);
}
.issue-warning {
  border-left-color: var(--warning);
  background: rgba(234, 179, 8, 0.03);
}
.issue-title {
  display: flex; align-items: center; gap: 8px;
  font-size: 13px;
}
.issue-error .issue-title { color: var(--error); }
.issue-warning .issue-title { color: var(--warning); }
.issue-title strong {
  flex: 1;
  color: var(--text-primary);
  font-weight: 500;
}
.issue-title code {
  color: var(--text-muted);
  font-size: 11px;
  padding: 2px 6px;
  border-radius: 4px;
  background: rgba(255, 255, 255, 0.05);
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
}
.issue-context {
  display: flex; flex-wrap: wrap; gap: 6px 14px;
  margin-top: 8px; padding-left: 24px;
  color: var(--text-muted); font-size: 11px;
}
.issues-truncated {
  display: flex; align-items: center; gap: 8px;
  padding: 12px;
  color: var(--text-muted);
  font-size: 12px;
  background: var(--bg-tertiary);
  border-radius: var(--radius-sm);
  border: 1px solid var(--border);
}

@media (max-width: 640px) {
  .validation-panel { padding: 16px; }
  .validation-head { flex-direction: column; align-items: stretch; }
  .validation-head p { padding-left: 0; margin-top: 6px; }
  .summary-item { font-size: 12px; padding: 6px 10px; }
  .validation-category { font-size: 11px; padding: 5px 10px; }
  .issue-list { max-height: 350px; }
}
</style>
