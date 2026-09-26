import http from 'node:http'
import net from 'node:net'
import path from 'node:path'
import { existsSync } from 'node:fs'
import { readFile, stat } from 'node:fs/promises'
import { spawn } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const siteDir = path.dirname(fileURLToPath(import.meta.url))
const rootDir = path.resolve(siteDir, '..')
const frontendDir = path.join(rootDir, 'frontend', 'dist')
const host = process.env.SITE_HOST || '127.0.0.1'
const port = Number(process.env.SITE_PORT || 4173)
const apiHost = '127.0.0.1'
const apiPort = Number(process.env.SITE_API_PORT || 8080)

const backendCandidates = [
  process.env.SITE_BACKEND_PATH,
  path.join(rootDir, 'build-site', 'timetable_solver.exe'),
  path.join(rootDir, 'build', 'Release', 'timetable_solver.exe'),
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
  if ((request.url || '').startsWith('/api')) proxyApi(request, response)
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
