<template>
  <section class="form-group">
    <label class="form-label">Минимум пар по датам</label>
    <p class="hint">Обязательная нагрузка в выбранный день. Укажите отдельное значение для дней с сокращённым графиком.</p>
    <div v-for="(target, index) in modelValue" :key="index" class="form-row">
      <label class="form-label">Дата<input v-model="target.date" type="date" class="form-input" /></label>
      <label class="form-label">Минимум пар<input v-model.number="target.minimum_pairs" type="number" min="0" max="7" class="form-input" /></label>
      <label class="form-label">Макс. пар одного предмета у подгруппы<input v-model.number="target.maximum_same_subject_pairs" type="number" min="0" max="7" placeholder="Общее правило" class="form-input" /><small>0 или пусто — общее правило. Общий предел пар у студентов сохраняется.</small></label>
      <button type="button" class="btn btn-ghost btn-sm" @click="remove(index)">Удалить</button>
    </div>
    <button type="button" class="btn btn-secondary btn-sm" @click="add">+ Дата и нагрузка</button>
  </section>
</template>

<script setup>
const props = defineProps({ modelValue: { type: Array, default: () => [] } })
const emit = defineEmits(['update:modelValue'])
const add = () => emit('update:modelValue', [...props.modelValue, { date: '', minimum_pairs: 0 }])
const remove = index => emit('update:modelValue', props.modelValue.filter((_, i) => i !== index))
</script>

<style scoped>
.hint { font-size: 12px; color: var(--text-secondary); margin-bottom: 10px; }
</style>
