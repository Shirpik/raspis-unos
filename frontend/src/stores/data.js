import { defineStore } from 'pinia'
import { ref } from 'vue'
import { api } from '../api/index.js'

export const useDataStore = defineStore('data', () => {
  const teachers = ref([])
  const groups = ref([])
  const lessons = ref([])
  const unavailable = ref([])
  const teacherUnavailable = ref([])
  const rooms = ref([])
  const roomTypes = ref([])
  const substitutions = ref([])
  const settings = ref({ start_date: '', end_date: '' })
  const loading = ref(false)

  async function loadAll() {
    loading.value = true
    const [t, g, l, u, tu, r, rt, s] = await Promise.all([
      api.teachers.list(),
      api.groups.list(),
      api.lessons.list(),
      api.unavailable.list(),
      api.teacherUnavailable.list(),
      api.rooms.list(),
      api.roomTypes.list(),
      api.settings.get(),
    ])
    if (t.ok) teachers.value = t.data
    if (g.ok) groups.value = g.data
    if (l.ok) lessons.value = l.data
    if (u.ok) unavailable.value = u.data
    if (tu.ok) teacherUnavailable.value = tu.data
    if (r.ok) rooms.value = r.data
    if (rt.ok) roomTypes.value = rt.data
    if (s.ok) settings.value = s.data
    loading.value = false
  }

  async function loadTeachers() {
    const r = await api.teachers.list()
    if (r.ok) teachers.value = r.data
    return r
  }
  async function createTeacher(d) {
    const r = await api.teachers.create(d)
    if (r.ok) await loadTeachers()
    return r
  }
  async function updateTeacher(id, d) {
    const r = await api.teachers.update(id, d)
    if (r.ok) await loadTeachers()
    return r
  }
  async function bulkUpdateTeachers(ids, patch, all = false) {
    const r = await api.teachers.bulkUpdate(ids, patch, all)
    if (r.ok) await loadTeachers()
    return r
  }
  async function deleteTeacher(id) {
    const r = await api.teachers.remove(id)
    if (r.ok) await loadTeachers()
    return r
  }

  async function loadGroups() {
    const r = await api.groups.list()
    if (r.ok) groups.value = r.data
    return r
  }
  async function createGroup(d) {
    const r = await api.groups.create(d)
    if (r.ok) await loadGroups()
    return r
  }
  async function updateGroup(id, d) {
    const r = await api.groups.update(id, d)
    if (r.ok) await loadGroups()
    return r
  }
  async function bulkUpdateGroups(ids, patch, all = false) {
    const r = await api.groups.bulkUpdate(ids, patch, all)
    if (r.ok) await loadGroups()
    return r
  }
  async function deleteGroup(id) {
    const r = await api.groups.remove(id)
    if (r.ok) await loadGroups()
    return r
  }

  async function loadLessons() {
    const r = await api.lessons.list()
    if (r.ok) lessons.value = r.data
    return r
  }
  async function createLesson(d) {
    const r = await api.lessons.create(d)
    if (r.ok) await loadLessons()
    return r
  }
  async function updateLesson(id, d) {
    const r = await api.lessons.update(id, d)
    if (r.ok) await loadLessons()
    return r
  }
  async function deleteLesson(id) {
    const r = await api.lessons.remove(id)
    if (r.ok) await loadLessons()
    return r
  }

  async function loadUnavailable() {
    const r = await api.unavailable.list()
    if (r.ok) unavailable.value = r.data
    return r
  }
  async function createUnavailable(d) {
    const r = await api.unavailable.create(d)
    if (r.ok) await loadUnavailable()
    return r
  }
  async function deleteUnavailable(id) {
    const r = await api.unavailable.remove(id)
    if (r.ok) await loadUnavailable()
    return r
  }

  async function saveSettings(d) {
    const r = await api.settings.update(d)
    if (r.ok) settings.value = { ...settings.value, ...d }
    return r
  }

  async function loadRooms() {
    const r = await api.rooms.list()
    if (r.ok) rooms.value = r.data
    return r
  }
  async function createRoom(d) { const r = await api.rooms.create(d); if (r.ok) await loadRooms(); return r }
  async function updateRoom(id, d) { const r = await api.rooms.update(id, d); if (r.ok) await loadRooms(); return r }
  async function deleteRoom(id) { const r = await api.rooms.remove(id); if (r.ok) await loadRooms(); return r }

  async function loadRoomTypes() {
    const r = await api.roomTypes.list()
    if (r.ok) roomTypes.value = r.data
    return r
  }
  async function createRoomType(d) { const r = await api.roomTypes.create(d); if (r.ok) await loadRoomTypes(); return r }
  async function updateRoomType(id, d) { const r = await api.roomTypes.update(id, d); if (r.ok) await loadRoomTypes(); return r }
  async function deleteRoomType(id) { const r = await api.roomTypes.remove(id); if (r.ok) await loadRoomTypes(); return r }

  async function loadTeacherUnavailable() {
    const r = await api.teacherUnavailable.list()
    if (r.ok) teacherUnavailable.value = r.data
    return r
  }
  async function createTeacherUnavailable(d) { const r = await api.teacherUnavailable.create(d); if (r.ok) await loadTeacherUnavailable(); return r }
  async function deleteTeacherUnavailable(id) { const r = await api.teacherUnavailable.remove(id); if (r.ok) await loadTeacherUnavailable(); return r }

  async function loadSubstitutions() { const r = await api.substitutions.list(); if (r.ok) substitutions.value = r.data; return r }
  async function createSubstitution(d) { const r = await api.substitutions.create(d); if (r.ok) await loadSubstitutions(); return r }
  async function updateSubstitution(id, d) { const r = await api.substitutions.update(id, d); if (r.ok) await loadSubstitutions(); return r }
  async function deleteSubstitution(id) { const r = await api.substitutions.remove(id); if (r.ok) await loadSubstitutions(); return r }

  return {
    teachers, groups, lessons, unavailable, teacherUnavailable, rooms, roomTypes, substitutions, settings, loading,
    loadAll,
    loadTeachers, createTeacher, updateTeacher, deleteTeacher, bulkUpdateTeachers,
    loadGroups, createGroup, updateGroup, deleteGroup, bulkUpdateGroups,
    loadLessons, createLesson, updateLesson, deleteLesson,
    loadUnavailable, createUnavailable, deleteUnavailable,
    loadTeacherUnavailable, createTeacherUnavailable, deleteTeacherUnavailable,
    loadRooms, createRoom, updateRoom, deleteRoom,
    loadRoomTypes, createRoomType, updateRoomType, deleteRoomType,
    loadSubstitutions, createSubstitution, updateSubstitution, deleteSubstitution,
    saveSettings,
  }
})
