export async function downloadReferenceAccounting({ hours }) {
  const response = await fetch('/export/accounting.xlsx', {
    method: 'POST', credentials: 'same-origin',
    headers: { 'Content-Type': 'application/json' }, body: '{}',
  })
  // Vite and installations with an older site server can use the browser builder.
  if (response.status === 404 || response.status === 405 ||
      response.ok && response.headers.get('Content-Type')?.includes('text/html')) {
    const { exportReferenceAccountingWorkbook } = await import('./referenceAccountingExport.js')
    return exportReferenceAccountingWorkbook({ hours })
  }
  if (!response.ok) {
    const error = await response.json().catch(() => ({}))
    throw new Error(error.message || `Ошибка экспорта (${response.status})`)
  }
  if (!response.headers.get('Content-Type')?.includes('spreadsheetml.sheet')) {
    throw new Error('Сервер экспорта обновлён. Обновите страницу и повторите скачивание.')
  }
  const blob = await response.blob()
  const filename = decodeURIComponent(response.headers.get('Content-Disposition')?.match(/filename\*=UTF-8''([^;]+)/i)?.[1] || 'Учет_часов.xlsx')
  const url = URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.href = url
  link.download = filename
  document.body.appendChild(link)
  link.click()
  link.remove()
  setTimeout(() => URL.revokeObjectURL(url), 1000)
  return {
    filename, groups: Number(response.headers.get('X-Export-Groups')),
    sheets: Number(response.headers.get('X-Export-Sheets')),
  }
}
