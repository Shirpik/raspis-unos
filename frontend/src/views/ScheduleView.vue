<template>
  <div class="page">
    <!-- Header -->
    <div class="page-header">
      <h1 class="page-title">📅 Расписание</h1>
      <div class="header-actions">
        <button class="btn btn-ghost" :disabled="!store.scheduleData || excelExporting" @click="downloadExcel">
          <span v-if="excelExporting" class="spinner spinner-sm" />{{ excelExporting ? 'Собираю Excel…' : 'Excel по образцу' }}
        </button>
        <button class="btn btn-ghost" :disabled="!store.scheduleData" @click="downloadPdf">PDF</button>
        <select v-model="validationSource" class="form-select validation-source" :disabled="validating">
          <option value="auto">Проверить автогенерацию</option>
          <option value="manual">Проверить Конструктор</option>
          <option value="published">Проверить публикацию</option>
        </select>
        <button class="btn btn-secondary" :disabled="validating" @click="runValidation">
          <span v-if="validating" class="spinner spinner-sm" />{{ validating ? 'Проверяю…' : 'Проверить' }}
        </button>
        <button class="btn btn-success" :disabled="!store.scheduleData || publishing" @click="publishSchedule">{{ publishing ? 'Публикую…' : 'Опубликовать' }}</button>
        <select v-if="!demoMode" v-model="lockMode" class="form-select lock-select" :disabled="store.generating">
          <option value="none">С нуля</option>
          <option value="manual">Зафиксировать Конструктор</option>
          <option value="auto">Зафиксировать прошлую автогенерацию</option>
        </select>
        <select v-if="!demoMode" v-model="generationMode" class="form-select generation-mode-select" :disabled="store.generating">
          <option value="weekly">По неделям</option>
          <option value="monolithic">Весь период</option>
        </select>
        <button v-if="!demoMode && !store.generating" class="btn btn-primary" @click="onRegenerate">
          Сгенерировать
        </button>
        <button v-else-if="!demoMode" class="btn btn-danger" @click="onCancel" :disabled="cancelling">
          {{ cancelling ? 'Отменяется…' : 'Отменить' }}
        </button>
      </div>
    </div>

    <div v-if="demoMode" class="notice demo-notice">
      Демонстрационный режим: показано сохранённое тестовое расписание. Запуск решателя на сервере отключён.
    </div>

    <!-- Generation progress panel -->
    <div v-else-if="store.generating && store.progress" class="gen-progress-panel">
      <div class="gen-progress-header">
        <span class="gen-progress-title">
          <span class="spinner spinner-sm" style="color:var(--accent)" />
          Генерация по неделям&ensp;
          <strong>{{ store.progress.solved_weeks }}/{{ store.progress.total_weeks }}</strong>
        </span>
        <span class="gen-progress-time">{{ store.progress.total_elapsed?.toFixed(1) }} с</span>
        <button class="btn btn-danger btn-sm" @click="onCancel" :disabled="cancelling">
          {{ cancelling ? 'Отменяется…' : 'Отменить' }}
        </button>
      </div>

      <!-- Progress bar -->
      <div class="gen-progress-bar-wrap">
        <div
          class="gen-progress-bar-fill"
          :style="{
            width: store.progress.total_weeks > 0
              ? (store.progress.solved_weeks / store.progress.total_weeks * 100) + '%'
              : '0%'
          }"
        />
      </div>

      <!-- Week list -->
      <div class="gen-weeks-list">
        <div
          v-for="w in store.progress.weeks"
          :key="w.num"
          class="gen-week-row"
          :class="`gen-week-${w.status}`"
        >
          <span class="gen-week-icon">
            <template v-if="w.status === 'done'">✓</template>
            <template v-else-if="w.status === 'running'"><span class="spinner spinner-xs" /></template>
            <template v-else-if="w.status === 'failed'">✕</template>
            <template v-else-if="w.status === 'skipped'">—</template>
            <template v-else>·</template>
          </span>
          <span class="gen-week-label">Нед. {{ w.num }}</span>
          <span class="gen-week-dates">{{ w.date_from }}<template v-if="w.date_to"> – {{ w.date_to }}</template></span>
          <span class="gen-week-elapsed" v-if="w.elapsed > 0">{{ w.elapsed.toFixed(1) }} с</span>
        </div>
      </div>
    </div>

    <!-- Generating spinner (no progress yet) -->
    <div v-else-if="store.generating && !store.progress" class="gen-progress-panel gen-progress-init">
      <span class="spinner spinner-sm" style="color:var(--accent)" />
      <span style="margin-left:10px">Запуск генерации…</span>
    </div>

    <ValidationPanel :result="validationResult" />

    <div class="schedule-mode-bar">
      <button class="mode-button" :class="{ active: viewMode === 'groups' }" @click="viewMode = 'groups'">🎓 По группам</button>
      <button class="mode-button" :class="{ active: viewMode === 'teachers' }" @click="viewMode = 'teachers'">👤 По преподавателю</button>
      <div v-if="viewMode === 'teachers'" class="teacher-search-wrap">
        <input
          v-model.trim="teacherSearch"
          class="form-input teacher-search"
          list="schedule-teacher-list"
          placeholder="Введите ФИО преподавателя"
          autocomplete="off"
        />
        <datalist id="schedule-teacher-list">
          <option v-for="teacher in sortedTeachers" :key="teacher.id" :value="teacher.name" />
        </datalist>
        <button v-if="teacherSearch" class="btn btn-ghost btn-sm" type="button" @click="teacherSearch = ''">Очистить</button>
      </div>
    </div>

    <div v-if="viewMode === 'teachers' && teacherSearch && !selectedTeacher && teacherMatches.length" class="teacher-suggestions">
      <button v-for="teacher in teacherMatches.slice(0, 8)" :key="teacher.id" type="button" @click="teacherSearch = teacher.name">
        {{ teacher.name }}
      </button>
    </div>

    <!-- Course year tabs -->
    <div v-if="viewMode === 'groups'" class="year-tabs">
      <button
        v-for="tab in yearTabs"
        :key="tab.value"
        class="year-tab"
        :class="{ active: selectedYear === tab.value }"
        @click="selectedYear = tab.value"
      >
        {{ tab.label }}
        <span v-if="tab.count > 0" class="tab-count">{{ tab.count }}</span>
      </button>
    </div>

    <!-- Loading -->
    <div v-if="store.loading" class="center-block">
      <span class="spinner spinner-lg" style="color: var(--accent)" />
      <p style="margin-top:16px; color: var(--text-muted)">Загрузка расписания…</p>
    </div>

    <!-- Error -->
    <div v-else-if="store.error" class="empty-state">
      <span class="icon">⚠️</span>
      <h3>{{ store.error }}</h3>
      <p>Не удалось получить сохранённое расписание с сервера.</p>
      <div style="margin-top:20px; display:flex; gap:10px; justify-content:center">
        <button class="btn btn-primary" @click="store.fetchSchedule()">Повторить</button>
        <button v-if="!demoMode" class="btn btn-secondary" :disabled="store.generating" @click="onRegenerate">
          {{ store.generating ? 'Генерируется…' : 'Сгенерировать расписание' }}
        </button>
      </div>
    </div>

    <!-- No schedule yet -->
    <div v-else-if="!store.scheduleData || store.groups.length === 0" class="empty-state">
      <span class="icon">📋</span>
      <h3>{{ demoMode ? 'Тестовое расписание не загружено' : 'Расписание не сгенерировано' }}</h3>
      <p>{{ demoMode ? 'Обновите страницу или обратитесь к администратору демонстрации.' : 'Нажмите кнопку «Сгенерировать», чтобы создать расписание' }}</p>
      <button v-if="!demoMode" class="btn btn-primary btn-lg" style="margin-top:20px" :disabled="store.generating" @click="onRegenerate">
        <span v-if="store.generating" class="spinner spinner-sm" />
        {{ store.generating ? 'Генерируется…' : '🚀 Сгенерировать расписание' }}
      </button>
    </div>

    <!-- Schedule table -->
    <template v-else>
      <!-- Week navigation -->
      <div class="week-nav">
        <button class="btn btn-ghost btn-sm" :disabled="weekIndex <= 0" @click="weekIndex--">‹ Пред.</button>
        <span class="week-label">
          <strong>Неделя {{ weekIndex + 1 }}</strong>
          <span class="week-dates">{{ weekLabel }}</span>
        </span>
        <button class="btn btn-ghost btn-sm" :disabled="weekIndex >= sortedWeeks.length - 1" @click="weekIndex++">След. ›</button>
      </div>

      <template v-if="viewMode === 'teachers'">
        <div v-if="!teacherSearch" class="empty-state compact-empty">
          <span class="icon">🔎</span>
          <h3>Найдите преподавателя</h3>
          <p>Введите фамилию, имя или полное ФИО в поле выше.</p>
        </div>
        <div v-else-if="!selectedTeacher" class="empty-state compact-empty">
          <span class="icon">👤</span>
          <h3>Преподаватель не выбран</h3>
          <p>{{ teacherMatches.length ? 'Выберите ФИО из подсказок.' : 'Совпадений в справочнике преподавателей нет.' }}</p>
        </div>
        <div v-else-if="teacherWeekLessonCount === 0" class="empty-state compact-empty">
          <span class="icon">📅</span>
          <h3>У {{ selectedTeacher.name }} нет занятий на этой неделе</h3>
          <p>Переключите неделю кнопками выше.</p>
        </div>
        <template v-else>
          <div class="teacher-result-title">
            <div><span>Расписание преподавателя</span><strong>{{ selectedTeacher.name }}</strong></div>
            <span class="badge badge-success">{{ teacherWeekLessonCount }} пар за неделю</span>
          </div>
          <div class="sched-scroll teacher-schedule-scroll">
            <table class="sched-table teacher-schedule-table">
              <thead>
                <tr>
                  <th class="th-slot sticky-col">
                    <span class="slot-header-icon">🕐</span>
                    <span class="slot-header-text">Пара</span>
                  </th>
                  <th v-for="dateStr in weekDates" :key="dateStr" class="th-day">
                    <span class="day-name">{{ getDayWeekday(dateStr).toUpperCase() }}</span>
                    <span class="day-date-sub">{{ formatDateShort(dateStr) }}</span>
                  </th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="slotNum in displaySlots" :key="slotNum" :class="['slot-row',{'class-hour-row':slotNum===0}]">
                  <td class="slot-label sticky-col">
                    <span class="slot-num">{{ slotNum }}</span>
                    <span class="slot-time">{{ getSlotTimeForSlot(slotNum) }}</span>
                  </td>
                  <td
                    v-for="dateStr in weekDates"
                    :key="`${dateStr}-${slotNum}`"
                    class="slot-cell teacher-slot-cell"
                    :class="teacherCellClass(dateStr, slotNum)"
                  >
                    <div v-if="teacherCell(dateStr, slotNum).length" class="cell-inner">
                      <div v-for="entry in teacherCell(dateStr, slotNum)" :key="entry.key" class="cell-lesson teacher-lesson">
                        <span class="cell-subject">{{ entry.subject }}</span>
                        <span class="teacher-group">{{ entry.groupName }}</span>
                        <span v-if="entry.subgroup" class="cell-detail">{{ entry.subgroup }}</span>
                        <span class="cell-room" :class="{ 'cell-room-missing': !entry.roomName }">
                          {{ entry.roomName ? `каб. ${entry.roomName}` : 'кабинет не назначен' }}<template v-if="entry.campus"> · {{ entry.campus }}</template>
                        </span>
                      </div>
                    </div>
                    <span v-else class="cell-empty">—</span>
                  </td>
                </tr>
              </tbody>
            </table>
          </div>
        </template>
      </template>

      <template v-else>

      <!-- No groups for selected year -->
      <div v-if="filteredGroups.length === 0" class="empty-state">
        <span class="icon">🎓</span>
        <h3>Нет групп для {{ yearTabs.find(t => t.value === selectedYear)?.label }}</h3>
        <p>Попробуйте другой курс или выберите «Все группы»</p>
      </div>

      <!-- No data for week -->
      <div v-else-if="weekDates.length === 0" class="empty-state">
        <span class="icon">📅</span>
        <h3>Нет занятий на этой неделе</h3>
      </div>

      <div v-else class="sched-scroll">
        <table class="sched-table">
          <thead>
            <!-- Row 1: day names -->
            <tr>
              <th rowspan="2" class="th-slot sticky-col">
                <span class="slot-header-icon">🕐</span>
                <span class="slot-header-text">Пара</span>
              </th>
              <th
                v-for="dateStr in weekDates"
                :key="dateStr"
                :colspan="filteredGroups.length"
                class="th-day"
              >
                <span class="day-name">{{ getDayWeekday(dateStr).toUpperCase() }}</span>
                <span class="day-date-sub">{{ formatDateShort(dateStr) }}</span>
              </th>
            </tr>
            <!-- Row 2: group names -->
            <tr>
              <template v-for="dateStr in weekDates" :key="dateStr">
                <th
                  v-for="(g, gi) in filteredGroups"
                  :key="`${dateStr}-${g.group_index}`"
                  class="th-group"
                  :class="{ 'day-separator': gi === 0 }"
                >
                  {{ g.group_name }}
                </th>
              </template>
            </tr>
          </thead>
          <tbody>
            <tr v-for="slotNum in displaySlots" :key="slotNum" :class="['slot-row',{'class-hour-row':slotNum===0}]">
              <td class="slot-label sticky-col">
                <span class="slot-num">{{ slotNum }}</span>
                <span class="slot-time">{{ getSlotTimeForSlot(slotNum) }}</span>
              </td>
              <template v-for="(dateStr, di) in weekDates" :key="dateStr">
                <td
                  v-for="(g, gi) in filteredGroups"
                  :key="`${dateStr}-${g.group_index}`"
                  class="slot-cell"
                  :class="[
                    getCellClass(g.group_index, dateStr, slotNum),
                    { 'day-separator': gi === 0 }
                  ]"
                >
                  <div class="cell-inner" v-if="hasCellContent(g.group_index, dateStr, slotNum)">
                    <template v-if="getCellLessonRows(g.group_index, dateStr, slotNum).length">
                      <div
                        v-for="(lesson, lessonIndex) in getCellLessonRows(g.group_index, dateStr, slotNum)"
                        :key="`${lesson.id}-${lessonIndex}`"
                        class="cell-lesson"
                      >
                        <span class="cell-subject">{{ lesson.subject }}</span>
                        <span v-if="lesson.subgroupLabel" class="cell-detail">{{ lesson.subgroupLabel }}</span>
                        <span v-for="(detail, i) in lesson.details" :key="i" class="cell-detail">{{ detail }}</span>
                        <span
                          class="cell-room"
                          :class="{ 'cell-room-substituted': lesson.substituted, 'cell-room-missing': lesson.missingRoom }"
                          :title="lesson.substitutionReason || ''"
                        >
                          {{ lesson.roomLabel }}<template v-if="lesson.substituted"> · автозамена</template>
                        </span>
                      </div>
                    </template>
                    <template v-else>
                      <span class="cell-subject">{{ parseSubject(getCellText(g.group_index, dateStr, slotNum)) }}</span>
                      <span
                        v-for="(detail, i) in parseDetails(getCellText(g.group_index, dateStr, slotNum))"
                        :key="i"
                        class="cell-detail"
                      >{{ detail }}</span>
                    </template>
                  </div>
                  <span v-else class="cell-empty">—</span>
                </td>
              </template>
            </tr>
          </tbody>
        </table>
      </div>
      </template>
    </template>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, watch } from 'vue'
import { useScheduleStore } from '../stores/schedule.js'
import { useDataStore } from '../stores/data.js'
import { useToast } from '../composables/useToast.js'
import { demoMode } from '../config.js'
import { api } from '../api/index.js'
import { collectSlotNumbers, subgroupLabel } from '../utils/schedulePresentation.js'
import ValidationPanel from '../components/ValidationPanel.vue'

const store = useScheduleStore()
const dataStore = useDataStore()
const toast = useToast()

const selectedYear = ref(0)
const viewMode = ref('groups')
const teacherSearch = ref('')
const weekIndex = ref(0)
const lockMode = ref('none')
const generationMode = ref('weekly')
const publishing = ref(false)
const validating = ref(false)
const excelExporting = ref(false)
const validationSource = ref('auto')
const validationResult = ref(null)

onMounted(async () => {
  await Promise.all([store.fetchSchedule(), dataStore.loadTeachers()])
  jumpToCurrentWeek()
})

async function publishSchedule() {
  publishing.value = true
  const r = await api.schedule.publish()
  publishing.value = false
  if (r.ok) toast.success('Студенческая версия опубликована')
  else toast.error(r.data?.message || 'Ошибка публикации')
}
async function runValidation() {
  validating.value = true
  const r = await api.schedule.validate({ source: validationSource.value })
  validating.value = false
  if (!r.ok) {
    toast.error(r.data?.message || 'Не удалось выполнить проверку')
    return
  }
  validationResult.value = r.data
  if (r.data?.ok) toast.success('Полная проверка пройдена')
  else toast.error(`Найдено нарушений: ${r.data?.summary?.hard_errors ?? 0}`)
}
async function downloadExcel() {
  excelExporting.value = true
  try {
    const { exportScheduleExcel } = await import('../utils/scheduleTemplateExport.js')
    const result = await exportScheduleExcel(store.scheduleData)
    toast.success(`Excel по образцу готов: ${result.insertedLessons} занятий`)
  } catch (error) {
    toast.error(`Не удалось собрать Excel: ${error.message}`)
  } finally {
    excelExporting.value = false
  }
}
async function downloadPdf() { const { exportSchedulePdf } = await import('../utils/scheduleExport.js'); exportSchedulePdf(store.scheduleData) }

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
  const explicit = store.groups.find(g => g.group_name === name)?.course_year
  if (Number(explicit) >= 1 && Number(explicit) <= 4) return Number(explicit)
  const m1 = name.match(/-([1-4])\d/)
  if (m1) {
    const d = parseInt(m1[1])
    if (d >= 1 && d <= 4) return d
  }
  return null
}

// ── Slot lookup ──────────────────────────────────────────────────────────

const slotLookup = computed(() => {
  const map = {}
  for (const g of store.groups) {
    map[g.group_index] = {}
    for (const day of g.days) {
      map[g.group_index][day.date] = { weekday: day.weekday, slots: {} }
      for (const s of day.slots) {
        map[g.group_index][day.date].slots[s.slot] = {
          time: s.time,
          text: s.text,
          lessons: Array.isArray(s.lessons) ? s.lessons : [],
        }
      }
    }
  }
  return map
})

const sortedTeachers = computed(() => [...dataStore.teachers].sort((a, b) => a.name.localeCompare(b.name, 'ru')))

const teacherMatches = computed(() => {
  const query = teacherSearch.value.toLocaleLowerCase('ru').replace(/\s+/g, ' ').trim()
  if (!query) return []
  return sortedTeachers.value.filter(teacher =>
    String(teacher.name || '').toLocaleLowerCase('ru').replace(/\s+/g, ' ').includes(query))
})

const selectedTeacher = computed(() => {
  const query = teacherSearch.value.toLocaleLowerCase('ru').replace(/\s+/g, ' ').trim()
  if (!query) return null
  const exact = sortedTeachers.value.find(teacher =>
    String(teacher.name || '').toLocaleLowerCase('ru').replace(/\s+/g, ' ').trim() === query)
  if (exact) return exact
  return teacherMatches.value.length === 1 ? teacherMatches.value[0] : null
})

const teacherLookup = computed(() => {
  const result = {}
  for (const group of store.groups) {
    for (const day of group.days || []) {
      for (const slot of day.slots || []) {
        const segments = String(slot.text || '').split(' | ')
        for (const [index, lesson] of (slot.lessons || []).entries()) {
          const teacherId = Number(lesson.teacher_id)
          if (!Number.isInteger(teacherId) || teacherId < 0) continue
          const segment = segments[index] || ''
          const separator = segment.indexOf(' — ')
          const details = separator >= 0 ? segment.slice(separator + 3).split(', ').filter(Boolean) : []
          const campus = normalizedCampus(details.at(-1))
          const entry = {
            key: `${group.group_index}-${day.date}-${slot.slot}-${lesson.uid || lesson.id}-${index}`,
            subject: lesson.name || (separator >= 0 ? segment.slice(0, separator) : segment),
            groupName: group.group_name,
            subgroup: subgroupLabel(lesson.subgroup, group.group_index),
            roomName: lesson.room_name === null || lesson.room_name === undefined ? '' : String(lesson.room_name).trim(),
            campus,
          }
          result[teacherId] ||= {}
          result[teacherId][day.date] ||= {}
          result[teacherId][day.date][slot.slot] ||= []
          result[teacherId][day.date][slot.slot].push(entry)
        }
      }
    }
  }
  return result
})

function teacherCell(dateStr, slotNum) {
  if (!selectedTeacher.value) return []
  return teacherLookup.value[Number(selectedTeacher.value.id)]?.[dateStr]?.[slotNum] || []
}

const teacherWeekLessonCount = computed(() => {
  if (!selectedTeacher.value) return 0
  let count = 0
  for (const date of weekDates.value) {
    const slots = teacherLookup.value[Number(selectedTeacher.value.id)]?.[date] || {}
    for (const entries of Object.values(slots)) count += entries.length
  }
  return count
})

function teacherCellClass(dateStr, slotNum) {
  const names = teacherCell(dateStr, slotNum).map(entry => entry.subject).join(' ').toLocaleLowerCase('ru')
  if (!names) return 'cell-empty-type'
  if (names.includes('лпз') || names.includes('лаб')) return 'cell-lab'
  if (names.includes('практик')) return 'cell-practice'
  return 'cell-normal'
}

// ── Weeks ────────────────────────────────────────────────────────────────

const sortedWeeks = computed(() => {
  if (!store.scheduleData) return []
  const weeksSet = new Set()
  for (const g of store.groups) {
    for (const d of g.days) {
      const mon = getMonday(parseDMY(d.date))
      weeksSet.add(mon.toISOString())
    }
  }
  return [...weeksSet].sort().map(iso => {
    const mon = new Date(iso)
    const sun = new Date(mon); sun.setDate(sun.getDate() + 6)
    return { iso, mon, sun, monStr: fmtDate(mon), sunStr: fmtDate(sun) }
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
  const set = new Set()
  for (const g of store.groups) {
    for (const d of g.days) {
      const parsed = parseDMY(d.date)
      const mon = getMonday(parsed)
      if (mon.toISOString() === w.iso) set.add(d.date)
    }
  }
  return [...set].sort(compareDMY)
})

function getDayWeekday(dateStr) {
  for (const g of store.groups) {
    const lookup = slotLookup.value[g.group_index]?.[dateStr]
    if (lookup) return lookup.weekday
  }
  return ''
}

function jumpToCurrentWeek() {
  if (!sortedWeeks.value.length) return
  const today = new Date()
  const mon = getMonday(today).toISOString()
  const idx = sortedWeeks.value.findIndex(w => w.iso === mon)
  weekIndex.value = idx >= 0 ? idx : 0
}

watch(() => store.scheduleData, jumpToCurrentWeek)

// ── Max slots ────────────────────────────────────────────────────────────

const displaySlots = computed(() => {
  return collectSlotNumbers(store.groups)
})

// ── Year tabs ────────────────────────────────────────────────────────────

const yearTabs = computed(() => {
  const counts = [0, 0, 0, 0]
  for (const g of store.groups) {
    const y = detectCourseYear(g.group_name)
    if (y && y >= 1 && y <= 4) counts[y - 1]++
  }
  const total = store.groups.length
  return [
    { value: 0, label: 'Все группы', count: total },
    { value: 1, label: '1 курс', count: counts[0] },
    { value: 2, label: '2 курс', count: counts[1] },
    { value: 3, label: '3 курс', count: counts[2] },
    { value: 4, label: '4 курс', count: counts[3] },
  ]
})

const filteredGroups = computed(() => {
  if (selectedYear.value === 0) return store.groups
  return store.groups.filter(g => detectCourseYear(g.group_name) === selectedYear.value)
})

// ── Cell helpers ─────────────────────────────────────────────────────────

function getSlotTimeForSlot(slotNum) {
  const found = new Map()
  for (const dateStr of weekDates.value) {
    for (const g of store.groups) {
      const s = slotLookup.value[g.group_index]?.[dateStr]?.slots[slotNum]
      if (s?.time) {
        const day = slotLookup.value[g.group_index][dateStr].weekday
        if (!found.has(s.time)) found.set(s.time, day)
        break
      }
    }
  }
  if (found.size === 1) return [...found.keys()][0]
  return [...found].map(([time, day]) => `${day}: ${time}`).join(' / ')
}

function getCellText(groupIndex, dateStr, slotNum) {
  const s = slotLookup.value[groupIndex]?.[dateStr]?.slots[slotNum]
  if (!s || s.text === '-') return null
  return s.text
}

function hasCellContent(groupIndex, dateStr, slotNum) {
  return getCellLessonRows(groupIndex, dateStr, slotNum).length > 0 || Boolean(getCellText(groupIndex, dateStr, slotNum))
}

function normalizedCampus(value) {
  if (/кривоус/i.test(value || '')) return 'Кривоусова, 53'
  if (/лесн/i.test(value || '')) return 'Лесная'
  return ''
}

function getCellLessonRows(groupIndex, dateStr, slotNum) {
  const cell = slotLookup.value[groupIndex]?.[dateStr]?.slots[slotNum]
  if (!cell?.lessons?.length) return []
  const segments = String(cell.text || '').split(' | ')

  return cell.lessons.map((lesson, index) => {
    const segment = segments[index] || ''
    const separator = segment.indexOf(' — ')
    const rawDetails = separator >= 0
      ? segment.slice(separator + 3).split(', ').filter(Boolean)
      : []
    const campus = normalizedCampus(rawDetails.at(-1))
    const detailsWithoutCampus = campus ? rawDetails.slice(0, -1) : rawDetails
    const details = detailsWithoutCampus.filter(detail =>
      !/^(вся группа|[12]-?я?\s*(подгруппа|п\/?г)|подгруппа)/i.test(detail))
    const hasRoom = lesson.room_name !== null && lesson.room_name !== undefined && String(lesson.room_name).trim() !== ''
    const room = hasRoom ? `каб. ${lesson.room_name}` : 'кабинет не назначен'

    return {
      id: lesson.uid || lesson.id,
      subject: lesson.name || (separator >= 0 ? segment.slice(0, separator) : segment),
      subgroupLabel: subgroupLabel(lesson.subgroup, groupIndex),
      details,
      roomLabel: campus ? `${room} · ${campus}` : room,
      missingRoom: !hasRoom,
      substituted: lesson.room_substituted === true,
      substitutionReason: lesson.room_substitution_reason,
    }
  })
}

function getCellClass(groupIndex, dateStr, slotNum) {
  const text = getCellText(groupIndex, dateStr, slotNum)
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

// ── Regenerate & Cancel ───────────────────────────────────────────────────

const cancelling = ref(false)

async function onRegenerate() {
  const opts = { mode: generationMode.value }
  if (lockMode.value && lockMode.value !== 'none') opts.lock_existing = lockMode.value
  cancelling.value = false
  const r = await store.regenerate(opts)
  if (r.async) {
    // Async weekly — ждём через polling; toast покажем по завершению
    return
  }
  if (r.ok) {
    toast.success(r.message || 'Расписание успешно сгенерировано!')
    jumpToCurrentWeek()
  } else {
    toast.error(r.message || 'Ошибка генерации расписания')
  }
}

async function onCancel() {
  cancelling.value = true
  await store.cancelGeneration()
}

// Показываем toast когда async-генерация завершается
watch(() => store.progress?.state, (newState, oldState) => {
  if (!oldState || oldState === newState) return
  if (newState === 'done') {
    toast.success('Расписание по неделям сгенерировано!')
    jumpToCurrentWeek()
    cancelling.value = false
    validationSource.value = 'auto'
    runValidation()
  } else if (newState === 'failed') {
    const msg = store.progress?.message || 'Ошибка генерации'
    toast.error(msg)
    cancelling.value = false
  } else if (newState === 'cancelled') {
    toast.info('Генерация отменена. Частичное расписание сохранено.')
    cancelling.value = false
    store.fetchSchedule()
  }
})
</script>

<style scoped>
.header-actions { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }
.lock-select { width: auto; min-width: 220px; padding: 7px 10px; font-size: 13px; }
.validation-source { width: auto; min-width: 205px; padding: 7px 10px; font-size: 13px; }
.generation-mode-select { width: auto; min-width: 145px; padding: 7px 10px; font-size: 13px; }
.schedule-mode-bar {
  display: flex; align-items: center; gap: 8px; flex-wrap: wrap;
  padding: 10px; margin-bottom: 12px; border: 1px solid var(--border);
  border-radius: var(--radius); background: var(--bg-secondary);
}
.mode-button {
  border: 1px solid var(--border); border-radius: var(--radius-sm); padding: 8px 13px;
  background: var(--bg-tertiary); color: var(--text-secondary); font: inherit;
  font-size: 13px; font-weight: 600; cursor: pointer;
}
.mode-button:hover { border-color: var(--accent); color: var(--text-primary); }
.mode-button.active { border-color: var(--accent); color: var(--accent); background: var(--accent-light); }
.teacher-search-wrap { display: flex; align-items: center; gap: 8px; flex: 1; min-width: min(100%, 320px); }
.teacher-search { min-width: 240px; flex: 1; }
.teacher-suggestions { display: flex; gap: 7px; flex-wrap: wrap; margin: -4px 0 16px; }
.teacher-suggestions button {
  border: 1px solid var(--border); border-radius: 999px; padding: 5px 10px;
  background: var(--bg-secondary); color: var(--text-secondary); cursor: pointer; font-size: 12px;
}
.teacher-suggestions button:hover { border-color: var(--accent); color: var(--accent); }
.teacher-result-title {
  display: flex; justify-content: space-between; align-items: center; gap: 14px;
  margin-bottom: 12px; padding: 12px 14px; border: 1px solid var(--border);
  border-radius: var(--radius); background: var(--bg-secondary);
}
.teacher-result-title div { display: flex; flex-direction: column; gap: 3px; }
.teacher-result-title div > span { color: var(--text-muted); font-size: 11px; text-transform: uppercase; letter-spacing: .06em; }
.teacher-result-title strong { font-size: 16px; }
.teacher-schedule-table { min-width: 900px; table-layout: fixed; }
.teacher-schedule-table .th-day { width: calc((100% - 84px) / 6); }
.teacher-slot-cell { min-width: 150px; vertical-align: top; }
.teacher-group { display: block; color: var(--accent); font-size: 12px; font-weight: 700; margin-top: 3px; }
.teacher-lesson + .teacher-lesson { border-top: 1px solid var(--border-strong); padding-top: 7px; margin-top: 7px; }
.compact-empty { min-height: 260px; }

/* ── Year tabs ─────────────────────────────────────────────────────────── */
.year-tabs {
  display: flex;
  gap: 6px;
  margin-bottom: 20px;
  flex-wrap: wrap;
}
.year-tab {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 7px 14px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border);
  background: var(--bg-secondary);
  color: var(--text-secondary);
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
  transition: all var(--transition);
}
.year-tab:hover { border-color: var(--accent); color: var(--text-primary); }
.year-tab.active {
  background: var(--accent-light);
  border-color: var(--accent);
  color: var(--accent);
}
.tab-count {
  background: var(--bg-tertiary);
  border-radius: 10px;
  padding: 0 6px;
  font-size: 11px;
}

/* ── Week nav ──────────────────────────────────────────────────────────── */
.week-nav {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 20px;
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 12px 16px;
}
.week-label {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
}
.week-label strong { font-size: 14px; }
.week-dates { font-size: 12px; color: var(--text-muted); }

/* ── Table scroll wrapper ──────────────────────────────────────────────── */
.sched-scroll {
  overflow-x: auto;
  -webkit-overflow-scrolling: touch;
  border-radius: var(--radius);
  border: 1px solid var(--border-strong);
  box-shadow: 0 4px 24px rgba(0, 0, 0, 0.35);
}

/* ── Table base ─────────────────────────────────────────────────────────  */
.sched-table {
  border-collapse: collapse;
  font-size: 13px;
  width: 100%;
  table-layout: auto;
}

/* ── Sticky first column ─────────────────────────────────────────────── */
.sticky-col {
  position: sticky;
  left: 0;
  z-index: 2;
}

/* ── Header: slot cell (top-left, rowspan=2) ────────────────────────── */
.th-slot {
  background: var(--bg-secondary);
  border-right: 2px solid var(--border-strong);
  border-bottom: 2px solid var(--border-strong);
  z-index: 4 !important;
  text-align: center;
  width: 84px;
  min-width: 76px;
  padding: 10px 8px;
}
.slot-header-icon {
  display: block;
  font-size: 18px;
  line-height: 1;
  margin-bottom: 4px;
}
.slot-header-text {
  display: block;
  font-size: 10px;
  font-weight: 700;
  color: var(--text-secondary);
  text-transform: uppercase;
  letter-spacing: 0.08em;
}

/* ── Header: day columns ─────────────────────────────────────────────── */
.th-day {
  background: linear-gradient(135deg, #4f46e5 0%, #6366f1 60%, #818cf8 100%);
  color: #fff;
  text-align: center;
  padding: 10px 14px;
  border-left: 2px solid rgba(255, 255, 255, 0.18);
  border-bottom: 1px solid rgba(255, 255, 255, 0.18);
  white-space: nowrap;
}
.th-day:first-child { border-left: none; }
.day-name {
  display: block;
  font-size: 12px;
  font-weight: 800;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}
.day-date-sub {
  display: block;
  font-size: 10px;
  font-weight: 400;
  opacity: 0.72;
  margin-top: 3px;
  letter-spacing: 0.04em;
}

/* ── Header: group sub-row ───────────────────────────────────────────── */
.th-group {
  background: #1a2540;
  color: var(--text-secondary);
  text-align: center;
  font-size: 10px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  padding: 6px 10px;
  white-space: nowrap;
  border-bottom: 2px solid var(--border-strong);
  min-width: 140px;
}
.th-group.day-separator {
  border-left: 2px solid var(--border-strong);
}

/* ── Slot label (first column in body) ──────────────────────────────── */
.slot-label {
  background: var(--bg-secondary);
  padding: 10px 8px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 4px;
  border-right: 2px solid var(--border-strong);
  border-top: 1px solid var(--border);
  min-height: 56px;
  transition: background var(--transition);
}
.slot-num {
  font-size: 20px;
  font-weight: 800;
  color: var(--text-primary);
  line-height: 1;
}
.slot-time {
  font-size: 9px;
  color: var(--text-muted);
  white-space: nowrap;
  letter-spacing: 0.03em;
  text-align: center;
}

/* ── Data cells ──────────────────────────────────────────────────────── */
.slot-cell {
  padding: 8px 10px;
  vertical-align: top;
  border-top: 1px solid var(--border);
  transition: background var(--transition);
}

.slot-cell.day-separator {
  border-left: 2px solid var(--border-strong);
}

.slot-row:hover .slot-cell {
  background: rgba(255, 255, 255, 0.025);
}
.slot-row:hover .slot-label {
  background: #253047;
}

/* Lesson type backgrounds */
.cell-lab {
  background: rgba(16, 185, 129, 0.07);
}
.slot-row:hover .cell-lab {
  background: rgba(16, 185, 129, 0.13) !important;
}
.cell-practice {
  background: rgba(245, 158, 11, 0.07);
}
.slot-row:hover .cell-practice {
  background: rgba(245, 158, 11, 0.13) !important;
}

/* Cell content */
.cell-inner {
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.cell-lesson {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 4px;
}
.cell-lesson + .cell-lesson {
  margin-top: 7px;
  padding-top: 7px;
  border-top: 1px dashed var(--border-strong);
}
.cell-subject {
  font-size: 12px;
  font-weight: 600;
  color: var(--text-primary);
  line-height: 1.35;
}
.cell-detail {
  font-size: 10px;
  color: var(--text-secondary);
  background: rgba(51, 65, 85, 0.7);
  border-radius: 3px;
  padding: 1px 5px;
  display: inline-block;
  width: fit-content;
  white-space: nowrap;
}
.cell-room {
  display: inline-flex;
  width: fit-content;
  max-width: 100%;
  padding: 3px 7px;
  border-radius: 4px;
  background: rgba(59, 130, 246, 0.18);
  border: 1px solid rgba(96, 165, 250, 0.38);
  color: #bfdbfe;
  font-size: 10px;
  font-weight: 800;
  line-height: 1.25;
  white-space: normal;
}
.cell-room-substituted {
  background: rgba(245, 158, 11, 0.16);
  border-color: rgba(245, 158, 11, 0.4);
  color: #fde68a;
}
.cell-room-missing {
  background: rgba(239, 68, 68, 0.15);
  border-color: rgba(248, 113, 113, 0.4);
  color: #fecaca;
}
.cell-empty {
  color: var(--text-muted);
  font-size: 14px;
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 40px;
  opacity: 0.3;
}

/* Loading center */
.center-block {
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 80px 20px;
}

@media (max-width: 640px) {
  .week-nav { flex-wrap: wrap; }
  .week-label { order: -1; width: 100%; align-items: center; }
  .th-slot { min-width: 68px; }
  .th-group { min-width: 110px; }
}

/* ── Generation progress panel ──────────────────────────────────────────── */
.gen-progress-panel {
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 16px 18px;
  margin-bottom: 18px;
}
.gen-progress-init {
  display: flex;
  align-items: center;
  color: var(--text-muted);
  font-size: 14px;
}
.gen-progress-header {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 10px;
}
.gen-progress-title {
  flex: 1;
  font-size: 14px;
  font-weight: 500;
  display: flex;
  align-items: center;
  gap: 8px;
}
.gen-progress-time {
  font-size: 13px;
  color: var(--text-muted);
  min-width: 48px;
  text-align: right;
}
.gen-progress-bar-wrap {
  height: 6px;
  background: var(--border);
  border-radius: 3px;
  margin-bottom: 12px;
  overflow: hidden;
}
.gen-progress-bar-fill {
  height: 100%;
  background: var(--accent);
  border-radius: 3px;
  transition: width 0.5s ease;
}
.gen-weeks-list {
  display: flex;
  flex-wrap: wrap;
  gap: 4px 8px;
  max-height: 180px;
  overflow-y: auto;
}
.gen-week-row {
  display: flex;
  align-items: center;
  gap: 5px;
  font-size: 12px;
  padding: 3px 8px;
  border-radius: 4px;
  background: var(--bg-primary);
  border: 1px solid var(--border);
  min-width: 190px;
}
.gen-week-icon { font-size: 13px; width: 14px; text-align: center; }
.gen-week-label { font-weight: 600; color: var(--text-secondary); min-width: 46px; }
.gen-week-dates { color: var(--text-muted); flex: 1; }
.gen-week-elapsed { color: var(--text-muted); font-size: 11px; }

.gen-week-done   { border-color: var(--success, #2da44e); background: color-mix(in srgb, var(--success, #2da44e) 8%, var(--bg-primary)); }
.gen-week-done .gen-week-icon { color: var(--success, #2da44e); }
.gen-week-running { border-color: var(--accent); background: color-mix(in srgb, var(--accent) 8%, var(--bg-primary)); }
.gen-week-failed  { border-color: var(--error, #cf222e); background: color-mix(in srgb, var(--error, #cf222e) 8%, var(--bg-primary)); }
.gen-week-failed .gen-week-icon { color: var(--error, #cf222e); }
.gen-week-skipped { opacity: 0.5; }
.gen-week-pending { opacity: 0.45; }

.spinner-xs {
  display: inline-block;
  width: 10px;
  height: 10px;
  border: 2px solid transparent;
  border-top-color: currentColor;
  border-radius: 50%;
  animation: spin 0.7s linear infinite;
}
</style>
