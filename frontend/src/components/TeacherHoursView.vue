<template>
  <div class="teacher-hours">
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
      <span class="icon">🔍</span>
      <h3>Преподаватель не найден</h3>
      <p>Измените строку поиска</p>
    </div>

    <div v-else-if="!selectedTeacher" class="empty-state">
      <span class="icon">👤</span>
      <h3>Введите ФИО преподавателя</h3>
      <p>Используйте поиск выше</p>
    </div>

    <div v-if="selectedTeacher" class="hours-content">
      <div class="hours-header">
        <h2>{{ selectedTeacher.name }}</h2>
        <button class="btn btn-primary" @click="downloadHours" :disabled="loading">
          <span v-if="loading" class="spinner spinner-sm" />
          📥 Скачать учет часов
        </button>
      </div>

      <div v-if="loading" class="center-block">
        <span class="spinner spinner-lg" style="color: var(--accent)" />
        <p style="margin-top:16px; color: var(--text-muted)">Загрузка учета часов…</p>
      </div>

      <div v-else-if="error" class="empty-state">
        <span class="icon">⚠️</span>
        <h3>{{ error }}</h3>
        <button class="btn btn-primary" style="margin-top:16px" @click="loadHours">
          Повторить
        </button>
      </div>

      <div v-else-if="!hoursData || hoursData.length === 0" class="empty-state">
        <span class="icon">📋</span>
        <h3>Данные по учету часов отсутствуют</h3>
      </div>

      <div v-else class="hours-table-wrap">
        <table class="hours-table">
          <thead>
            <tr>
              <th>Дисциплина</th>
              <th>Группа</th>
              <th>Тип занятия</th>
              <th>Часов по плану</th>
              <th>Проведено</th>
              <th>Остаток</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="(row, idx) in hoursData" :key="idx">
              <td>{{ row.subject }}</td>
              <td>{{ row.group }}</td>
              <td>{{ row.type }}</td>
              <td class="num-cell">{{ row.planned }}</td>
              <td class="num-cell">{{ row.completed }}</td>
              <td class="num-cell" :class="{'warn': row.remaining < 0}">{{ row.remaining }}</td>
            </tr>
          </tbody>
          <tfoot>
            <tr>
              <td colspan="3"><strong>Итого:</strong></td>
              <td class="num-cell"><strong>{{ totalPlanned }}</strong></td>
              <td class="num-cell"><strong>{{ totalCompleted }}</strong></td>
              <td class="num-cell" :class="{'warn': totalRemaining < 0}">
                <strong>{{ totalRemaining }}</strong>
              </td>
            </tr>
          </tfoot>
        </table>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch } from 'vue'
import { useDataStore } from '../stores/data.js'
import { api } from '../api/index.js'

const dataStore = useDataStore()

const searchQuery = ref('')
const selectedTeacher = ref(null)
const hoursData = ref([])
const loading = ref(false)
const error = ref(null)

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
  loadHours()
}

function clearSelection() {
  selectedTeacher.value = null
  searchQuery.value = ''
  hoursData.value = []
}

async function loadHours() {
  if (!selectedTeacher.value) return
  loading.value = true
  error.value = null

  const res = await api.data.teacherOccupancy()
  loading.value = false

  if (res.ok) {
    const allData = res.data || []
    const teacherId = selectedTeacher.value.id
    hoursData.value = allData
      .filter(row => row.teacher_id === teacherId)
      .map(row => ({
        subject: row.lesson_name || '',
        group: row.group_name || '',
        type: row.lesson_type || 'Занятие',
        planned: row.planned_hours || 0,
        completed: row.completed_hours || 0,
        remaining: (row.planned_hours || 0) - (row.completed_hours || 0),
      }))
  } else {
    error.value = res.data?.message || 'Ошибка загрузки данных'
  }
}

const totalPlanned = computed(() => {
  return hoursData.value.reduce((sum, row) => sum + row.planned, 0)
})

const totalCompleted = computed(() => {
  return hoursData.value.reduce((sum, row) => sum + row.completed, 0)
})

const totalRemaining = computed(() => {
  return hoursData.value.reduce((sum, row) => sum + row.remaining, 0)
})

async function downloadHours() {
  if (!selectedTeacher.value || !hoursData.value.length) return

  loading.value = true

  let csv = 'Дисциплина,Группа,Тип занятия,Часов по плану,Проведено,Остаток\n'
  for (const row of hoursData.value) {
    csv += `"${row.subject}","${row.group}","${row.type}",${row.planned},${row.completed},${row.remaining}\n`
  }
  csv += `\nИтого:,,,${totalPlanned.value},${totalCompleted.value},${totalRemaining.value}\n`

  const blob = new Blob(['﻿' + csv], { type: 'text/csv;charset=utf-8;' })
  const url = URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.href = url
  link.download = `Учет_часов_${selectedTeacher.value.name.replace(/\s+/g, '_')}_${new Date().toISOString().slice(0, 10)}.csv`
  link.click()
  URL.revokeObjectURL(url)

  loading.value = false
}

watch(selectedTeacher, () => {
  if (selectedTeacher.value) loadHours()
})
</script>

<style scoped>
.teacher-hours { display: flex; flex-direction: column; gap: 20px; }

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

.hours-content { display: flex; flex-direction: column; gap: 16px; }

.hours-header {
  display: flex; align-items: center; justify-content: space-between;
  gap: 16px; padding: 12px 16px; background: var(--bg-secondary);
  border: 1px solid var(--border); border-radius: var(--radius);
  flex-wrap: wrap;
}
.hours-header h2 { font-size: 18px; font-weight: 700; margin: 0; }

.hours-table-wrap {
  overflow-x: auto; -webkit-overflow-scrolling: touch;
  border: 1px solid var(--border); border-radius: var(--radius);
}

.hours-table {
  width: 100%; border-collapse: collapse; font-size: 13px;
  background: var(--bg-secondary);
}

.hours-table thead th {
  background: var(--bg-tertiary); color: var(--text-primary);
  font-weight: 700; text-align: left; padding: 12px 14px;
  border-bottom: 2px solid var(--border-strong);
  white-space: nowrap;
}

.hours-table tbody td {
  padding: 10px 14px; border-bottom: 1px solid var(--border);
}

.hours-table tfoot td {
  padding: 12px 14px; border-top: 2px solid var(--border-strong);
  background: var(--bg-tertiary);
}

.num-cell {
  text-align: right; font-variant-numeric: tabular-nums;
}

.num-cell.warn {
  color: var(--error); font-weight: 600;
}

.center-block {
  display: flex; flex-direction: column; align-items: center; padding: 60px 20px;
}

@media (max-width: 640px) {
  .search-bar { flex-direction: column; align-items: stretch; }
  .hours-header { flex-direction: column; align-items: stretch; }
  .teacher-list { grid-template-columns: 1fr; }
}
</style>
