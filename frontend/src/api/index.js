const BASE = '/api'

async function request(method, path, body = null) {
  const opts = { method, credentials: 'same-origin', headers: { 'Content-Type': 'application/json; charset=utf-8' } }
  if (body !== null) opts.body = JSON.stringify(body)
  try {
    const res = await fetch(BASE + path, opts)
    const text = await res.text()
    let data
    try { data = JSON.parse(text) } catch { data = { message: text } }
    if (res.status === 401 && !path.startsWith('/auth/')) {
      window.dispatchEvent(new CustomEvent('auth:unauthorized'))
    }
    return { ok: res.ok, status: res.status, data }
  } catch {
    return { ok: false, status: 0, data: { message: 'Нет связи с сервером' } }
  }
}

export const api = {
  auth: {
    status: () => request('GET', '/auth/status'),
    login: (username, password) => request('POST', '/auth/login', { username, password }),
    logout: () => request('POST', '/auth/logout'),
    changeCredentials: (data) => request('PUT', '/auth/credentials', data),
  },
  data: {
    get: () => request('GET', '/data'),
    replace: (d) => request('PUT', '/data', d),
    audit: (d = null) => d === null ? request('GET', '/audit') : request('POST', '/audit', d),
    hours: () => request('GET', '/hours'),
    semesterReadout: () => request('GET', '/semester/readout'),
    teacherOccupancy: () => request('GET', '/accounting/teacher-occupancy'),
    versions: () => request('GET', '/versions'),
    restore: (filename) => request('POST', `/versions/${encodeURIComponent(filename)}/restore`),
  },
  transfer: {
    exportBundle: () => request('GET', '/transfer/export'),
    importBundle: (bundle, primarySchedule, publish = true) => request('POST', '/transfer/import', {
      bundle,
      primary_schedule: primarySchedule,
      publish,
    }),
  },
  schedule: {
    get: () => request('GET', '/schedule'),
    getGroup: (id) => request('GET', `/schedule/group/${encodeURIComponent(id)}`),
    regenerate: (opts = {}) => request('POST', '/schedule/regenerate', opts),
    progress: () => request('GET', '/schedule/progress'),
    cancel: () => request('POST', '/schedule/cancel'),
    validate: (data = { source: 'auto' }) => request('POST', '/schedule/validate', data),
    publish: () => request('POST', '/schedule/publish'),
    getPublished: () => request('GET', '/schedule/published'),
    rooms: () => request('GET', '/schedule/rooms'),
  },
  constructor: {
    load: () => request('GET', '/schedule/manual'),
    save: (data) => request('POST', '/schedule/manual', data),
    clear: () => request('DELETE', '/schedule/manual'),
    copyFromAuto: () => request('POST', '/schedule/manual/copy-from-auto'),
  },
  groups: {
    list: () => request('GET', '/groups'),
    create: (d) => request('POST', '/groups', d),
    update: (id, d) => request('PUT', `/groups/${id}`, d),
    remove: (id) => request('DELETE', `/groups/${id}`),
    bulkUpdate: (ids, patch, all = false) => request('PATCH', '/groups/bulk', { ids, patch, all }),
  },
  teachers: {
    list: () => request('GET', '/teachers'),
    create: (d) => request('POST', '/teachers', d),
    update: (id, d) => request('PUT', `/teachers/${id}`, d),
    remove: (id) => request('DELETE', `/teachers/${id}`),
    bulkUpdate: (ids, patch, all = false) => request('PATCH', '/teachers/bulk', { ids, patch, all }),
  },
  lessons: {
    list: () => request('GET', '/lessons'),
    create: (d) => request('POST', '/lessons', d),
    update: (id, d) => request('PUT', `/lessons/${id}`, d),
    remove: (id) => request('DELETE', `/lessons/${id}`),
  },
  unavailable: {
    list: () => request('GET', '/unavailable'),
    create: (d) => request('POST', '/unavailable', d),
    update: (id, d) => request('PUT', `/unavailable/${id}`, d),
    remove: (id) => request('DELETE', `/unavailable/${id}`),
  },
  teacherUnavailable: {
    list: () => request('GET', '/teacher-unavailable'),
    create: (d) => request('POST', '/teacher-unavailable', d),
    update: (id, d) => request('PUT', `/teacher-unavailable/${id}`, d),
    remove: (id) => request('DELETE', `/teacher-unavailable/${id}`),
  },
  rooms: {
    list: () => request('GET', '/rooms'),
    create: (d) => request('POST', '/rooms', d),
    update: (id, d) => request('PUT', `/rooms/${id}`, d),
    remove: (id) => request('DELETE', `/rooms/${id}`),
  },
  roomTypes: {
    list: () => request('GET', '/room-types'),
    create: (d) => request('POST', '/room-types', d),
    update: (id, d) => request('PUT', `/room-types/${id}`, d),
    remove: (id) => request('DELETE', `/room-types/${id}`),
  },
  substitutions: {
    list: () => request('GET', '/substitutions'),
    create: (d) => request('POST', '/substitutions', d),
    update: (id, d) => request('PUT', `/substitutions/${id}`, d),
    remove: (id) => request('DELETE', `/substitutions/${id}`),
    csvUrl: '/api/accounting/substitutions.csv',
  },
  settings: {
    get: () => request('GET', '/settings'),
    update: (d) => request('PATCH', '/settings', d),
    getSolverConfig: () => request('GET', '/settings/solver-config'),
    updateSolverConfig: (d) => request('PATCH', '/settings/solver-config', d),
    resetSolverConfig: () => request('POST', '/settings/solver-config/reset'),
    getSolverProfiles: () => request('GET', '/settings/solver-profiles'),
    applySolverProfile: (id) => request('POST', `/settings/solver-profile/${encodeURIComponent(id)}`),
  },
}
