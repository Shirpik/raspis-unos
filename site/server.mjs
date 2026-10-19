import http from 'node:http'
import net from 'node:net'
import path from 'node:path'
import { existsSync } from 'node:fs'
import { readFile, stat } from 'node:fs/promises'
import { spawn } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { withReferenceContext } from '../frontend/src/utils/referenceAccountingMetrics.js'

const siteDir = path.dirname(fileURLToPath(import.meta.url))
const rootDir = path.resolve(siteDir, '..')
const frontendDir = path.join(rootDir, 'frontend', 'dist')
const host = process.env.SITE_HOST || '127.0.0.1'
const port = Number(process.env.SITE_PORT || 4173)
const apiHost = '127.0.0.1'
const apiPort = Number(process.env.SITE_API_PORT || 8080)
const scheduleTemplatePath = path.join(rootDir, 'frontend', 'public', 'templates', 'schedule-template.xlsx')
const scheduleExportModule = import('../frontend/src/utils/scheduleTemplateExport.js')

const backendCandidates = [
  process.env.SITE_BACKEND_PATH,
  path.join(rootDir, 'build', 'Release', 'timetable_solver.exe'),
  path.join(rootDir, 'build-site', 'timetable_solver.exe'),
  // Historical temporary builds are a last resort only. They can be stale.
  path.join(rootDir, '.tmp', 'build-sep7', 'Release', 'timetable_solver.exe'),
].filter(Boolean)
const backendPath = backendCandidates.find(candidate => existsSync(candidate))

if (!existsSync(path.join(frontendDir, 'index.html'))) {
  throw new Error('frontend/dist не найден. Выполните npm run build в каталоге frontend.')
}

const portIsOpen = (hostname, targetPort) => new Promise(resolve => {
  const socket = net.createConnection({ host: hostname, port: targetPort })
  const finish = value => { socket.destroy(); resolve(value) }
  socket.setTimeout(500)
  socket.once('connect', () => finish(true))
  socket.once('timeout', () => finish(false))
  socket.once('error', () => finish(false))
})

let backend = null
if (!(await portIsOpen(apiHost, apiPort))) {
  if (!backendPath) throw new Error('Backend не найден. Соберите timetable_solver.exe или задайте SITE_BACKEND_PATH.')
  backend = spawn(backendPath, [String(apiPort)], {
    cwd: rootDir,
    windowsHide: true,
    stdio: ['ignore', 'inherit', 'inherit'],
  })
  backend.once('exit', code => {
    if (code && code !== 0) console.error(`Backend остановлен с кодом ${code}`)
  })
}

const mime = new Map([
  ['.html', 'text/html; charset=utf-8'], ['.js', 'text/javascript; charset=utf-8'],
  ['.css', 'text/css; charset=utf-8'], ['.json', 'application/json; charset=utf-8'],
  ['.svg', 'image/svg+xml'], ['.png', 'image/png'], ['.ico', 'image/x-icon'],
  ['.xlsx', 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'],
  ['.webmanifest', 'application/manifest+json'],
])

function proxyApi(request, response) {
  const upstream = http.request({
    hostname: apiHost,
    port: apiPort,
    path: request.url,
    method: request.method,
    headers: { ...request.headers, host: `${apiHost}:${apiPort}` },
  }, upstreamResponse => {
    response.writeHead(upstreamResponse.statusCode || 502, upstreamResponse.headers)
    upstreamResponse.pipe(response)
  })
  upstream.on('error', error => {
    if (!response.headersSent) response.writeHead(502, { 'Content-Type': 'application/json; charset=utf-8' })
    response.end(JSON.stringify({ message: `Backend ещё не готов: ${error.message}` }))
  })
  request.pipe(upstream)
}

async function readJsonBody(request, maxBytes = 25 * 1024 * 1024) {
  const chunks = []
  let received = 0
  for await (const chunk of request) {
    received += chunk.length
    if (received > maxBytes) throw new Error('Расписание слишком большое для экспорта')
    chunks.push(chunk)
  }
  return JSON.parse(Buffer.concat(chunks).toString('utf8'))
}

async function restrictScheduleToLastGeneratedScope(schedule) {
  if (schedule?.generation_scope !== 'selected_dates' || schedule?.status !== 'scoped') return schedule
  try {
    const audit = JSON.parse(await readFile(path.join(rootDir, 'output', 'latest', 'strict_audit.json'), 'utf8'))
    const from = audit.period_from
    const to = audit.period_to
    if (!/^\d{4}-\d{2}-\d{2}$/.test(from || '') || !/^\d{4}-\d{2}-\d{2}$/.test(to || '')) return schedule
    const dates = new Set((schedule.groups || []).flatMap(group => (group.days || [])
      .map(day => day.date_iso)
      .filter(date => date && from <= date && date <= to)))
    if (!dates.size) return schedule
    return {
      ...schedule,
      groups: (schedule.groups || []).map(group => ({
        ...group,
        days: (group.days || []).filter(day => dates.has(day.date_iso)),
      })),
    }
  } catch {
    return schedule
  }
}

async function exportScheduleExcel(request, response) {
  if (request.method !== 'POST') {
    response.writeHead(405, { Allow: 'POST' }).end()
    return
  }
  try {
    const schedule = await restrictScheduleToLastGeneratedScope(await readJsonBody(request))
    const template = await readFile(scheduleTemplatePath)
    const { buildScheduleExcelWorkbook, scheduleExcelFilename } = await scheduleExportModule
    const result = await buildScheduleExcelWorkbook(schedule, template)
    const body = Buffer.from(await result.workbook.xlsx.writeBuffer())
    const filename = scheduleExcelFilename(schedule)
    response.writeHead(200, {
      'Content-Type': mime.get('.xlsx'),
      'Content-Disposition': `attachment; filename*=UTF-8''${encodeURIComponent(filename)}`,
      'Content-Length': body.length,
      'Cache-Control': 'no-store',
      'X-Export-Lessons': String(result.insertedLessons),
      'X-Export-Groups': String(result.groups),
      'X-Export-Dates': String(result.dates),
    })
    response.end(body)
  } catch (error) {
    response.writeHead(400, { 'Content-Type': 'application/json; charset=utf-8' })
    response.end(JSON.stringify({ message: error.message || 'Не удалось собрать Excel' }))
  }
}

async function exportAccountingExcel(request, response) {
  if (request.method !== 'POST') {
    response.writeHead(405, { Allow: 'POST' }).end()
    return
  }
  try {
    // Read through the authenticated API, never accept an unauthenticated report payload.
    const upstream = await fetch(`http://${apiHost}:${apiPort}/api/hours`, {
      headers: { cookie: request.headers.cookie || '', authorization: request.headers.authorization || '' },
      signal: AbortSignal.timeout(30000),
    })
    if (!upstream.ok) {
      response.writeHead(upstream.status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' })
      response.end(await upstream.text())
      return
    }
    const hours = await upstream.json()
    const contextResponse = await fetch(`http://${apiHost}:${apiPort}/api/bootstrap`, {
      headers: { cookie: request.headers.cookie || '', authorization: request.headers.authorization || '' },
      signal: AbortSignal.timeout(30000),
    })
    if (!contextResponse.ok) throw new Error('Не удалось получить календарь групп для экспорта')
    const context = await contextResponse.json()
    const { buildReferenceAccountingWorkbook, writeReferenceAccountingBuffer } = await import('../frontend/src/utils/referenceAccountingExport.js')
    const { workbook, groups, semester } = buildReferenceAccountingWorkbook(withReferenceContext(hours, context))
    const body = Buffer.from(await writeReferenceAccountingBuffer(workbook))
    const filename = `Учет_часов_по_образцу_${semester}_семестр_${hours.as_of_date || new Date().toISOString().slice(0, 10)}.xlsx`
    response.writeHead(200, {
      'Content-Type': mime.get('.xlsx'),
      'Content-Disposition': `attachment; filename*=UTF-8''${encodeURIComponent(filename)}`,
      'Content-Length': body.length, 'Cache-Control': 'no-store',
      'X-Export-Groups': String(groups.length),
      'X-Export-Sheets': String(workbook.worksheets.length),
    })
    response.end(body)
  } catch (error) {
    response.writeHead(500, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' })
    response.end(JSON.stringify({ message: error.message || 'Не удалось собрать учёт часов' }))
  }
}

async function serveFrontend(request, response) {
  const url = new URL(request.url || '/', `http://${request.headers.host || 'localhost'}`)
  let relative = decodeURIComponent(url.pathname).replace(/^\/+/, '')
  if (!relative) relative = 'index.html'
  let filePath = path.resolve(frontendDir, relative)
  if (!filePath.startsWith(`${path.resolve(frontendDir)}${path.sep}`) && filePath !== path.join(frontendDir, 'index.html')) {
    response.writeHead(403).end('Forbidden')
    return
  }
  try {
    if (!(await stat(filePath)).isFile()) throw new Error('not a file')
  } catch {
    if (relative.startsWith('assets/')) {
      // Previously opened tabs still request the old name of this stable export API.
      // This compatibility module has no hashed dependencies, so an old tab
      // cannot accidentally import and mount a second version of the whole app.
      if (/^assets\/referenceAccountingExport-[\w-]+\.js$/.test(relative)) {
        response.writeHead(200, { 'Content-Type': 'text/javascript; charset=utf-8', 'Cache-Control': 'no-store' })
        response.end("export { downloadReferenceAccounting as exportReferenceAccountingWorkbook } from '/export/accounting-client.js';")
        return
      }
      response.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8', 'Cache-Control': 'no-store' }).end('Asset not found')
      return
    }
    filePath = path.join(frontendDir, 'index.html')
  }
  const body = await readFile(filePath)
  const extension = path.extname(filePath).toLowerCase()
  // Only fingerprinted assets are immutable. A year-long cache for sw.js,
  // registerSW.js or the XLSX template can keep an obsolete UI/export alive.
  const fingerprinted = path.dirname(filePath) === path.join(frontendDir, 'assets') &&
    /-[\w-]{8,}\.(js|css)$/.test(path.basename(filePath))
  response.writeHead(200, {
    'Content-Type': mime.get(extension) || 'application/octet-stream',
    'Cache-Control': fingerprinted ? 'public, max-age=31536000, immutable' : 'no-cache',
    'X-Content-Type-Options': 'nosniff',
    'X-Frame-Options': 'SAMEORIGIN',
  })
  response.end(body)
}

const server = http.createServer((request, response) => {
  const pathname = new URL(request.url || '/', `http://${request.headers.host || 'localhost'}`).pathname
  if (pathname === '/export/schedule.xlsx') exportScheduleExcel(request, response)
  else if (pathname === '/export/accounting.xlsx') exportAccountingExcel(request, response)
  else if (pathname === '/export/accounting-client.js') {
    readFile(path.join(rootDir, 'frontend/src/utils/accountingDownload.js')).then(body => {
      response.writeHead(200, { 'Content-Type': 'text/javascript; charset=utf-8', 'Cache-Control': 'no-store' })
      response.end(body)
    }).catch(() => response.writeHead(500).end('Export client unavailable'))
  }
  else if (pathname.startsWith('/api')) proxyApi(request, response)
  else serveFrontend(request, response).catch(error => {
    response.writeHead(500, { 'Content-Type': 'text/plain; charset=utf-8' })
    response.end(error.message)
  })
})

const shutdown = () => {
  server.close(() => process.exit(0))
  if (backend && !backend.killed) backend.kill()
}
process.once('SIGINT', shutdown)
process.once('SIGTERM', shutdown)

server.listen(port, host, () => {
  console.log(`Сайт расписания: http://${host}:${port}`)
  console.log(`Backend API: http://${apiHost}:${apiPort}`)
})
