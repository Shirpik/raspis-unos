<template>
  <div class="teacher-week-editor">
    <label class="form-label" for="teacher-week-date">Неделя, содержащая дату</label>
    <input id="teacher-week-date" type="date" class="form-input" :value="selectedWeek" @change="selectWeek($event.target.value)" />
    <p class="week-range">{{ dates[0] }} — {{ dates[6] }}</p>
    <label class="form-checkbox"><input type="checkbox" :checked="enabled" @change="toggleOverride($event.target.checked)" /> Особый график на эту неделю</label>
    <p v-if="!enabled" class="week-help">Недельное правило не задано. Действует обычный график преподавателя.</p>
    <template v-else>
      <p class="week-help">Отмеченные пары разрешены только на выбранной неделе. Снимите все плитки, чтобы преподаватель не работал всю неделю.</p>
      <WorkScheduleEditor :schedule="draft" :show-period="false" :show-date-overrides="false" :day-dates="dates" @change="commit" />
      <button type="button" class="btn btn-ghost btn-sm" @click="toggleOverride(false)">Вернуть обычный график этой недели</button>
    </template>
    <p v-if="dateExceptions.length" class="week-help">На {{ dateExceptions.join(', ') }} заданы исключения по датам. Они имеют приоритет над недельным графиком.</p>
    <div v-if="modelValue.length" class="saved-weeks">
      <span class="form-label">Недели с особым графиком</span>
      <button v-for="week in modelValue" :key="week.week_from" type="button" class="btn btn-secondary btn-sm" @click="selectWeek(week.week_from)">{{ week.week_from }} — {{ weekDates(week.week_from)[6] }}</button>
    </div>
  </div>
</template>

<script setup>
import { computed, ref, watch } from 'vue'
import WorkScheduleEditor from './WorkScheduleEditor.vue'
import { weekStart, weekDates, weekWorkDays, setWeekSchedule } from '../utils/weeklyWorkSchedule.js'

const props=defineProps({modelValue:{type:Array,default:()=>[]},schedule:{type:Object,required:true}})
const emit=defineEmits(['update:modelValue'])
const selectedWeek=ref(weekStart(new Date()))
const dates=computed(()=>weekDates(selectedWeek.value))
const current=computed(()=>props.modelValue.find(week=>week.week_from===selectedWeek.value))
const enabled=computed(()=>Boolean(current.value))
const draft=ref({work_days:[]})
const dateExceptions=computed(()=>(props.schedule.date_slot_overrides||[]).filter(item=>dates.value.includes(item.date)).map(item=>item.date))
function load(){draft.value={work_days:weekWorkDays(props.schedule,current.value)}}
watch(selectedWeek,load,{immediate:true})
function selectWeek(value){const start=weekStart(value);if(start)selectedWeek.value=start}
function commit(){emit('update:modelValue',setWeekSchedule(props.modelValue,selectedWeek.value,draft.value.work_days))}
function toggleOverride(value){
  if(value){load();commit()}
  else {emit('update:modelValue',setWeekSchedule(props.modelValue,selectedWeek.value,null));draft.value={work_days:weekWorkDays(props.schedule,null)}}
}
</script>

<style scoped>
.week-range{font-weight:600;margin:10px 0}.week-help{font-size:13px;color:var(--text-secondary);line-height:1.5;margin:10px 0}.saved-weeks{display:flex;gap:6px;flex-wrap:wrap;border-top:1px solid var(--border);padding-top:14px;margin-top:14px}.saved-weeks>span{width:100%}
</style>
