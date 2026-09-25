<template>
  <div class="teacher-schedule">
    <div class="search-bar">
      <input
        v-model.trim="searchQuery"
        class="form-input"
        placeholder="Поиск по ФИО или фамилии"
      />
      <button
        v-if="selectedTeacher"
        class="btn btn-secondary"
        @click="clearSelection"
      >
        Сбросить выбор
      </button>
    </div>

    <div v-if="!selectedTeacher && filteredTeachers.length > 0" class="teacher-list">
      <div
        v-for="teacher in filteredTeachers"
        :key="teacher.id"
        class="teacher-item"
        @click="selectTeacher(teacher)"
      >
        <div class="teacher-avatar">{{ initials(teacher.name) }}</div>
        <div class="teacher-info">
          <div class="teacher-name">{{ teacher.name }}</div>
          <div class="teacher-id">ID: {{ teacher.id }}</div>
        </div>
      </div>
    </div>

    <div v-else-if="!selectedTeacher && searchQuery && filteredTeachers.length === 0" class="empty-state">
      <Search :size="48" style="opacity: 0.3" />
      <h3>Преподаватель не найден</h3>
      <p>Измените строку поиска</p>
    </div>

    <div v-else-if="!selectedTeacher" class="empty-state">
      <User :size="48" style="opacity: 0.3" />
      <h3>Введите ФИО преподавателя</h3>
      <p>Используйте поиск выше</p>
    </div>

    <div v-if="selectedTeacher" class="schedule-content">
      <div class="teacher-header">
        <h2>{{ selectedTeacher.name }}</h2>
      </div>

      <div v-if="store.loading" class="center-block">
        <span class="spinner spinner-lg" style="color: var(--accent)" />
        <p style="margin-top:16px; color: var(--text-muted)">Загрузка расписания…</p>
      </div>

      <div v-else-if="store.error" class="empty-state">
        <AlertCircle :size="48" style="color: var(--error)" />
        <h3>{{ store.error }}</h3>
        <button class="btn btn-primary" style="margin-top:16px" @click="store.fetchPublished()">
          Повторить
        </button>
      </div>

      <div v-else-if="!store.scheduleData" class="empty-state">
        <Calendar :size="48" style="color: var(--text-muted)" />
        <h3>Расписание ещё не сформировано</h3>
      </div>

      <template v-else>
        <div class="week-nav">
          <button class="btn btn-ghost btn-sm" :disabled="weekIndex <= 0" @click="weekIndex--">
            ‹ Пред.
          </button>
          <span class="week-label">
            <strong>Неделя {{ weekIndex + 1 }}</strong>
            <span class="week-dates">{{ weekLabel }}</span>
          </span>
          <button
            class="btn btn-ghost btn-sm"
            :disabled="weekIndex >= sortedWeeks.length - 1"
            @click="weekIndex++"
          >
            След. ›
          </button>
        </div>

        <div v-if="weekDates.length === 0" class="empty-state">
          <CalendarX :size="48" style="opacity: 0.3" />
          <h3>На этой неделе занятий нет</h3>
        </div>

        <div v-else class="sched-scroll">
          <table class="sched-table">
            <thead>
              <tr>
                <th rowspan="2" class="th-slot sticky-col">
                  <Clock :size="18" />
                  <span class="slot-header-text">Пара</span>
                </th>
                <th v-for="dateStr in weekDates" :key="dateStr" class="th-day">
                  <span class="day-name">{{ getDayWeekday(dateStr).toUpperCase() }}</span>
                  <span class="day-date-sub">{{ formatDateShort(dateStr) }}</span>
                </th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="slotNum in displaySlots" :key="slotNum" class="slot-row">
                <td class="slot-label sticky-col">
                  <span class="slot-num">{{ slotNum }}</span>
                  <span class="slot-time">{{ getSlotTime(dateStr, slotNum) }}</span>
                </td>
                <td
                  v-for="dateStr in weekDates"
                  :key="dateStr"
                  class="slot-cell"
                >
                  <div v-if="getLessonsForSlot(dateStr, slotNum).length" class="cell-inner">
                    <div
                      v-for="(lesson, idx) in getLessonsForSlot(dateStr, slotNum)"
                      :key="idx"
                      class="cell-lesson"
                    >
                      <span class="cell-subject">{{ lesson.subject }}</span>
                      <span class="cell-detail">{{ lesson.group }}</span>
                      <span v-if="lesson.room" class="cell-room">каб. {{ lesson.room }}</span>
                    </div>
                  </div>
                  <span v-else class="cell-empty">—</span>
                </td>
              </tr>
            </tbody>
          </table>
        </div>
      </template>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, watch } from 'vue'
import { useScheduleStore } from '../stores/schedule.js'
import { useDataStore } from '../stores/data.js'
import { AlertCircle, Calendar, Search, User, Clock, CalendarX } from 'lucide-vue-next'

const store = useScheduleStore()
const dataStore = useDataStore()

const searchQuery = ref('')
const selectedTeacher = ref(null)
const weekIndex = ref(0)

onMounted(async () => {
  await Promise.all([store.fetchPublished(), dataStore.loadTeachers()])
  jumpToCurrentWeek()
})

const filteredTeachers = computed(() => {
  if (!searchQuery.value) return []
  const q = searchQuery.value.toLowerCase()
  return dataStore.teachers.filter(t => t.name.toLowerCase().includes(q))
})

function initials(name) {
  return name.split(/\s+/).map(w => w[0]).join('').slice(0, 2).toUpperCase()
}

function selectTeacher(teacher) {
  selectedTeacher.value = teacher
  weekIndex.value = 0
  jumpToCurrentWeek()
}

function clearSelection() {
  selectedTeacher.value = null
  searchQuery.value = ''
}

// Week logic
function parseDMY(str) {
  const [d, m, y] = str.split('.')
  return new Date(+y, +m - 1, +d)
}

function getMonday(date) {
  const d = new Date(date)
  const day = d.getDay()
  d.setDate(d.getDate() - (day === 0 ? 6 : day - 1))
  d.setHours(0, 0, 0, 0)
  return d
}

function fmtDate(date) {
  return `${String(date.getDate()).padStart(2, '0')}.${String(date.getMonth() + 1).padStart(2, '0')}.${date.getFullYear()}`
}

function formatDateShort(dateStr) {
  const [d, m] = dateStr.split('.')
  return `${d}.${m}`
}

function compareDMY(a, b) {
  return parseDMY(a) - parseDMY(b)
}

const teacherLessons = computed(() => {
  if (!selectedTeacher.value || !store.scheduleData) return {}
  const map = {}

  for (const group of store.scheduleData.groups || []) {
    for (const day of group.days || []) {
      if (!map[day.date]) map[day.date] = { weekday: day.weekday, slots: {} }

      for (const slot of day.slots || []) {
        if (!map[day.date].slots[slot.slot]) {
          map[day.date].slots[slot.slot] = { time: slot.time, lessons: [] }
        }

        for (const lesson of slot.lessons || []) {
          if (lesson.teacher_id === selectedTeacher.value.id) {
            map[day.date].slots[slot.slot].lessons.push({
              subject: lesson.name || '',
              group: group.group_name || '',
              room: lesson.room_name || '',
            })
          }
        }
      }
    }
  }

  return map
})

const allDates = computed(() => {
  return Object.keys(teacherLessons.value).sort(compareDMY)
})

const sortedWeeks = computed(() => {
  if (!allDates.value.length) return []
  const set = new Set()
  for (const dateStr of allDates.value) {
    const mon = getMonday(parseDMY(dateStr))
    set.add(mon.toISOString())
  }
  return [...set].sort().map(iso => {
    const mon = new Date(iso)
    const sun = new Date(mon)
    sun.setDate(sun.getDate() + 6)
    return { iso, monStr: fmtDate(mon), sunStr: fmtDate(sun) }
  })
})

const weekLabel = computed(() => {
  const w = sortedWeeks.value[weekIndex.value]
  return w ? `${w.monStr} — ${w.sunStr}` : ''
})

const weekDates = computed(() => {
  if (!sortedWeeks.value.length) return []
  const w = sortedWeeks.value[weekIndex.value]
  if (!w) return []
  return allDates.value.filter(dateStr => {
    return getMonday(parseDMY(dateStr)).toISOString() === w.iso
  })
})

function getDayWeekday(dateStr) {
  return teacherLessons.value[dateStr]?.weekday || ''
}

const displaySlots = computed(() => {
  const slots = new Set()
  for (const dateStr of weekDates.value) {
    const daySlots = teacherLessons.value[dateStr]?.slots || {}
    for (const slotNum of Object.keys(daySlots)) {
      slots.add(parseInt(slotNum))
    }
  }
  return [...slots].sort((a, b) => a - b)
})

function getSlotTime(dateStr, slotNum) {
  return teacherLessons.value[dateStr]?.slots[slotNum]?.time || ''
}

function getLessonsForSlot(dateStr, slotNum) {
  return teacherLessons.value[dateStr]?.slots[slotNum]?.lessons || []
}

function jumpToCurrentWeek() {
  if (!sortedWeeks.value.length) return
  const today = new Date()
  const mon = getMonday(today).toISOString()
  const idx = sortedWeeks.value.findIndex(w => w.iso === mon)
  weekIndex.value = idx >= 0 ? idx : 0
}

watch(selectedTeacher, jumpToCurrentWeek)
watch(() => store.scheduleData, jumpToCurrentWeek)
</script>

<style scoped>
.teacher-schedule { display: flex; flex-direction: column; gap: 20px; }

.search-bar {
  display: flex; gap: 12px; align-items: center;
}

.teacher-list {
  display: grid; grid-template-columns: repeat(auto-fill, minmax(240px, 1fr));
  gap: 12px;
}

.teacher-item {
  display: flex; align-items: center; gap: 12px; padding: 16px;
  background: var(--bg-secondary); border: 1px solid var(--border);
  border-radius: var(--radius); cursor: pointer;
  transition: all var(--transition);
}
.teacher-item:hover {
  border-color: var(--accent); background: var(--accent-light);
}

.teacher-avatar {
  width: 40px; height: 40px; border-radius: 50%;
  background: var(--accent-light); color: var(--accent);
  display: flex; align-items: center; justify-content: center;
  font-size: 14px; font-weight: 700; flex-shrink: 0;
}

.teacher-info { flex: 1; }
.teacher-name { font-weight: 600; font-size: 14px; }
.teacher-id { font-size: 12px; color: var(--text-muted); margin-top: 2px; }

.schedule-content { display: flex; flex-direction: column; gap: 16px; }

.teacher-header h2 {
  font-size: 18px; font-weight: 700; margin: 0;
  padding: 12px 16px; background: var(--bg-secondary);
  border: 1px solid var(--border); border-radius: var(--radius);
}

.week-nav {
  display: flex; align-items: center; justify-content: space-between; gap: 12px;
  background: var(--bg-secondary); border: 1px solid var(--border);
  border-radius: var(--radius); padding: 12px 16px;
}
.week-label { display: flex; flex-direction: column; align-items: center; gap: 2px; }
.week-label strong { font-size: 14px; }
.week-dates { font-size: 12px; color: var(--text-muted); }

.sched-scroll {
  overflow-x: auto; -webkit-overflow-scrolling: touch;
  border-radius: var(--radius); border: 1px solid var(--border-strong);
  box-shadow: 0 4px 24px rgba(0, 0, 0, 0.35);
}

.sched-table { border-collapse: collapse; font-size: 13px; width: 100%; }
.sticky-col { position: sticky; left: 0; z-index: 2; }

.th-slot {
  background: var(--bg-secondary); border-right: 2px solid var(--border-strong);
  border-bottom: 2px solid var(--border-strong); z-index: 4 !important;
  text-align: center; width: 84px; padding: 10px 8px;
}
.slot-header-icon { display: block; font-size: 18px; line-height: 1; margin-bottom: 4px; }
.slot-header-text {
  display: block; font-size: 10px; font-weight: 700;
  color: var(--text-secondary); text-transform: uppercase; letter-spacing: 0.08em;
}

.th-day {
  background: linear-gradient(135deg, #4f46e5 0%, #6366f1 60%, #818cf8 100%);
  color: #fff; text-align: center; padding: 10px 14px;
  border-left: 2px solid rgba(255, 255, 255, 0.18);
  border-bottom: 2px solid rgba(255, 255, 255, 0.18); white-space: nowrap;
  min-width: 180px;
}
.th-day:first-child { border-left: none; }
.day-name {
  display: block; font-size: 12px; font-weight: 800;
  letter-spacing: 0.08em; text-transform: uppercase;
}
.day-date-sub {
  display: block; font-size: 10px; font-weight: 400;
  opacity: 0.72; margin-top: 3px; letter-spacing: 0.04em;
}

.slot-label {
  background: var(--bg-secondary); padding: 10px 8px;
  display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 4px;
  border-right: 2px solid var(--border-strong); border-top: 1px solid var(--border);
  min-height: 56px;
}
.slot-num { font-size: 20px; font-weight: 800; color: var(--text-primary); line-height: 1; }
.slot-time {
  font-size: 9px; color: var(--text-muted); white-space: nowrap;
  letter-spacing: 0.03em; text-align: center;
}

.slot-cell {
  padding: 8px 10px; vertical-align: top; border-top: 1px solid var(--border);
  border-left: 2px solid var(--border-strong);
}

.cell-inner { display: flex; flex-direction: column; gap: 4px; }
.cell-lesson { display: flex; flex-direction: column; gap: 4px; }
.cell-lesson + .cell-lesson {
  margin-top: 7px; padding-top: 7px;
  border-top: 1px dashed var(--border-strong);
}
.cell-subject { font-size: 12px; font-weight: 600; color: var(--text-primary); }
.cell-detail {
  font-size: 10px; color: var(--text-secondary);
  background: rgba(51, 65, 85, 0.7); border-radius: 3px;
  padding: 1px 5px; display: inline-block; width: fit-content;
}
.cell-room { font-size: 10px; color: var(--success); font-weight: 600; }
.cell-empty {
  color: var(--text-muted); font-size: 14px; display: flex;
  align-items: center; justify-content: center; min-height: 40px; opacity: 0.3;
}

.center-block {
  display: flex; flex-direction: column; align-items: center; padding: 60px 20px;
}

@media (max-width: 640px) {
  .search-bar { flex-direction: column; align-items: stretch; }
  .week-nav { flex-wrap: wrap; }
  .teacher-list { grid-template-columns: 1fr; }
}
</style>
