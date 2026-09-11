<template>
  <div class="practice-editor">
    <strong>Производственная практика и срок вычитки</strong>
    <p class="help">Обычные занятия завершаются до начала практики. Во время практики группа недоступна для генерации. Часы берутся из вклеек за выбранный семестр.</p>
    <div v-for="(period,index) in modelValue" :key="index" class="practice-row">
      <label>Начало<input v-model="period.from" type="date" class="form-input" /></label>
      <label>Окончание<input v-model="period.to" type="date" :min="period.from" class="form-input" /></label>
      <button type="button" class="btn btn-ghost" @click="$emit('update:modelValue',modelValue.filter((_,i)=>i!==index))">Удалить</button>
    </div>
    <button type="button" class="btn btn-secondary btn-sm" @click="$emit('update:modelValue',[...modelValue,{from:'',to:'',kind:'industrial'}])">Добавить период практики</button>
  </div>
</template>
<script setup>
defineProps({modelValue:{type:Array,default:()=>[]}})
defineEmits(['update:modelValue'])
</script>
<style scoped>
.practice-editor{padding:14px;border:1px solid var(--border);border-radius:8px;margin:14px 0}.help{color:var(--text-secondary);font-size:13px}.practice-row{display:flex;gap:8px;align-items:end;margin-bottom:10px}.practice-row label{flex:1;font-size:13px}@media(max-width:600px){.practice-row{flex-wrap:wrap}}
</style>
