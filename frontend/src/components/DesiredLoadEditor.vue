<template>
  <section class="form-group">
    <label class="form-label">Желаемая нагрузка и срок вычитки</label>
    <p class="hint">Минимум — в физических парах за полную неделю. Срок включает выбранную дату. Ограничения доступности не снимаются; невыполнимый план будет отклонён.</p>
    <div v-for="(rule, index) in modelValue" :key="index" class="rule">
      <label class="form-label">Группы (пусто — все выбранного курса)
        <select v-model="rule.group_ids" multiple class="form-select">
          <option v-for="group in groups" :key="group.id" :value="group.id">{{ group.name }}</option>
        </select>
      </label>
      <div class="form-row">
        <label class="form-label">Курс
          <select v-model.number="rule.course_year" class="form-select"><option :value="0">Все курсы</option><option v-for="year in 4" :value="year" :key="year">{{ year }} курс</option></select>
        </label>
        <label class="form-label">Вычитать до (включительно)<input v-model="rule.deadline" type="date" class="form-input" /></label>
        <label class="form-label">Минимум пар за неделю<input v-model.number="rule.minimum_pairs_per_week" type="number" min="0" max="42" class="form-input" /></label>
      </div>
      <button type="button" class="btn btn-ghost btn-sm" @click="remove(index)">Удалить условие</button>
    </div>
    <button type="button" class="btn btn-secondary btn-sm" @click="add">+ Группа / курс / срок</button>
  </section>
</template>

<script setup>
const props = defineProps({ modelValue: { type: Array, default: () => [] }, groups: { type: Array, default: () => [] } })
const emit = defineEmits(['update:modelValue'])
const add = () => emit('update:modelValue', [...props.modelValue, { group_ids: [], course_year: 0, deadline: '', minimum_pairs_per_week: 0 }])
const remove = index => emit('update:modelValue', props.modelValue.filter((_, i) => i !== index))
</script>

<style scoped>
.hint { font-size: 12px; color: var(--text-secondary); margin-bottom: 10px; }
.rule { border: 1px solid var(--border); padding: 12px; margin-bottom: 10px; border-radius: 8px; }
select[multiple] { min-height: 85px; }
</style>
