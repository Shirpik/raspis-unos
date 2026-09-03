<template>
  <div class="work-editor">
    <div class="period-grid">
      <div class="form-group"><label class="form-label">Работает с даты</label><input v-model="schedule.work_period.from" type="date" class="form-input" /></div>
      <div class="form-group"><label class="form-label">Работает по дату</label><input v-model="schedule.work_period.to" type="date" class="form-input" /></div>
    </div>
    <label class="form-label">Рабочие дни и допустимые пары</label>
    <div class="quick-actions"><button type="button" class="preset" @click="setPreset(5,1,5)">ПН–ПТ, 1–5</button><button type="button" class="preset" @click="setPreset(6,1,7)">ПН–СБ, 1–7</button><button type="button" class="preset" @click="setPreset(6,2,6)">ПН–СБ, 2–6</button><button type="button" class="preset" @click="disableAll">Без рабочих дней</button><button type="button" class="preset" @click="clearPeriod">Весь семестр</button></div>
    <div class="slot-caption"><span>День</span><span v-for="n in 7" :key="n">{{ n }}</span></div>
    <div class="day-grid">
      <div v-for="day in schedule.work_days" :key="day.day" :class="['day-row',{off:!day.enabled}]">
        <button type="button" :class="['day-name',{active:day.enabled}]" @click="toggleDay(day)">{{ names[day.day] }}</button>
        <button v-for="n in 7" :key="n" type="button" :class="['slot-tile',{active:isActive(day,n)}]" :aria-label="`${names[day.day]}, ${n} пара`" @click="toggleSlot(day,n)">{{ n }}</button>
      </div>
    </div>
  </div>
</template>

<script setup>
const props=defineProps({ schedule: { type: Object, required: true } })
const names=['','ПН','ВТ','СР','ЧТ','ПТ','СБ','ВС']
function slots(day){if(!Array.isArray(day.slots))day.slots=day.enabled?Array.from({length:Math.max(0,(day.end_slot||7)-(day.start_slot||1)+1)},(_,i)=>(day.start_slot||1)+i):[];return day.slots}
function sync(day){day.slots=[...new Set(slots(day).map(Number).filter(n=>n>=1&&n<=7))].sort((a,b)=>a-b);day.enabled=day.slots.length>0;if(day.enabled){day.start_slot=day.slots[0];day.end_slot=day.slots.at(-1)}else{day.start_slot=1;day.end_slot=7}}
function isActive(day,n){return day.enabled&&slots(day).includes(n)}
function toggleSlot(day,n){const values=slots(day);const index=values.indexOf(n);if(index>=0)values.splice(index,1);else values.push(n);sync(day)}
function toggleDay(day){if(day.enabled){day.slots=[]}else{day.slots=Array.from({length:7},(_,i)=>i+1)}sync(day)}
function setPreset(lastDay,start,end){for(const day of props.schedule.work_days){day.slots=day.day<=lastDay?Array.from({length:end-start+1},(_,i)=>start+i):[];sync(day)}}
function disableAll(){for(const day of props.schedule.work_days){day.slots=[];sync(day)}}
function clearPeriod(){props.schedule.work_period.from='';props.schedule.work_period.to=''}
</script>

<style scoped>
.work-editor{margin-top:14px;border-top:1px solid var(--border);padding-top:14px}.period-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}.quick-actions{display:flex;gap:6px;flex-wrap:wrap;margin:7px 0 10px}.preset{border:1px solid var(--border);background:var(--bg-secondary);color:var(--text-secondary);border-radius:7px;padding:5px 9px;font-size:12px;cursor:pointer}.preset:hover{border-color:var(--accent);color:var(--accent)}.slot-caption,.day-row{display:grid;grid-template-columns:58px repeat(7,minmax(34px,1fr));gap:6px;align-items:center}.slot-caption{margin-bottom:5px;color:var(--text-muted);font-size:11px;text-align:center}.slot-caption span:first-child{text-align:left}.day-grid{display:grid;gap:7px}.day-row.off{opacity:.72}.day-name,.slot-tile{height:34px;border:1px solid var(--border);background:var(--bg-secondary);color:var(--text-secondary);border-radius:8px;cursor:pointer;font-weight:600}.day-name.active,.slot-tile.active{border-color:var(--accent);background:var(--accent);color:#fff}.slot-tile:hover,.day-name:hover{border-color:var(--accent)}@media(max-width:650px){.period-grid{grid-template-columns:1fr}.slot-caption,.day-row{grid-template-columns:46px repeat(7,minmax(30px,1fr));gap:4px}.slot-tile,.day-name{height:32px;font-size:12px}}
</style>
