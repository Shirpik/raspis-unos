<template>
  <div class="page">
    <div class="page-header">
      <h1 class="page-title">
        <FileEdit :size="24" style="margin-right: 8px" />
        Конструктор расписания
      </h1>
      <div class="header-actions">
        <input ref="scheduleImportInput" class="visually-hidden" type="file" accept=".xlsx,.xls" @change="onImportSchedule" />
        <button class="btn btn-secondary btn-sm" :disabled="cstore.loading || cstore.saving || importingSchedule" @click="scheduleImportInput?.click()">
          <span v-if="importingSchedule" class="spinner spinner-sm"/>
          <Upload v-else :size="16" />
          {{ importingSchedule ? 'Проверяю Excel…' : 'Восстановить неделю из Excel' }}
        </button>
        <button class="btn btn-secondary btn-sm" :disabled="cstore.saving" @click="onCopyFromAuto">
          <span v-if="cstore.saving" class="spinner spinner-sm"/>
          <Download v-else :size="16" />
          Скопировать из автогенерации
        </button>
        <button class="btn btn-ghost btn-sm" :disabled="cstore.saving" @click="onClear">
          <Trash2 :size="16" />
          Очистить
        </button>
        <button class="btn btn-secondary btn-sm" :disabled="validating || !cstore.manualData" @click="onValidate">
          <span v-if="validating" class="spinner spinner-sm" />
          <CheckCircle v-else :size="16" />
          {{ validating ? 'Проверяю…' : 'Проверить' }}
        </button>
        <button class="btn btn-primary btn-sm" :disabled="cstore.saving || !cstore.dirty" @click="onSave">
          <span v-if="cstore.saving" class="spinner spinner-sm"/>
          <Save v-else :size="16" />
          Сохранить
          <span v-if="cstore.dirty" class="badge badge-warning" style="margin-left:6px">●</span>
        </button>
        <button class="btn btn-success btn-sm" :disabled="cstore.saving || generating || rangePlacedLessonCount === 0" @click="onSaveAndGenerate">
          <span v-if="generating" class="spinner spinner-sm"/>
          <Sparkles v-else :size="16" />
          {{ generating ? 'Запускаю…' : 'Сохранить и достроить выбранные даты' }}
        </button>
      </div>
    </div>

    <section class="generation-range-card">
      <div class="generation-range-copy">
        <strong>Обязательные пары и достройка</strong>
        <span>Поставьте нужные пары в таблице. Решатель зафиксирует их и заполнит только выбранные даты; прежнее расписание вне диапазона сохранится.</span>
        <span><strong>УП:</strong> одно размещение = 6 часов, за день можно поставить 12 часов. Если преподаватель один — ставьте утро с 1-й пары и вторую УП с 3-й; разные преподаватели могут вести подгруппы параллельно.</span>
      </div>
      <div class="generation-range-controls">
        <label><span>С даты</span><input v-model="generationFrom" type="date" class="form-input" /></label>
        <label><span>По дату</span><input v-model="generationTo" type="date" class="form-input" /></label>
        <label class="teacher-filter-field">
          <span>Показывать занятия преподавателя</span>
          <select v-model.number="teacherFilter" class="form-select">
            <option :value="-1">Все преподаватели</option>
            <option v-for="teacher in sortedTeachers" :key="teacher.id" :value="teacher.id">{{ teacher.name }}</option>
          </select>
        </label>
        <div class="range-lock-count" :class="{ empty: rangePlacedLessonCount === 0 }">
          Закреплено в диапазоне: <strong>{{ rangePlacedLessonCount }}</strong>
        </div>
      </div>
      <div v-if="selectedTeacherUpHours" class="teacher-hours-summary">
        <strong>{{ selectedTeacherUpHours.name }} · УП, 1 семестр</strong>
        <span>План: {{ selectedTeacherUpHours.plan }} ч</span>
        <span>Проведено: {{ selectedTeacherUpHours.confirmed }} ч</span>
        <span>В Конструкторе: {{ selectedTeacherUpHours.manual }} ч</span>
        <span class="teacher-hours-remaining">Осталось: {{ selectedTeacherUpHours.remaining }} ч</span>
      </div>
      <p v-if="rangeError" class="generation-range-error">{{ rangeError }}</p>
    </section>

    <div v-if="importReport" class="import-report" :class="importReport.ok ? 'import-report-ok' : 'import-report-error'">
      <strong>{{ importReport.ok ? 'Неделя восстановлена' : 'Excel не импортирован' }}</strong>
      <span v-if="importReport.ok">{{ importReport.dates[0] }} — {{ importReport.dates.at(-1) }}: {{ importReport.imported }} занятий, {{ importReport.groupCount }} групп.</span>
      <ul v-if="importReport.errors?.length"><li v-for="message in importReport.errors.slice(0, 8)" :key="message">{{ message }}</li></ul>
      <ul v-if="importReport.warnings?.length"><li v-for="message in importReport.warnings.slice(0, 8)" :key="message">{{ message }}</li></ul>
    </div>

    <ValidationPanel :result="validationResult" />

    <div v-if="cstore.loading" class="center-block">
      <span class="spinner spinner-lg" style="color: var(--accent)" />
      <p style="margin-top:16px; color: var(--text-muted)">Загрузка ручного расписания…</p>
    </div>

    <div v-else-if="cstore.error" class="empty-state">
      <AlertCircle :size="48" style="color: var(--error)" />
      <h3>{{ cstore.error }}</h3>
    </div>

    <div v-else-if="groups.length === 0" class="empty-state">
      <FileEdit :size="48" style="color: var(--text-muted)" />
      <h3>Ручное расписание пусто</h3>
      <p>Скопируй из автогенерации, чтобы начать редактирование, или построй с нуля кликами по ячейкам.</p>
      <div style="margin-top:20px; display:flex; gap:10px; justify-content:center">
        <button class="btn btn-primary" @click="onCopyFromAuto">
          <Download :size="18" />
          Скопировать из автогенерации
        </button>
        <button class="btn btn-secondary" @click="initScratch">
          <Copy :size="18" />
          Начать с нуля
        </button>
      </div>
    </div>

    <template v-else>
      <!-- Year tabs -->
      <div class="year-tabs">
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

      <!-- Week nav -->
      <div class="week-nav">
        <button class="btn btn-ghost btn-sm" :disabled="weekIndex <= 0" @click="weekIndex--">‹ Пред.</button>
        <span class="week-label">
          <strong>Неделя {{ weekIndex + 1 }}</strong>
          <span class="week-dates">{{ weekLabel }}</span>
        </span>
        <button class="btn btn-ghost btn-sm" :disabled="weekIndex >= sortedWeeks.length - 1" @click="weekIndex++">След. ›</button>
      </div>

      <div v-if="filteredGroups.length === 0" class="empty-state">
        <GraduationCap :size="48" style="opacity: 0.3" />
        <h3>Нет групп для выбранного курса</h3>
      </div>

      <div v-else-if="weekDates.length === 0" class="empty-state">
        <CalendarX :size="48" style="opacity: 0.3" />
        <h3>Нет учебных дней на этой неделе</h3>
      </div>

      <div v-else class="sched-scroll">
        <table class="sched-table">
          <thead>
            <tr>
              <th rowspan="2" class="th-slot sticky-col">
                <Clock :size="18" />
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
                  class="slot-cell editable"
                  :class="{
                    'day-separator': gi === 0,
                    'cell-lab': cellIsLab(g.group_index, dateStr, slotNum),
                    'cell-practice': cellIsBlock(g.group_index, dateStr, slotNum),
                  }"
                  @click="slotNum === 0 ? null : openPicker(g, dateStr, slotNum)"
                >
                  <div class="cell-inner" v-if="cellLessons(g.group_index, dateStr, slotNum).length">
                    <div
                      v-for="(lesson, lessonIndex) in cellLessons(g.group_index, dateStr, slotNum)"
                      :key="`${lesson.uid || lesson.id}-${lessonIndex}`"
                      class="cell-lesson"
                    >
                      <span class="cell-subject">
                        <b v-if="lessonSourceIndex(lesson)" class="subject-index">{{ lessonSourceIndex(lesson) }}</b>
                        {{ lesson.name }}
                      </span>
                      <span class="cell-detail">{{ subgroupLabel(lesson.subgroup) }}</span>
                    </div>
                  </div>
                  <span v-else class="cell-empty">+</span>
                </td>
              </template>
            </tr>
          </tbody>
        </table>
      </div>

      <!-- Прогресс уроков -->
      <section class="progress-card">
        <div class="card-title">
          <BarChart3 :size="20" />
          <span>Прогресс расстановки уроков</span>
        </div>
        <p class="progress-help">
          По каждому занятию показаны часы первого семестра: <strong>план, уже проведено, поставлено в Конструкторе и остаток</strong>.
          После добавления пары остаток пересчитывается сразу; для УП одно размещение вычитает 6 часов.
        </p>
        <div class="progress-legend" aria-label="Обозначения прогресса">
          <span><i class="legend-dot progress-none-dot" />Ещё не ставили</span>
          <span><i class="legend-dot progress-partial-dot" />Часть закреплена</span>
          <span><i class="legend-dot progress-complete-dot" />План закреплён полностью</span>
          <span><i class="legend-dot progress-over-dot" />Поставлено больше текущего плана</span>
        </div>
        <div class="progress-grid">
          <div v-for="lp in lessonProgress" :key="lp.id" class="progress-item" :class="{
            'progress-partial': lp.placed > 0 && lp.placed < lp.total_slots,
            'progress-complete': lp.total_slots > 0 && lp.placed === lp.total_slots,
            'progress-over': lp.placed > lp.total_slots,
          }">
            <div class="progress-name">
              <span class="progress-group">{{ groupNameById(lp.group) }}</span>
              <span><b v-if="lp.source_index" class="subject-index">{{ lp.source_index }}</b> {{ lp.name }}</span>
              <small>
                <template v-if="lp.total_slots === 0 && lp.placed > 0">не входит в текущий план · </template>
                {{ subgroupLabel(lp.subgroup) }} · {{ teacherNameById(lp.teacher) }}
              </small>
            </div>
            <div class="progress-counts" :title="`${lp.placed} закреплено вручную, ${lp.total_slots} требуется на период`">
              <span>План {{ lp.planHours }} ч</span>
              <span>Проведено {{ lp.confirmedHours }} ч</span>
              <span>В Конструкторе {{ lp.manualHours }} ч</span>
              <strong>Осталось {{ lp.remainingHours }} ч</strong>
            </div>
          </div>
        </div>
      </section>
    </template>

    <!-- Lesson picker modal -->
    <Modal v-model="pickerOpen" :title="pickerTitle">
      <div v-if="pickerCell" class="picker-body">
        <div v-if="pickerCell.lessons.length" class="picker-current">
          <div style="font-weight:600;margin-bottom:8px">Сейчас в ячейке:</div>
          <div v-for="L in pickerCell.lessons" :key="L.id" class="picker-current-item">
            <div><b v-if="lessonSourceIndex(L)" class="subject-index">{{ lessonSourceIndex(L) }}</b> {{ L.name }} <span class="muted">— {{ subgroupLabel(L.subgroup) }}, {{ teacherNameById(L.teacher_id) }}</span></div>
            <button class="btn btn-ghost btn-sm" style="color:var(--error)" @click="removeLessonFromCell(L.id)">Удалить</button>
          </div>
          <hr style="margin:14px 0;border-color:var(--border)"/>
        </div>

        <div style="font-weight:600;margin-bottom:8px">Доступные уроки группы {{ pickerCell.groupName }}:</div>
        <div v-if="pickerOptions.length === 0" style="color:var(--text-muted);padding:14px 0">
          У группы нет предметов, подходящих под выбранный фильтр преподавателя.
        </div>
        <div v-else class="picker-list">
          <div
            v-for="opt in pickerOptions" :key="opt.id"
            class="picker-option"
            :class="{ 'picker-warn': opt.conflict, 'picker-blocked': opt.blocking }"
            @click="addLessonToCell(opt)"
          >
            <div class="picker-option-main">
              <span class="picker-option-name">
                <b v-if="opt.source_index" class="subject-index">{{ opt.source_index }}</b>
                {{ opt.name }}
              </span>
              <span class="picker-option-meta">
                {{ subgroupLabel(opt.subgroup) }} ·
                {{ teacherNameById(opt.teacher) }} ·
                план {{ opt.planHours }} ч · проведено {{ opt.confirmedHours }} ч ·
                в Конструкторе {{ opt.manualHours }} ч · осталось {{ opt.remainingHours }} ч
                <span v-if="opt.is_block"> · УП</span>
                <span v-if="opt.is_lab"> · ЛПЗ</span>
              </span>
            </div>
            <div v-if="opt.conflict" class="picker-option-warn">
              ⚠ {{ opt.conflict }}<template v-if="!opt.blocking"> · можно закрепить вручную</template>
            </div>
          </div>
        </div>
      </div>
      <template #footer>
        <button class="btn btn-ghost" @click="pickerOpen = false">Закрыть</button>
      </template>
    </Modal>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, watch } from 'vue'
import { useRouter } from 'vue-router'
import Modal from '../components/Modal.vue'
import { useConstructorStore } from '../stores/constructor.js'
import { useDataStore } from '../stores/data.js'
import { useScheduleStore } from '../stores/schedule.js'
import { useToast } from '../composables/useToast.js'
import { api } from '../api/index.js'
import ValidationPanel from '../components/ValidationPanel.vue'
import { AlertCircle, Download, Upload, Trash2, CheckCircle, Save, FileEdit, Copy, Clock, CalendarX, GraduationCap, BarChart3, Sparkles } from 'lucide-vue-next'

const cstore = useConstructorStore()
const data = useDataStore()
const scheduleStore = useScheduleStore()
const router = useRouter()
const toast = useToast()

const selectedYear = ref(0)
const weekIndex = ref(0)
const validating = ref(false)
const generating = ref(false)
const validationResult = ref(null)
const scheduleImportInput = ref(null)
const importingSchedule = ref(false)
const importReport = ref(null)
const generationFrom = ref('')
const generationTo = ref('')
const teacherFilter = ref(-1)
const teachingLedger = ref([])

onMounted(async () => {
  const [, , contextResponse] = await Promise.all([
    cstore.load(),
    data.loadAll(),
    api.schedule.context(),
  ])
  teachingLedger.value = contextResponse.ok && Array.isArray(contextResponse.data?.teaching_ledger)
    ? contextResponse.data.teaching_ledger
    : []
  ensureConstructorGroups()
  setDefaultGenerationRange()
  jumpToGenerationWeek()
})

// ── Computed shape ──
const groups = computed(() => cstore.manualData?.groups || [])

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
function compareDMY(a, b) { return parseDMY(a) - parseDMY(b) }
function isoDateValue(date) {
  return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}-${String(date.getDate()).padStart(2, '0')}`
}
function displayFromIso(iso) {
  const [year, month, day] = iso.split('-')
  return `${day}.${month}.${year}`
}
function ensureConstructorGroups() {
  if (!cstore.manualData || cstore.error) return
  for (const group of data.groups) cstore.ensureGroup(group.id, group.name)
}
function setDefaultGenerationRange() {
  const now = new Date()
  now.setHours(12, 0, 0, 0)
  let from = addDays(now, 1)
  if (from.getDay() === 0) from = addDays(from, 1)
  const saturdayOffset = (6 - from.getDay() + 7) % 7
  let to = addDays(from, saturdayOffset)
  const semesterStart = data.settings?.semester_start_date || data.settings?.start_date
  const semesterEnd = data.settings?.first_course_semester_end_date || data.settings?.semester_end_date || data.settings?.end_date
  if (semesterStart && isoDateValue(from) < semesterStart) from = new Date(`${semesterStart}T12:00:00`)
  if (semesterEnd && isoDateValue(to) > semesterEnd) to = new Date(`${semesterEnd}T12:00:00`)
  generationFrom.value = isoDateValue(from)
  generationTo.value = isoDateValue(to)
}
function jumpToGenerationWeek() {
  if (!generationFrom.value) return
  const target = getMonday(new Date(`${generationFrom.value}T12:00:00`)).toISOString()
  const index = sortedWeeks.value.findIndex(week => week.iso === target)
  if (index >= 0) weekIndex.value = index
}
watch(generationFrom, jumpToGenerationWeek)
function detectCourseYear(name) {
  const m1 = name.match(/-([1-4])\d/)
  if (m1) {
    const d = parseInt(m1[1])
    if (d >= 1 && d <= 4) return d
  }
  return null
}

const slotLookup = computed(() => {
  const map = {}
  for (const g of groups.value) {
    map[g.group_index] = {}
    for (const day of g.days || []) {
      map[g.group_index][day.date] = { weekday: day.weekday, date_iso: day.date_iso, slots: {} }
      for (const s of day.slots || []) {
        map[g.group_index][day.date].slots[s.slot] = { time: s.time, text: s.text, lessons: s.lessons || [] }
      }
    }
  }
  return map
})

const sortedWeeks = computed(() => {
  const set = new Set()
  for (const g of groups.value) {
    for (const d of g.days || []) {
      const mon = getMonday(parseDMY(d.date))
      set.add(mon.toISOString())
    }
  }
  const from = data.settings?.semester_start_date || data.settings?.start_date
  const to = data.settings?.first_course_semester_end_date || data.settings?.semester_end_date || data.settings?.end_date
  if (from && to) {
    const date = getMonday(new Date(`${from}T12:00:00`))
    const end = new Date(`${to}T12:00:00`)
    while (date <= end) {
      set.add(date.toISOString())
      date.setDate(date.getDate() + 7)
    }
  }
  return [...set].sort().map(iso => {
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
  for (const g of groups.value) {
    for (const d of g.days || []) {
      if (getMonday(parseDMY(d.date)).toISOString() === w.iso) set.add(d.date)
    }
  }
  const from = data.settings?.semester_start_date || data.settings?.start_date
  const to = data.settings?.first_course_semester_end_date || data.settings?.semester_end_date || data.settings?.end_date
  if (from && to) {
    for (let offset = 0; offset < 6; offset++) {
      const date = addDays(w.mon, offset)
      const iso = isoDateValue(date)
      if (from <= iso && iso <= to) set.add(fmtDate(date))
    }
  }
  return [...set].sort(compareDMY)
})

function getDayWeekday(dateStr) {
  for (const g of groups.value) {
    const lk = slotLookup.value[g.group_index]?.[dateStr]
    if (lk) return lk.weekday
  }
  return ['', 'ПН', 'ВТ', 'СР', 'ЧТ', 'ПТ', 'СБ'][parseDMY(dateStr).getDay()] || ''
}

const yearTabs = computed(() => {
  const counts = [0, 0, 0, 0]
  for (const g of groups.value) {
    const y = detectCourseYear(g.group_name)
    if (y >= 1 && y <= 4) counts[y - 1]++
  }
  return [
    { value: 0, label: 'Все группы', count: groups.value.length },
    { value: 1, label: '1 курс', count: counts[0] },
    { value: 2, label: '2 курс', count: counts[1] },
    { value: 3, label: '3 курс', count: counts[2] },
    { value: 4, label: '4 курс', count: counts[3] },
  ]
})

const filteredGroups = computed(() => {
  if (selectedYear.value === 0) return groups.value
  return groups.value.filter(g => detectCourseYear(g.group_name) === selectedYear.value)
})

const displaySlots = computed(() => {
  const values = new Set()
  for (const g of groups.value) for (const d of g.days || []) for (const s of d.slots || []) values.add(Number(s.slot))
  if (!values.size) for (let slot = 1; slot <= 7; slot++) values.add(slot)
  return [...values].sort((a, b) => a - b)
})

function getSlotTimeForSlot(slotNum) {
  for (const dateStr of weekDates.value) {
    for (const g of groups.value) {
      const s = slotLookup.value[g.group_index]?.[dateStr]?.slots[slotNum]
      if (s?.time) return s.time
    }
  }
  return ''
}

function cellLessons(gi, dateStr, slotNum) {
  return slotLookup.value[gi]?.[dateStr]?.slots[slotNum]?.lessons || []
}
function cellText(gi, dateStr, slotNum) {
  const arr = cellLessons(gi, dateStr, slotNum)
  if (!arr.length) return ''
  return arr.map(L => L.name).join(' | ')
}
function cellIsLab(gi, dateStr, slotNum) {
  return cellLessons(gi, dateStr, slotNum).some(L => L.is_lab)
}
function cellIsBlock(gi, dateStr, slotNum) {
  return cellLessons(gi, dateStr, slotNum).some(L => L.is_block)
}

function subgroupLabel(sub) {
  if (sub === -1 || sub == null) return 'вся группа'
  return `подгруппа ${(sub % 2) + 1}`
}

function groupNameById(gi) {
  return groups.value.find(g => g.group_index === gi)?.group_name || `Группа ${gi}`
}

function teacherNameById(id) {
  return data.teachers.find(t => t.id === id)?.name || `преподаватель #${id}`
}

function lessonSourceIndex(lesson) {
  if (lesson?.source_index) return lesson.source_index
  return data.lessons.find(item => Number(item.id) === Number(lesson?.id))?.source_index || ''
}

function isUpLesson(lesson) {
  return lesson?.is_block === true || /^(?:В?УП)(?:\.|\s)/i.test(String(lesson?.name || '').trim())
}

function isTransferredToPp(lesson) {
  const teacher = data.teachers.find(item => Number(item.id) === Number(lesson?.teacher))
  const name = String(teacher?.name || '').toLocaleLowerCase('ru')
  return name.includes('вынес') && name.includes('пп')
}

function upAllowedOnDate(groupId, dateIso) {
  const group = data.groups.find(item => Number(item.id) === Number(groupId))
  if (!group || !dateIso) return false
  if ((group.practice_periods || []).some(period => period.from <= dateIso && dateIso <= period.to)) return false
  const week = (group.academic_calendar || []).find(item => item.from <= dateIso && dateIso <= item.to)
  // The calendar UP value is a planning hint, not a hard restriction for a
  // dispatcher-locked lesson. Manual UP is allowed in an ordinary teaching
  // week, but never during PP or vacation.
  return Boolean(week && Number(week.pp_hours || 0) === 0 && !week.vacation)
}

function placedUpHoursOnDay(groupId, dateIso) {
  const manualGroup = groups.value.find(item => Number(item.group_index) === Number(groupId))
  if (!manualGroup) return 0
  let hours = 0
  for (const day of manualGroup.days || []) {
    if (day.date_iso !== dateIso) continue
    for (const slot of day.slots || []) {
      for (const placed of slot.lessons || []) {
        const source = data.lessons.find(item => Number(item.id) === Number(placed.id)) || placed
        if (isUpLesson(source)) hours += 6
      }
    }
  }
  return hours
}

function lessonHoursPerPlacement(lesson) {
  return isUpLesson(lesson) ? 6 : 2
}

const confirmedHoursByLesson = computed(() => {
  const totals = {}
  for (const entry of teachingLedger.value) {
    const id = Number(entry.lesson_id)
    totals[id] = (totals[id] || 0) + Number(entry.hours || 0)
  }
  return totals
})

const confirmedPlacementKeys = computed(() => new Set(teachingLedger.value.map(entry =>
  `${Number(entry.lesson_id)}|${entry.date}|${Number(entry.slot)}`
)))

const manualHoursByLesson = computed(() => {
  const totals = {}
  for (const group of groups.value) {
    for (const day of group.days || []) {
      for (const slot of day.slots || []) {
        for (const placed of slot.lessons || []) {
          const id = Number(placed.id)
          if (confirmedPlacementKeys.value.has(`${id}|${day.date_iso}|${Number(slot.slot)}`)) continue
          const lesson = data.lessons.find(item => Number(item.id) === id) || placed
          totals[id] = (totals[id] || 0) + lessonHoursPerPlacement(lesson)
        }
      }
    }
  }
  return totals
})

function lessonHourState(lesson) {
  const planHours = Number(lesson?.total_hours || 0)
  const confirmedHours = Number(confirmedHoursByLesson.value[lesson?.id] || 0)
  const manualHours = Number(manualHoursByLesson.value[lesson?.id] || 0)
  return {
    planHours,
    confirmedHours,
    manualHours,
    remainingHours: Math.max(0, planHours - confirmedHours - manualHours),
  }
}

function effectiveTargetSlots(lesson, dateIso = generationFrom.value) {
  const hours = lessonHourState(lesson)
  if (!isUpLesson(lesson)) return Math.ceil(Math.max(0, hours.planHours - hours.confirmedHours) / 2)
  if (lesson?.plan_active === false || Number(lesson?.total_hours || 0) <= 0 || Number(lesson?.teacher) < 0 ||
      isTransferredToPp(lesson) || !upAllowedOnDate(lesson.group, dateIso)) return 0
  return Math.floor(Math.max(0, hours.planHours - hours.confirmedHours) / 6)
}

const selectedTeacherUpHours = computed(() => {
  if (teacherFilter.value < 0) return null
  const lessons = data.lessons.filter(lesson =>
    Number(lesson.teacher) === teacherFilter.value &&
    lesson.plan_active !== false &&
    isUpLesson(lesson) &&
    Number(lesson.workload_source?.semester || 1) === 1
  )
  if (!lessons.length) return null
  const result = {
    name: teacherNameById(teacherFilter.value),
    plan: 0,
    confirmed: 0,
    manual: 0,
    remaining: 0,
  }
  for (const lesson of lessons) {
    const state = lessonHourState(lesson)
    result.plan += state.planHours
    result.confirmed += state.confirmedHours
    result.manual += state.manualHours
    result.remaining += state.remainingHours
  }
  return result
})

// ── Lesson progress ──
const placementByLesson = computed(() => {
  const acc = {}
  for (const g of groups.value) {
    for (const d of g.days || []) {
      for (const s of d.slots || []) {
        for (const L of (s.lessons || [])) {
          acc[L.id] = (acc[L.id] || 0) + 1
        }
      }
    }
  }
  return acc
})

const lessonProgress = computed(() => {
  const items = []
  const visibleGroups = new Set(filteredGroups.value.map(group => group.group_index))
  for (const lesson of data.lessons) {
    if (!visibleGroups.has(lesson.group)) continue
    if (teacherFilter.value >= 0 && Number(lesson.teacher) !== teacherFilter.value) continue
    const placed = rangePlacementByLesson.value[lesson.id] || 0
    const targetSlots = effectiveTargetSlots(lesson)
    const hourState = lessonHourState(lesson)
    if (targetSlots <= 0 && placed === 0) continue
    items.push({
      id: lesson.id,
      group: lesson.group,
      name: lesson.name,
      source_index: lesson.source_index || '',
      teacher: lesson.teacher,
      total_slots: targetSlots,
      placed,
      ...hourState,
      subgroup: lesson.subgroup,
    })
  }
  items.sort((a, b) => a.group - b.group || a.id - b.id)
  return items
})

const placedLessonCount = computed(() =>
  Object.values(placementByLesson.value).reduce((sum, count) => sum + count, 0)
)

const rangeError = computed(() => {
  if (!generationFrom.value || !generationTo.value) return 'Укажите обе даты генерации.'
  if (generationTo.value < generationFrom.value) return 'Дата окончания раньше даты начала.'
  const from = new Date(`${generationFrom.value}T12:00:00`)
  const to = new Date(`${generationTo.value}T12:00:00`)
  if ((to - from) / 86400000 > 6) return 'За один запуск можно достроить не более 7 календарных дней.'
  return ''
})

const rangePlacementByLesson = computed(() => {
  const counts = {}
  if (rangeError.value) return counts
  for (const group of groups.value) {
    for (const day of group.days || []) {
      if (!day.date_iso || day.date_iso < generationFrom.value || day.date_iso > generationTo.value) continue
      for (const slot of day.slots || []) {
        for (const lesson of slot.lessons || []) counts[lesson.id] = (counts[lesson.id] || 0) + 1
      }
    }
  }
  return counts
})

const rangePlacedLessonCount = computed(() =>
  Object.values(rangePlacementByLesson.value).reduce((sum, count) => sum + count, 0)
)

const sortedTeachers = computed(() => [...data.teachers].sort((a, b) => a.name.localeCompare(b.name, 'ru')))

const placedDates = computed(() => {
  const dates = new Set()
  for (const group of groups.value) {
    for (const day of group.days || []) {
      if ((day.slots || []).some(slot => (slot.lessons || []).length > 0)) dates.add(day.date_iso)
    }
  }
  return [...dates].filter(Boolean).sort()
})

// ── Picker modal ──
const pickerOpen = ref(false)
const pickerCell = ref(null) // { groupName, group_index, dateStr, dateIso, slotNum, time, weekday, lessons }
const pickerTitle = computed(() =>
  pickerCell.value
    ? `${pickerCell.value.groupName} · ${pickerCell.value.dateStr} · Пара ${pickerCell.value.slotNum}`
    : 'Выбор урока'
)

function openPicker(group, dateStr, slotNum) {
  const lk = slotLookup.value[group.group_index]?.[dateStr]
  const sl = lk?.slots[slotNum]
  pickerCell.value = {
    groupName: group.group_name,
    group_index: group.group_index,
    dateStr,
    dateIso: lk?.date_iso || isoFromDmy(dateStr),
    slotNum,
    time: sl?.time || '',
    weekday: lk?.weekday || '',
    lessons: sl?.lessons ? sl.lessons.slice() : [],
  }
  pickerOpen.value = true
}

function isoFromDmy(dateStr) {
  const [d, m, y] = dateStr.split('.')
  return `${y}-${m}-${d}`
}

const pickerOptions = computed(() => {
  if (!pickerCell.value) return []
  const sameGroup = data.lessons.filter(l =>
    l.group === pickerCell.value.group_index &&
    (teacherFilter.value < 0 || Number(l.teacher) === teacherFilter.value) &&
    l.plan_active !== false &&
    l.curriculum_active !== false &&
    l.is_class_hour !== true
  )
  return sameGroup.map(l => {
    const placed = rangePlacementByLesson.value[l.id] || 0
    const totalSlots = effectiveTargetSlots(l, pickerCell.value.dateIso)
    const hourState = lessonHourState(l)
    let conflict = ''
    let blocking = false
    if (Number(l.teacher) < 0) { conflict = 'не назначен преподаватель'; blocking = true }
    if (!conflict && l.is_pp === true) { conflict = 'производственная практика ставится по периоду ПП'; blocking = true }
    if (!conflict && isTransferredToPp(l)) { conflict = 'УП вынесена на производственную практику'; blocking = true }
    if (!conflict && isUpLesson(l) && !upAllowedOnDate(l.group, pickerCell.value.dateIso)) {
      conflict = 'УП нельзя ставить во время ПП или каникул'; blocking = true
    }
    if (!conflict && Number(l.total_hours || 0) <= 0) conflict = 'в учебном плане нет часов'
    if (!conflict && isUpLesson(l) && ![1, 3].includes(Number(pickerCell.value.slotNum))) {
      conflict = 'УП можно начинать только с 1-й или 3-й пары'; blocking = true
    }
    // Конфликт: тот же урок уже в этом слоте
    if (!conflict && pickerCell.value.lessons.some(x => x.id === l.id)) { conflict = 'уже в этой ячейке'; blocking = true }
    if (!conflict && pickerCell.value.lessons.some(x => Number(x.teacher_id) === Number(l.teacher))) {
      conflict = 'преподаватель уже занят в этой ячейке'; blocking = true
    }
    // Конфликт: эта же подгруппа уже занята
    else if (!conflict && l.subgroup !== -1) {
      const occupiedSub = pickerCell.value.lessons.some(x => x.subgroup === l.subgroup || x.subgroup === -1)
      if (occupiedSub) { conflict = 'подгруппа уже занята'; blocking = true }
    } else if (!conflict) {
      // -1 (вся группа) не должна сосуществовать ни с чем
      if (pickerCell.value.lessons.length) { conflict = 'ячейка занята подгруппой'; blocking = true }
    }
    // Конфликт: преподаватель занят в этот слот у другой группы
    if (!conflict) {
      for (const g of groups.value) {
        if (g.group_index === pickerCell.value.group_index) continue
        const arr = slotLookup.value[g.group_index]?.[pickerCell.value.dateStr]?.slots[pickerCell.value.slotNum]?.lessons || []
        if (arr.some(x => x.teacher_id === l.teacher)) {
          conflict = `препод #${l.teacher} занят в ${g.group_name}`
          blocking = true
          break
        }
      }
    }
    if (!conflict && isUpLesson(l) && placedUpHoursOnDay(l.group, pickerCell.value.dateIso) + 6 > 12) {
      conflict = 'на этот день уже поставлено 12 ч УП'; blocking = true
    }
    if (!conflict && hourState.remainingHours < lessonHoursPerPlacement(l)) {
      conflict = 'нагрузка уже выполнена'
    }
    return {
      id: l.id, name: l.name, subgroup: l.subgroup, teacher: l.teacher,
      source_index: l.source_index || '',
      is_lab: l.is_lab, is_block: isUpLesson(l), total_slots: totalSlots,
      placed,
      ...hourState,
      conflict,
      blocking,
    }
  }).sort((a, b) => Number(Boolean(a.conflict)) - Number(Boolean(b.conflict)) ||
    a.source_index.localeCompare(b.source_index, 'ru', { numeric: true }) ||
    a.name.localeCompare(b.name, 'ru') || a.subgroup - b.subgroup)
})

function addLessonToCell(opt) {
  if (opt.conflict && opt.blocking) {
    toast.warning(opt.conflict)
    return
  }
  const cell = pickerCell.value
  const group = cstore.ensureGroup(cell.group_index, cell.groupName)
  const day = cstore.findOrCreateDay(group, cell.dateIso, cell.dateStr, cell.weekday)
  const slot = cstore.findOrCreateSlot(day, cell.slotNum, cell.time)
  const newLessons = [...(slot.lessons || []), {
    id: opt.id, name: opt.name, teacher_id: opt.teacher,
    source_index: opt.source_index,
    subgroup: opt.subgroup, is_lab: opt.is_lab, is_block: opt.is_block,
  }]
  cstore.setSlotLessons(slot, newLessons)
  // Sync picker view
  pickerCell.value.lessons = newLessons.slice()
  if (opt.conflict) toast.warning(`Закреплено вручную: ${opt.conflict}`)
  else toast.success('Добавлено')
}

function removeLessonFromCell(lessonId) {
  const cell = pickerCell.value
  if (!cell) return
  const group = cstore.findGroup(cell.group_index)
  if (!group) return
  const day = group.days.find(d => d.date_iso === cell.dateIso)
  if (!day) return
  const slot = day.slots.find(s => s.slot === cell.slotNum)
  if (!slot) return
  const newLessons = (slot.lessons || []).filter(L => L.id !== lessonId)
  cstore.setSlotLessons(slot, newLessons)
  pickerCell.value.lessons = newLessons.slice()
  toast.success('Удалено')
}

// ── Header actions ──

async function onCopyFromAuto() {
  if ((cstore.manualData?.groups || []).length && !confirm('Заменить весь Конструктор текущей автогенерацией? Сохранённые ручные недели будут перезаписаны.')) return
  const r = await cstore.copyFromAuto()
  if (r.ok) toast.success('Скопировано из автогенерации')
  else toast.error(r.data?.message || 'Ошибка копирования')
}

async function onClear() {
  if (!confirm('Очистить ручное расписание полностью? Действие необратимо.')) return
  const r = await cstore.clear()
  if (r.ok) toast.success('Очищено')
  else toast.error(r.data?.message || 'Ошибка')
}

async function onSave() {
  const r = await cstore.save()
  if (r.ok) toast.success('Ручное расписание сохранено')
  else toast.error(r.data?.message || 'Ошибка сохранения')
}

async function onImportSchedule(event) {
  const file = event.target.files?.[0]
  event.target.value = ''
  if (!file) return
  if (cstore.error) {
    toast.error('Сначала устраните ошибку загрузки Конструктора — импорт не будет перезаписывать недоступные данные')
    return
  }
  importingSchedule.value = true
  importReport.value = null
  try {
    const { importScheduleWeekFromExcel } = await import('../utils/scheduleTemplateImport.js')
    const result = await importScheduleWeekFromExcel(file, {
      groups: data.groups,
      teachers: data.teachers,
      lessons: data.lessons,
      rooms: data.rooms,
      settings: data.settings,
      teaching_ledger: [],
    }, cstore.manualData)
    importReport.value = result
    if (!result.ok) {
      toast.error(result.errors[0] || 'Excel не удалось распознать')
      return
    }
    const range = result.dates.length === 1 ? result.dates[0] : `${result.dates[0]} — ${result.dates.at(-1)}`
    if (!confirm(`Восстановить в Конструкторе неделю ${range}? Будут заменены только эти даты; остальные недели сохранятся.`)) {
      importReport.value = null
      return
    }
    const previous = cstore.manualData
    const previousDirty = cstore.dirty
    cstore.manualData = result.manualData
    cstore.dirty = true
    const saved = await cstore.save()
    if (!saved.ok) {
      cstore.manualData = previous
      cstore.dirty = previousDirty
      importReport.value = { ...result, ok: false, errors: [saved.data?.message || 'Не удалось сохранить восстановленную неделю'] }
      toast.error(importReport.value.errors[0])
      return
    }
    await cstore.load()
    toast.success(`Неделя ${range} восстановлена из Excel и сохранена`)
  } catch (error) {
    importReport.value = { ok: false, errors: [error.message], warnings: [], dates: [] }
    toast.error(`Excel не прочитан: ${error.message}`)
  } finally {
    importingSchedule.value = false
  }
}

async function onSaveAndGenerate() {
  if (rangeError.value) {
    toast.error(rangeError.value)
    return
  }
  if (rangePlacedLessonCount.value === 0) {
    toast.error('Поставьте хотя бы одну обязательную пару внутри выбранного диапазона')
    return
  }
  generating.value = true
  const saved = await cstore.save()
  if (!saved.ok) {
    toast.error(saved.data?.message || 'Не удалось сохранить закреплённые пары')
    generating.value = false
    return
  }
  const options = {
    mode: 'weekly',
    lock_existing: 'manual',
    scope_from: generationFrom.value,
    scope_to: generationTo.value,
  }
  const result = await scheduleStore.regenerate(options)
  generating.value = false
  if (!result.ok && !result.async) {
    toast.error(result.message || 'Не удалось запустить генерацию')
    return
  }
  toast.success(`Закреплённые пары сохранены. Достраивается диапазон ${generationFrom.value} — ${generationTo.value}; остальные даты сохраняются.`)
  router.push('/schedule')
}

async function onValidate() {
  if (!cstore.manualData) return
  validating.value = true
  const r = await api.schedule.validate({ source: 'payload', schedule: cstore.manualData })
  validating.value = false
  if (!r.ok) {
    toast.error(r.data?.message || 'Проверка не выполнена')
    return
  }
  validationResult.value = r.data
  if (r.data?.ok) toast.success('Ручной вариант прошёл полную проверку')
  else toast.error(`В ручном варианте нарушений: ${r.data?.summary?.hard_errors ?? 0}`)
}

function addDays(date, count) {
  const result = new Date(date)
  result.setDate(result.getDate() + count)
  return result
}

function isoDate(date) {
  return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}-${String(date.getDate()).padStart(2, '0')}`
}

function displayDate(date) {
  return `${String(date.getDate()).padStart(2, '0')}.${String(date.getMonth() + 1).padStart(2, '0')}.${date.getFullYear()}`
}

const weekdayLabels = ['', 'ПН', 'ВТ', 'СР', 'ЧТ', 'ПТ', 'СБ']
const slotTimes = [
  '1 пара (08:30-09:55)', '2 пара (10:05-11:30)', '3 пара (12:25-13:50)',
  '4 пара (14:00-15:25)', '5 пара (15:35-16:55)', '6 пара (17:05-18:25)',
  '7 пара (18:35-19:55)',
]

function initScratch() {
  const startIso = data.settings?.start_date
  const endIso = data.settings?.end_date
  const start = startIso ? new Date(`${startIso}T12:00:00`) : null
  const end = endIso ? new Date(`${endIso}T12:00:00`) : null
  if (!start || !end || Number.isNaN(start.getTime()) || Number.isNaN(end.getTime()) || start > end) {
    toast.error('Сначала укажите корректные даты периода в настройках')
    return
  }
  const days = []
  for (let date = start, dayIndex = 0; date <= end; date = addDays(date, 1)) {
    const weekday = date.getDay()
    if (weekday === 0) continue
    days.push({
      date: displayDate(date),
      date_iso: isoDate(date),
      day_index: dayIndex++,
      weekday: weekdayLabels[weekday],
      slots: slotTimes.map((time, index) => ({ slot: index + 1, time, text: '-', lessons: [] })),
    })
  }
  cstore.manualData = {
    groups: data.groups.map(group => ({
      group_index: group.id,
      group_name: group.name,
      days: days.map(day => ({ ...day, slots: day.slots.map(slot => ({ ...slot, lessons: [] })) })),
    })),
  }
  cstore.dirty = true
  weekIndex.value = 0
  toast.info('Пустая сетка создана. Нажимайте на ячейки, чтобы закрепить занятия.')
}
</script>

<style scoped>
.header-actions { display: flex; gap: 8px; flex-wrap: wrap; }
.generation-range-card {
  display: grid; gap: 12px; margin-bottom: 16px; padding: 14px 16px;
  border: 1px solid var(--accent); border-radius: var(--radius); background: var(--accent-light);
}
.generation-range-copy { display: grid; gap: 4px; }
.generation-range-copy strong { color: var(--text-primary); font-size: 15px; }
.generation-range-copy span { color: var(--text-secondary); font-size: 13px; }
.generation-range-controls { display: flex; align-items: end; gap: 10px; flex-wrap: wrap; }
.generation-range-controls label { display: grid; gap: 4px; color: var(--text-secondary); font-size: 12px; font-weight: 600; }
.generation-range-controls input { width: 150px; }
.teacher-filter-field { min-width: min(100%, 320px); flex: 1; }
.teacher-filter-field select { width: 100%; }
.range-lock-count { padding: 9px 12px; border-radius: var(--radius-sm); background: var(--success-light); color: var(--success); font-size: 13px; }
.range-lock-count.empty { background: var(--bg-tertiary); color: var(--text-muted); }
.teacher-hours-summary { display:flex; align-items:center; gap:8px 14px; flex-wrap:wrap; padding:10px 12px; border:1px solid var(--border); border-radius:var(--radius-sm); background:var(--bg-secondary); color:var(--text-secondary); font-size:13px; }
.teacher-hours-summary strong { margin-right:auto; color:var(--text-primary); }
.teacher-hours-remaining { color:var(--success); font-weight:800; }
.generation-range-error { margin: 0; color: var(--error); font-size: 13px; font-weight: 600; }
.visually-hidden { position:absolute; width:1px; height:1px; padding:0; margin:-1px; overflow:hidden; clip:rect(0,0,0,0); white-space:nowrap; border:0; }
.import-report { display:flex; flex-direction:column; gap:6px; margin-bottom:16px; padding:12px 14px; border:1px solid var(--border); border-radius:var(--radius); font-size:13px; }
.import-report-ok { border-color:var(--success); background:var(--success-light); }
.import-report-error { border-color:var(--error); background:var(--error-light); }
.import-report ul { margin:0; padding-left:20px; }

.year-tabs { display: flex; gap: 6px; margin-bottom: 16px; flex-wrap: wrap; }
.year-tab {
  display: flex; align-items: center; gap: 6px;
  padding: 7px 14px; border-radius: var(--radius-sm);
  border: 1px solid var(--border); background: var(--bg-secondary);
  color: var(--text-secondary); font-size: 13px; font-weight: 500;
  cursor: pointer; transition: all var(--transition);
}
.year-tab:hover { border-color: var(--accent); color: var(--text-primary); }
.year-tab.active { background: var(--accent-light); border-color: var(--accent); color: var(--accent); }
.tab-count { background: var(--bg-tertiary); border-radius: 10px; padding: 0 6px; font-size: 11px; }

.week-nav {
  display: flex; align-items: center; justify-content: space-between; gap: 12px;
  margin-bottom: 16px; background: var(--bg-secondary); border: 1px solid var(--border);
  border-radius: var(--radius); padding: 12px 16px;
}
.week-label { display: flex; flex-direction: column; align-items: center; gap: 2px; }
.week-dates { font-size: 12px; color: var(--text-muted); }

.sched-scroll {
  overflow-x: auto; border-radius: var(--radius);
  border: 1px solid var(--border-strong); box-shadow: 0 4px 24px rgba(0, 0, 0, 0.35);
  margin-bottom: 20px;
}
.sched-table { border-collapse: collapse; font-size: 13px; width: 100%; table-layout: auto; }
.sticky-col { position: sticky; left: 0; z-index: 2; }
.th-slot {
  background: var(--bg-secondary); border-right: 2px solid var(--border-strong);
  border-bottom: 2px solid var(--border-strong); z-index: 4 !important;
  text-align: center; width: 84px; min-width: 76px; padding: 10px 8px;
}
.slot-header-icon { display: block; font-size: 18px; line-height: 1; margin-bottom: 4px; }
.slot-header-text {
  display: block; font-size: 10px; font-weight: 700;
  color: var(--text-secondary); text-transform: uppercase; letter-spacing: 0.08em;
}
.th-day {
  background: linear-gradient(135deg, #4f46e5 0%, #6366f1 60%, #818cf8 100%);
  color: #fff; text-align: center; padding: 10px 14px;
  border-left: 2px solid rgba(255,255,255,0.18);
  border-bottom: 1px solid rgba(255,255,255,0.18); white-space: nowrap;
}
.th-day:first-child { border-left: none; }
.day-name { display: block; font-size: 12px; font-weight: 800; letter-spacing: 0.08em; text-transform: uppercase; }
.day-date-sub { display: block; font-size: 10px; font-weight: 400; opacity: 0.72; margin-top: 3px; letter-spacing: 0.04em; }
.th-group {
  background: #1a2540; color: var(--text-secondary); text-align: center;
  font-size: 10px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.06em;
  padding: 6px 10px; white-space: nowrap;
  border-bottom: 2px solid var(--border-strong); min-width: 140px;
}
.th-group.day-separator { border-left: 2px solid var(--border-strong); }
.slot-label {
  background: var(--bg-secondary); padding: 10px 8px;
  display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 4px;
  border-right: 2px solid var(--border-strong);
  border-top: 1px solid var(--border); min-height: 56px;
  transition: background var(--transition);
}
.slot-num { font-size: 20px; font-weight: 800; color: var(--text-primary); line-height: 1; }
.slot-time { font-size: 9px; color: var(--text-muted); white-space: nowrap; letter-spacing: 0.03em; text-align: center; }

.slot-cell {
  padding: 8px 10px; vertical-align: top;
  border-top: 1px solid var(--border); transition: background var(--transition);
}
.slot-cell.editable { cursor: pointer; }
.slot-row:hover .slot-cell { background: rgba(255, 255, 255, 0.025); }
.slot-row:hover .slot-label { background: #253047; }
.slot-cell.editable:hover { background: rgba(99, 102, 241, 0.12) !important; }
.slot-cell.day-separator { border-left: 2px solid var(--border-strong); }

.cell-lab { background: rgba(16, 185, 129, 0.07); }
.slot-row:hover .cell-lab { background: rgba(16, 185, 129, 0.13) !important; }
.cell-practice { background: rgba(245, 158, 11, 0.07); }
.slot-row:hover .cell-practice { background: rgba(245, 158, 11, 0.13) !important; }

.cell-inner { display: flex; flex-direction: column; gap: 4px; }
.cell-lesson { display:flex; flex-direction:column; align-items:flex-start; gap:3px; }
.cell-lesson + .cell-lesson { margin-top:6px; padding-top:6px; border-top:1px dashed var(--border-strong); }
.cell-subject { font-size: 12px; font-weight: 600; color: var(--text-primary); line-height: 1.35; }
.subject-index { display:inline-block; margin-right:4px; padding:1px 5px; border-radius:4px; background:var(--accent-light); color:var(--accent); font-size:.85em; font-weight:800; white-space:nowrap; }
.cell-detail { font-size:10px; color:var(--text-secondary); }
.cell-empty {
  color: var(--text-muted); font-size: 18px; display: flex;
  align-items: center; justify-content: center; min-height: 40px; opacity: 0.25;
}

.progress-card {
  background: var(--bg-secondary); border: 1px solid var(--border);
  border-radius: var(--radius); padding: 16px; margin-bottom: 20px;
}
.card-title { font-size: 14px; font-weight: 700; margin-bottom: 10px; color: var(--text-primary); }
.progress-help { margin: 0 0 10px; color: var(--text-secondary); font-size: 12px; line-height: 1.45; }
.progress-legend { display:flex; flex-wrap:wrap; gap:8px 16px; margin-bottom:12px; color:var(--text-muted); font-size:11px; }
.progress-legend span { display:inline-flex; align-items:center; gap:6px; }
.legend-dot { width:9px; height:9px; border-radius:50%; background:var(--text-muted); }
.progress-partial-dot { background:#f59e0b; }
.progress-complete-dot { background:#10b981; }
.progress-over-dot { background:#ef4444; }
.progress-grid {
  display: grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr)); gap: 6px;
}
.progress-item {
  display: flex; justify-content: space-between; gap: 8px; padding: 6px 10px;
  background: var(--bg-tertiary); border-radius: var(--radius-sm); font-size: 12px;
}
.progress-name { color: var(--text-secondary); display:flex; flex-direction:column; }
.progress-name small { margin-top:2px; color:var(--text-muted); font-size:10px; }
.progress-group { color: var(--accent); font-weight: 600; margin-right: 6px; }
.progress-counts { display:flex; flex-direction:column; align-items:flex-end; color: var(--text-primary); font-weight: 600; font-variant-numeric: tabular-nums; white-space:nowrap; }
.progress-counts span { color:var(--text-muted); font-size:10px; font-weight:500; }
.progress-partial .progress-counts { color: #f59e0b; }
.progress-complete .progress-counts { color: #10b981; }
.progress-over .progress-counts { color: #ef4444; }

.picker-body { max-height: 60vh; overflow-y: auto; }
.picker-current-item {
  display: flex; align-items: center; justify-content: space-between; gap: 8px;
  padding: 8px 10px; background: var(--bg-tertiary); border-radius: var(--radius-sm);
  margin-bottom: 6px;
}
.picker-list { display: flex; flex-direction: column; gap: 4px; }
.picker-option {
  padding: 8px 10px; background: var(--bg-tertiary); border-radius: var(--radius-sm);
  cursor: pointer; transition: background var(--transition);
  border: 1px solid transparent;
}
.picker-option:hover { background: var(--accent-light); border-color: var(--accent); }
.picker-option.picker-warn { background: var(--warning-light); border-color: var(--warning); }
.picker-option.picker-warn:hover { background: var(--warning-light); border-color: var(--warning); }
.picker-option.picker-blocked { opacity: 0.6; cursor: not-allowed; }
.picker-option.picker-blocked:hover { background: rgba(239, 68, 68, 0.1); border-color: var(--error); }
.picker-option-main { display: flex; justify-content: space-between; gap: 10px; align-items: center; }
.picker-option-name { font-weight: 600; font-size: 13px; }
.picker-option-meta { font-size: 11px; color: var(--text-muted); }
.picker-option-warn { font-size: 11px; color: var(--error); margin-top: 4px; }
.muted { color: var(--text-muted); font-size: 11px; }

.center-block { display: flex; flex-direction: column; align-items: center; padding: 80px 20px; }
</style>
