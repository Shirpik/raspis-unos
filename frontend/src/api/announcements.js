const API_BASE = '/api'

/**
 * Получает список всех рассылок
 */
export async function getAnnouncements(course = null, groupId = null) {
  let url = `${API_BASE}/announcements`
  const params = new URLSearchParams()
  
  if (course) params.append('course', course)
  if (groupId) params.append('group_id', groupId)
  
  if (params.toString()) {
    url += '?' + params.toString()
  }

  const response = await fetch(url)
  if (!response.ok) {
    throw new Error('Failed to fetch announcements')
  }
  return await response.json()
}

/**
 * Создает новую рассылку (только для диспетчеров)
 */
export async function createAnnouncement(data) {
  const response = await fetch(`${API_BASE}/announcements`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify(data)
  })
  
  if (!response.ok) {
    throw new Error('Failed to create announcement')
  }
  return await response.json()
}

/**
 * Удаляет рассылку (только для диспетчеров)
 */
export async function deleteAnnouncement(id) {
  const response = await fetch(`${API_BASE}/announcements/${id}`, {
    method: 'DELETE'
  })
  
  if (!response.ok) {
    throw new Error('Failed to delete announcement')
  }
  return await response.json()
}

/**
 * Отмечает рассылку как прочитанную
 */
export async function markAnnouncementAsRead(id) {
  const response = await fetch(`${API_BASE}/announcements/${id}/read`, {
    method: 'POST'
  })
  
  if (!response.ok) {
    throw new Error('Failed to mark announcement as read')
  }
  return await response.json()
}
