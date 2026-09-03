const FALLBACK_SLOT_COUNT = 7

export function splitSlotText(text) {
  return String(text || '').split(' | ').filter(segment => segment && segment !== '-')
}

export function collectSlotNumbers(groups, fallbackCount = FALLBACK_SLOT_COUNT) {
  const values = new Set()
  for (const group of groups || []) {
    for (const day of group?.days || []) {
      for (const slot of day?.slots || []) {
        const value = Number(slot?.slot)
        if (Number.isInteger(value) && value >= 0) values.add(value)
      }
    }
  }
  if (!values.size) {
    for (let slot = 1; slot <= fallbackCount; slot++) values.add(slot)
  }
  return [...values].sort((a, b) => a - b)
}

export function subgroupOrdinal(subgroup, groupIndex = null) {
  const value = Number(subgroup)
  if (!Number.isInteger(value) || value < 0) return null

  const group = Number(groupIndex)
  if (Number.isInteger(group) && group >= 0) {
    const local = value - group * 2
    if (local === 0 || local === 1) return local + 1
  }

  // Older snapshots sometimes stored local 0/1 values. For a global subgroup
  // id the parity still identifies the subgroup within its group.
  return ((value % 2) + 2) % 2 + 1
}

export function subgroupLabel(subgroup, groupIndex = null, suffix = 'подгруппа') {
  const ordinal = subgroupOrdinal(subgroup, groupIndex)
  return ordinal ? `${ordinal} ${suffix}` : ''
}

export function slotLessonEntries(slot, groupIndex = null) {
  if (!slot) return []
  const segments = splitSlotText(slot.text)
  const lessons = Array.isArray(slot.lessons) ? slot.lessons : []

  if (lessons.length) {
    return lessons.map((lesson, index) => ({
      lesson,
      segment: segments[index] || segments[0] || '',
      subgroupOrdinal: subgroupOrdinal(lesson?.subgroup, groupIndex),
      subgroupLabel: subgroupLabel(lesson?.subgroup, groupIndex),
    }))
  }

  // Keep every legacy text segment visible even when a snapshot predates the
  // structured `lessons` array.
  return segments.map(segment => ({
    lesson: null,
    segment,
    subgroupOrdinal: null,
    subgroupLabel: '',
  }))
}

export function scheduleRowHeight(cells, minimum = 58) {
  const maxLines = Math.max(1, ...(cells || []).map(value => String(value || '').split('\n').length))
  return Math.max(minimum, 10 + maxLines * 14)
}
