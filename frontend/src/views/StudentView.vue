<template>
  <div class="student-page">
    <div class="student-header">
      <RouterLink to="/" class="back-link">
        <ArrowLeft :size="16" />
        <span>Назад</span>
      </RouterLink>
      <h1 class="page-title">Расписание занятий</h1>
    </div>

    <!-- Group selector -->
    <div class="selector-card">
      <div class="selector-section">
        <div class="selector-label">
          <GraduationCap :size="16" />
          <span>Выберите курс</span>
        </div>
        <div class="year-grid">
          <button
            v-for="tab in yearTabs"
            :key="tab.value"
            class="year-card"
            :class="{ active: selectedYear === tab.value }"
            @click="onYearSelect(tab.value)"
          >
            <span class="year-number">{{ tab.value }}</span>
            <span class="year-label">курс</span>
            <span v-if="tab.count > 0" class="year-count">{{ tab.count }}</span>
          </button>
        </div>
      </div>

      <div v-if="groupsForYear.length > 0" class="selector-section">
        <div class="selector-label">
          <Users :size="16" />
          <span>Выберите группу</span>
        </div>
        <div class="group-grid">
          <button
            v-for="g in groupsForYear"
            :key="g.group_index"
            class="group-card"
            :class="{ active: selectedGroupIndex === g.group_index }"
            @click="selectedGroupIndex = g.group_index"
          >
            {{ g.group_name }}
          </button>
        </div>
      </div>
    </div>

    <!-- Loading -->
    <div v-if="store.loading" class="loading-state">
      <div class="selector-card">
        <div class="selector-section">
          <Skeleton class="w-32 h-4 mb-3" />
          <div class="year-grid">
            <Skeleton v-for="i in 4" :key="i" class="h-24" />
          </div>
        </div>
        <div class="selector-section">
          <Skeleton class="w-32 h-4 mb-3" />
          <div class="group-grid">
            <Skeleton v-for="i in 6" :key="i" class="h-12" />
          </div>
        </div>
      </div>
      <div class="week-nav">
        <Skeleton class="w-20 h-9" />
        <div class="week-label">
          <Skeleton class="w-24 h-4" />
          <Skeleton class="w-32 h-3 mt-1" />
        </div>
        <Skeleton class="w-20 h-9" />
      </div>
      <Skeleton class="w-full h-96" />
    </div>

    <!-- Error -->
    <div v-else-if="store.error" class="empty-state">
      <AlertCircle :size="48" style="color: var(--error)" />
      <h3>{{ store.error }}</h3>
      <p>Убедитесь, что сервер запущен</p>
      <button class="btn btn-primary" style="margin-top:16px" @click="store.fetchPublished()">Повторить</button>
    </div>

    <!-- No schedule -->
    <div v-else-if="!store.scheduleData || store.groups.length === 0" class="empty-state">
      <Calendar :size="48" style="color: var(--text-muted)" />
      <h3>Расписание ещё не сформировано</h3>
      <p>Обратитесь к диспетчеру учебного процесса</p>
    </div>

    <!-- Pick a group prompt -->
    <div v-else-if="selectedGroupIndex === null" class="empty-state">
      <GraduationCap :size="48" style="color: var(--accent)" />
      <h3>Выберите курс и группу</h3>
      <p>Используйте панель выше для выбора</p>
    </div>

    <!-- Schedule -->
    <template v-else>
      <div class="week-nav">
        <button class="btn btn-secondary btn-sm" :disabled="weekIndex <= 0" @click="weekIndex--">
          <ChevronLeft :size="16" />
          <span>Пред.</span>
        </button>
        <div class="week-label">
          <span class="week-title">Неделя {{ weekIndex + 1 }}</span>
          <span class="week-dates">{{ weekLabel }}</span>
        </div>
        <button class="btn btn-secondary btn-sm" :disabled="weekIndex >= sortedWeeks.length - 1" @click="weekIndex++">
          <span>След.</span>
          <ChevronRight :size="16" />
        </button>
      </div>

      <div v-if="weekDates.length === 0" class="empty-state">
        <Calendar :size="48" style="color: var(--text-muted)" />
        <h3>На этой неделе занятий нет</h3>
      </div>

      <div v-else class="sched-scroll">
        <table class="sched-table">
          <thead>
            <tr>
              <th rowspan="2" class="th-slot sticky-col">
                <Clock :size="18" style="margin-bottom: 4px" />
                <span class="slot-header-text">Пара</span>
              </th>
              <th
                v-for="dateStr in weekDates"
                :key="dateStr"
                class="th-day"
              >
                <span class="day-name">{{ getDayWeekday(dateStr).toUpperCase() }}</span>
                <span class="day-date-sub">{{ formatDateShort(dateStr) }}</span>
              </th>
            </tr>
            <tr>
              <th
                v-for="dateStr in weekDates"
                :key="dateStr"
                class="th-group day-separator"
              >
                {{ selectedGroupName }}
              </th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="slotNum in displaySlots" :key="slotNum" :class="['slot-row',{'class-hour-row':slotNum===0}]">
              <td class="slot-label sticky-col">
                <span class="slot-num">{{ slotNum }}</span>
                <span class="slot-time">{{ getSlotTime(slotNum) }}</span>
              </td>
              <td
                v-for="dateStr in weekDates"
                :key="dateStr"
                class="slot-cell day-separator"
                :class="getCellClass(dateStr, slotNum)"
              >
                <div class="cell-inner" v-if="getCellLessonRows(dateStr, slotNum).length">
                  <div
                    v-for="(lesson, lessonIndex) in getCellLessonRows(dateStr, slotNum)"
                    :key="lesson.key || lessonIndex"
                    class="cell-lesson"
                  >
                    <span class="cell-subject">{{ lesson.subject }}</span>
                    <span v-if="lesson.subgroupLabel" class="cell-detail">{{ lesson.subgroupLabel }}</span>
                    <span v-for="(detail, i) in lesson.details" :key="i" class="cell-detail">{{ detail }}</span>
                    <span v-if="lesson.roomLabel" class="cell-room">{{ lesson.roomLabel }}</span>
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
</template>

<script setup>
import { ref, computed, onMounted, watch } from 'vue'
import { useScheduleStore } from '../stores/schedule.js'
import { collectSlotNumbers, slotLessonEntries } from '../utils/schedulePresentation.js'
import { GraduationCap, Users, ArrowLeft, Calendar, AlertCircle, ChevronLeft, ChevronRight, Clock } from 'lucide-vue-next'
import Skeleton from '../components/ui/Skeleton.vue'

const store = useScheduleStore()

const selectedYear = ref(0)
const selectedGroupIndex = ref(null)
const weekIndex = ref(0)

onMounted(async () => {
  await store.fetchPublished()
  jumpToCurrentWeek()
})

// ── Helpers ──────────────────────────────────────────────────────────────

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

function detectCourseYear(name) {
  const m4 = name.match(/[^\d]2(\d)\d{2}/)
  if (m4) {
    const d = parseInt(m4[1])
    const enrollYear = 2020 + d
    const year = 2025 - enrollYear + 1
    if (year >= 1 && year <= 4) return year
  }
  const m1 = name.match(/-(\d)/)
  if (m1) {
    const d = parseInt(m1[1])
    if (d >= 1 && d <= 4) return d
  }
  return null
}

// ── Year / Group selection ────────────────────────────────────────────────

const yearTabs = computed(() => {
  const counts = [0, 0, 0, 0]
  for (const g of store.groups) {
    const y = detectCourseYear(g.group_name)
    if (y && y >= 1 && y <= 4) counts[y - 1]++
  }
  return [
    { value: 1, label: '1 курс', count: counts[0] },
    { value: 2, label: '2 курс', count: counts[1] },
    { value: 3, label: '3 курс', count: counts[2] },
    { value: 4, label: '4 курс', count: counts[3] },
  ]
})

const groupsForYear = computed(() => {
  if (selectedYear.value === 0) return store.groups
  return store.groups.filter(g => detectCourseYear(g.group_name) === selectedYear.value)
})

const selectedGroupName = computed(() => {
  return store.groups.find(g => g.group_index === selectedGroupIndex.value)?.group_name || ''
})

function onYearSelect(year) {
  selectedYear.value = year
  selectedGroupIndex.value = null
}

// Auto-select first group when only one choice
watch(groupsForYear, (list) => {
  if (list.length === 1) selectedGroupIndex.value = list[0].group_index
  else if (!list.find(g => g.group_index === selectedGroupIndex.value)) selectedGroupIndex.value = null
})

// ── Slot lookup for selected group ────────────────────────────────────────

const groupData = computed(() => {
  return store.groups.find(g => g.group_index === selectedGroupIndex.value) || null
})

const slotLookup = computed(() => {
  const map = {}
  if (!groupData.value) return map
  for (const day of groupData.value.days) {
    map[day.date] = { weekday: day.weekday, slots: {} }
    for (const s of day.slots) {
      map[day.date].slots[s.slot] = {
        time: s.time,
        text: s.text,
        lessons: Array.isArray(s.lessons) ? s.lessons : [],
      }
    }
  }
  return map
})

// ── Weeks ─────────────────────────────────────────────────────────────────

const sortedWeeks = computed(() => {
  if (!groupData.value) return []
  const set = new Set()
  for (const d of groupData.value.days) {
    const mon = getMonday(parseDMY(d.date))
    set.add(mon.toISOString())
  }
  return [...set].sort().map(iso => {
    const mon = new Date(iso)
    const sun = new Date(mon); sun.setDate(sun.getDate() + 6)
    return { iso, monStr: fmtDate(mon), sunStr: fmtDate(sun) }
  })
})

const weekLabel = computed(() => {
  const w = sortedWeeks.value[weekIndex.value]
  return w ? `${w.monStr} — ${w.sunStr}` : ''
})

const weekDates = computed(() => {
  if (!sortedWeeks.value.length || !groupData.value) return []
  const w = sortedWeeks.value[weekIndex.value]
  if (!w) return []
  const set = new Set()
  for (const d of groupData.value.days) {
    if (getMonday(parseDMY(d.date)).toISOString() === w.iso) set.add(d.date)
  }
  return [...set].sort(compareDMY)
})

function getDayWeekday(dateStr) {
  return slotLookup.value[dateStr]?.weekday || ''
}

function jumpToCurrentWeek() {
  if (!sortedWeeks.value.length) return
  const today = new Date()
  const mon = getMonday(today).toISOString()
  const idx = sortedWeeks.value.findIndex(w => w.iso === mon)
  weekIndex.value = idx >= 0 ? idx : 0
}

watch(selectedGroupIndex, () => {
  jumpToCurrentWeek()
})

watch(() => store.scheduleData, jumpToCurrentWeek)

// ── Max slots ─────────────────────────────────────────────────────────────

const displaySlots = computed(() => {
  return collectSlotNumbers(groupData.value ? [groupData.value] : [])
})

// ── Cell helpers ──────────────────────────────────────────────────────────

function getSlotTime(slotNum) {
  for (const dateStr of weekDates.value) {
    const s = slotLookup.value[dateStr]?.slots[slotNum]
    if (s?.time) return s.time
  }
  return ''
}

function getCellText(dateStr, slotNum) {
  const s = slotLookup.value[dateStr]?.slots[slotNum]
  if (!s || s.text === '-') return null
  return s.text
}

function getCellLessonRows(dateStr, slotNum) {
  const cell = slotLookup.value[dateStr]?.slots[slotNum]
  return slotLessonEntries(cell, groupData.value?.group_index).map((entry, index) => {
    const segment = entry.segment || ''
    const separator = segment.indexOf(' — ')
    const rawDetails = separator >= 0
      ? segment.slice(separator + 3).split(', ').filter(Boolean)
      : []
    const details = rawDetails.filter(detail =>
      !/^(вся группа|[12]-?я?\s*(подгруппа|п\/?г)|подгруппа)/i.test(detail))
    const lesson = entry.lesson
    const roomName = lesson?.room_name === null || lesson?.room_name === undefined
      ? ''
      : String(lesson.room_name).trim()
    return {
      key: lesson ? `${lesson.uid || lesson.id}-${index}` : `legacy-${index}-${segment}`,
      subject: lesson?.name || (separator >= 0 ? segment.slice(0, separator) : segment),
      subgroupLabel: entry.subgroupLabel,
      details,
      roomLabel: roomName ? `каб. ${roomName}` : '',
    }
  })
}

function getCellClass(dateStr, slotNum) {
  const text = getCellText(dateStr, slotNum)
  if (!text) return 'cell-empty-type'
  const lo = text.toLowerCase()
  if (lo.includes('лпз') || lo.includes('лаб')) return 'cell-lab'
  if (lo.includes('уп-') || lo.includes('уп ') || lo.includes('практик')) return 'cell-practice'
  return 'cell-normal'
}

function parseSubject(text) {
  if (!text) return ''
  const idx = text.indexOf(' — ')
  return idx >= 0 ? text.slice(0, idx) : text
}

function parseDetails(text) {
  if (!text) return []
  const idx = text.indexOf(' — ')
  if (idx < 0) return []
  return text.slice(idx + 3).split(', ').filter(Boolean)
}
</script>

<style scoped>
.student-page {
  min-height: 100vh;
  padding: 24px 20px 40px;
  max-width: 1400px;
  margin: 0 auto;
  width: 100%;
}

.student-header {
  display: flex;
  align-items: center;
  gap: 20px;
  margin-bottom: 24px;
  flex-wrap: wrap;
}

.back-link {
  display: flex;
  align-items: center;
  gap: 6px;
  color: var(--text-secondary);
  text-decoration: none;
  font-size: 14px;
  font-weight: 500;
  padding: 8px 12px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border);
  background: var(--bg-secondary);
  transition: all var(--transition);
  white-space: nowrap;
}
.back-link:hover { color: var(--text-primary); border-color: var(--accent); transform: translateX(-2px); }

.page-title { font-size: 22px; font-weight: 700; margin: 0; letter-spacing: -0.02em; }

/* ── Selector card ─────────────────────────────────────────────────────── */
.selector-card {
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 20px;
  margin-bottom: 24px;
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.selector-section {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.selector-label {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  font-weight: 600;
  color: var(--text-secondary);
  text-transform: uppercase;
  letter-spacing: 0.05em;
}
.selector-label svg {
  color: var(--accent);
}

.year-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(100px, 1fr));
  gap: 12px;
}

.year-card {
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 20px 16px;
  background: var(--bg-tertiary);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  cursor: pointer;
  transition: all var(--transition);
}
.year-card:hover {
  border-color: var(--accent);
  transform: translateY(-2px);
  box-shadow: 0 4px 12px rgba(251, 146, 60, 0.15);
}
.year-card.active {
  background: var(--accent-light);
  border-color: var(--accent);
  box-shadow: 0 0 0 3px rgba(251, 146, 60, 0.1);
}
.year-number {
  font-size: 32px;
  font-weight: 800;
  color: var(--text-primary);
  line-height: 1;
  margin-bottom: 4px;
}
.year-card.active .year-number {
  color: var(--accent);
}
.year-label {
  font-size: 12px;
  color: var(--text-muted);
  font-weight: 500;
}
.year-count {
  position: absolute;
  top: 8px;
  right: 8px;
  background: var(--bg-primary);
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 2px 8px;
  font-size: 11px;
  font-weight: 600;
  color: var(--text-secondary);
}
.year-card.active .year-count {
  background: var(--accent);
  color: #fff;
  border-color: var(--accent);
}

.group-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(140px, 1fr));
  gap: 10px;
}

.group-card {
  padding: 14px 16px;
  background: var(--bg-tertiary);
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  font-size: 14px;
  font-weight: 600;
  color: var(--text-secondary);
  cursor: pointer;
  transition: all var(--transition);
  text-align: center;
}
.group-card:hover {
  border-color: var(--accent);
  color: var(--text-primary);
  transform: translateY(-1px);
}
.group-card.active {
  background: var(--accent-light);
  border-color: var(--accent);
  color: var(--accent);
  box-shadow: 0 0 0 3px rgba(251, 146, 60, 0.1);
}

/* ── Week nav ──────────────────────────────────────────────────────────── */
.week-nav {
  display: flex; align-items: center; justify-content: space-between; gap: 12px;
  margin-bottom: 20px; background: var(--bg-secondary); border: 1px solid var(--border);
  border-radius: var(--radius); padding: 12px 16px;
}
.week-label {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 4px;
  flex: 1;
}
.week-title {
  font-size: 15px;
  font-weight: 600;
  color: var(--text-primary);
}
.week-dates {
  font-size: 12px;
  color: var(--text-muted);
}

/* ── Table ─────────────────────────────────────────────────────────────── */
.sched-scroll {
  overflow-x: auto;
  -webkit-overflow-scrolling: touch;
  border-radius: var(--radius);
  border: 1px solid var(--border-strong);
  box-shadow: 0 4px 24px rgba(0, 0, 0, 0.35);
}

.sched-table { border-collapse: collapse; font-size: 13px; width: 100%; table-layout: auto; }
.sticky-col { position: sticky; left: 0; z-index: 2; }

.th-slot {
  background: var(--bg-secondary); border-right: 2px solid var(--border-strong);
  border-bottom: 2px solid var(--border-strong); z-index: 4 !important;
  text-align: center; width: 84px; min-width: 76px; padding: 10px 8px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 4px;
}
.th-slot svg {
  color: var(--accent);
}
.slot-header-text {
  display: block; font-size: 10px; font-weight: 700;
  color: var(--text-secondary); text-transform: uppercase; letter-spacing: 0.08em;
}

.th-day {
  background: linear-gradient(135deg, #4f46e5 0%, #6366f1 60%, #818cf8 100%);
  color: #fff; text-align: center; padding: 10px 14px;
  border-left: 2px solid rgba(255, 255, 255, 0.18);
  border-bottom: 1px solid rgba(255, 255, 255, 0.18); white-space: nowrap;
}
.th-day:first-child { border-left: none; }
.day-name { display: block; font-size: 12px; font-weight: 800; letter-spacing: 0.08em; text-transform: uppercase; }
.day-date-sub { display: block; font-size: 10px; font-weight: 400; opacity: 0.72; margin-top: 3px; letter-spacing: 0.04em; }

.th-group {
  background: #1a2540; color: var(--text-secondary); text-align: center;
  font-size: 10px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.06em;
  padding: 6px 10px; white-space: nowrap;
  border-bottom: 2px solid var(--border-strong); min-width: 180px;
}
.th-group.day-separator { border-left: 2px solid var(--border-strong); }

.slot-label {
  background: var(--bg-secondary); padding: 10px 8px;
  display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 4px;
  border-right: 2px solid var(--border-strong); border-top: 1px solid var(--border);
  min-height: 56px; transition: background var(--transition);
}
.slot-num { font-size: 20px; font-weight: 800; color: var(--text-primary); line-height: 1; }
.slot-time { font-size: 9px; color: var(--text-muted); white-space: nowrap; letter-spacing: 0.03em; text-align: center; }

.slot-cell {
  padding: 8px 10px; vertical-align: top;
  border-top: 1px solid var(--border); transition: background var(--transition);
}
.slot-cell.day-separator { border-left: 2px solid var(--border-strong); }
.slot-row:hover .slot-cell { background: rgba(255, 255, 255, 0.025); }
.slot-row:hover .slot-label { background: #253047; }

.cell-lab { background: rgba(16, 185, 129, 0.07); }
.slot-row:hover .cell-lab { background: rgba(16, 185, 129, 0.13) !important; }
.cell-practice { background: rgba(245, 158, 11, 0.07); }
.slot-row:hover .cell-practice { background: rgba(245, 158, 11, 0.13) !important; }

.cell-inner { display: flex; flex-direction: column; gap: 4px; }
.cell-lesson { display:flex; flex-direction:column; align-items:flex-start; gap:4px; }
.cell-lesson + .cell-lesson { margin-top:7px; padding-top:7px; border-top:1px dashed var(--border-strong); }
.cell-subject { font-size: 12px; font-weight: 600; color: var(--text-primary); line-height: 1.35; }
.cell-detail {
  font-size: 10px; color: var(--text-secondary); background: rgba(51, 65, 85, 0.7);
  border-radius: 3px; padding: 1px 5px; display: inline-block; width: fit-content; white-space: nowrap;
}
.cell-room { font-size:10px; color:var(--success); font-weight:600; }
.cell-empty {
  color: var(--text-muted); font-size: 14px; display: flex;
  align-items: center; justify-content: center; min-height: 40px; opacity: 0.3;
}

.center-block { display: flex; flex-direction: column; align-items: center; padding: 80px 20px; }

@media (max-width: 640px) {
  .student-header { gap: 12px; }
  .week-nav { flex-wrap: wrap; }
  .week-label { order: -1; width: 100%; align-items: center; }
  .th-slot { min-width: 68px; }
  .year-grid {
    grid-template-columns: repeat(2, 1fr);
  }
  .group-grid {
    grid-template-columns: 1fr;
  }
  .selector-card {
    padding: 16px;
  }
}
</style>
