const { app, BrowserWindow, Menu, dialog, shell } = require('electron')
const { spawn } = require('node:child_process')
const crypto = require('node:crypto')
const fs = require('node:fs')
const http = require('node:http')
const net = require('node:net')
const path = require('node:path')

const PRODUCT_NAME = 'Расписание УСПО'
const LOOPBACK = '127.0.0.1'
const BACKEND_READY_TIMEOUT_MS = 20_000
const SEED_VERSION = '1.3.1'
const REPLACEABLE_SEED_DATABASE_HASHES = new Set([
  // Исходная база из установщика 1.0.0. Изменённую пользователем базу не перезаписываем.
  '01BFB8D30D3DF7423DF51D232CAAEE12C3806028C20821FE790536E69193756B',
  // Исходная база установщика 1.3.0 перед субботней правкой кабинетов.
  '50B1003AF48F9EDCD371D4AED303807F85F84780E652D8FB1C6B2EDF4B420F9B',
])

let mainWindow = null
let backendProcess = null
let uiServer = null
let isQuitting = false
let workspaceRoot = ''
let logFile = ''

function appendLog(message) {
  const line = `[${new Date().toISOString()}] ${message}\n`
  try {
    if (logFile) fs.appendFileSync(logFile, line, 'utf8')
  } catch {
    // Ошибка журнала не должна мешать запуску приложения.
  }
}

function copyDirectoryIfMissing(source, destination) {
  if (!fs.existsSync(source) || fs.existsSync(destination)) return
  fs.mkdirSync(path.dirname(destination), { recursive: true })
  fs.cpSync(source, destination, { recursive: true, force: false, errorOnExist: false })
}

function fileSha256(filePath) {
  return crypto.createHash('sha256').update(fs.readFileSync(filePath)).digest('hex').toUpperCase()
}

function writeSeedMarker() {
  fs.writeFileSync(path.join(workspaceRoot, 'seed-version.json'), JSON.stringify({
    version: SEED_VERSION,
    installed_at: new Date().toISOString(),
  }, null, 2), 'utf8')
}

function upgradePristineLegacySeed(seedRoot, targetData, targetDatabase) {
  if (!fs.existsSync(targetDatabase)) return false
  let installedSeedVersion = ''
  try {
    installedSeedVersion = JSON.parse(
      fs.readFileSync(path.join(workspaceRoot, 'seed-version.json'), 'utf8'),
    ).version || ''
  } catch {
    // Старые версии могли не иметь маркера; для них остаётся проверка хэша.
  }
  const isUrgentPreviousRelease = installedSeedVersion === '1.3.0'
  if (!isUrgentPreviousRelease &&
      !REPLACEABLE_SEED_DATABASE_HASHES.has(fileSha256(targetDatabase))) return false

  const backupRoot = path.join(workspaceRoot, 'update-backups', `before-seed-${SEED_VERSION}-${timestampForPath()}`)
  fs.mkdirSync(backupRoot, { recursive: true })
  fs.cpSync(targetData, path.join(backupRoot, 'data'), { recursive: true })
  for (const name of ['latest', 'manual', 'published']) {
    const existingOutput = path.join(workspaceRoot, 'output', name)
    if (fs.existsSync(existingOutput)) {
      fs.cpSync(existingOutput, path.join(backupRoot, 'output', name), { recursive: true })
    }
  }

  fs.copyFileSync(path.join(seedRoot, 'data', 'timetable_data.json'), targetDatabase)
  const latestSeed = path.join(seedRoot, 'output', 'latest')
  if (fs.existsSync(latestSeed)) {
    fs.mkdirSync(path.join(workspaceRoot, 'output', 'latest'), { recursive: true })
    fs.cpSync(latestSeed, path.join(workspaceRoot, 'output', 'latest'), { recursive: true, force: true })
  }
  writeSeedMarker()
  appendLog(`Исходная база обновлена до ${SEED_VERSION}; резервная копия: ${backupRoot}`)
  return true
}

function resolveRuntimePaths() {
  const projectRoot = path.resolve(__dirname, '..')
  const resourcesRoot = app.isPackaged ? process.resourcesPath : projectRoot
  return {
    backendExecutable: app.isPackaged
      ? path.join(resourcesRoot, 'solver', 'timetable_solver.exe')
      : path.join(projectRoot, 'build-desktop', 'timetable_solver.exe'),
    frontendRoot: app.isPackaged
      ? path.join(resourcesRoot, 'frontend')
      : path.join(projectRoot, 'frontend', 'dist'),
    seedRoot: app.isPackaged ? path.join(resourcesRoot, 'seed') : projectRoot,
  }
}

function ensureWorkspace(seedRoot) {
  workspaceRoot = process.env.RASPIS_DESKTOP_DATA_DIR
    ? path.resolve(process.env.RASPIS_DESKTOP_DATA_DIR)
    : path.join(app.getPath('userData'), 'workspace')

  fs.mkdirSync(workspaceRoot, { recursive: true })
  fs.mkdirSync(path.join(workspaceRoot, 'logs'), { recursive: true })
  logFile = path.join(workspaceRoot, 'logs', 'desktop.log')

  const sourceData = path.join(seedRoot, 'data')
  const targetData = path.join(workspaceRoot, 'data')
  const targetDatabase = path.join(targetData, 'timetable_data.json')

  if (!fs.existsSync(targetDatabase)) {
    fs.cpSync(sourceData, targetData, { recursive: true, force: false, errorOnExist: false })
    writeSeedMarker()
    appendLog(`Создана рабочая база из ${sourceData}`)
  } else {
    const upgraded = upgradePristineLegacySeed(seedRoot, targetData, targetDatabase)
    if (!upgraded) {
      copyDirectoryIfMissing(path.join(sourceData, 'history'), path.join(targetData, 'history'))
      const sourceAuth = path.join(sourceData, 'auth_config.json')
      const targetAuth = path.join(targetData, 'auth_config.json')
      if (fs.existsSync(sourceAuth) && !fs.existsSync(targetAuth)) fs.copyFileSync(sourceAuth, targetAuth)
    }
  }

  for (const name of ['latest', 'manual', 'published']) {
    const target = path.join(workspaceRoot, 'output', name)
    copyDirectoryIfMissing(path.join(seedRoot, 'output', name), target)
    fs.mkdirSync(target, { recursive: true })
  }
}

function findFreePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer()
    server.unref()
    server.once('error', reject)
    server.listen(0, LOOPBACK, () => {
      const address = server.address()
      const port = typeof address === 'object' && address ? address.port : 0
      server.close((error) => error ? reject(error) : resolve(port))
    })
  })
}

function startBackend(executable, port) {
  if (!fs.existsSync(executable)) throw new Error(`Не найден решатель: ${executable}`)

  appendLog(`Запуск решателя ${executable} на порту ${port}`)
  backendProcess = spawn(executable, [String(port)], {
    cwd: workspaceRoot,
    windowsHide: true,
    stdio: ['ignore', 'pipe', 'pipe'],
  })

  backendProcess.stdout.on('data', (chunk) => appendLog(`[solver] ${chunk.toString().trimEnd()}`))
  backendProcess.stderr.on('data', (chunk) => appendLog(`[solver:error] ${chunk.toString().trimEnd()}`))
  backendProcess.once('error', (error) => appendLog(`Ошибка процесса решателя: ${error.stack || error.message}`))
  backendProcess.once('exit', (code, signal) => {
    appendLog(`Решатель завершён: code=${code}, signal=${signal || '-'}`)
    backendProcess = null
    if (!isQuitting) {
      dialog.showErrorBox(PRODUCT_NAME, `Локальный решатель неожиданно завершил работу.\n\nЖурнал: ${logFile}`)
      app.quit()
    }
  })
}

function backendIsReady(port) {
  return new Promise((resolve) => {
    const request = http.get({ hostname: LOOPBACK, port, path: '/api/auth/status', timeout: 1_000 }, (response) => {
      response.resume()
      resolve(response.statusCode === 200)
    })
    request.once('timeout', () => request.destroy())
    request.once('error', () => resolve(false))
  })
}

async function waitForBackend(port) {
  const deadline = Date.now() + BACKEND_READY_TIMEOUT_MS
  while (Date.now() < deadline) {
    if (await backendIsReady(port)) return
    await new Promise((resolve) => setTimeout(resolve, 200))
  }
  throw new Error(`Решатель не ответил за ${BACKEND_READY_TIMEOUT_MS / 1000} секунд. Журнал: ${logFile}`)
}

const MIME_TYPES = {
  '.css': 'text/css; charset=utf-8',
  '.html': 'text/html; charset=utf-8',
  '.ico': 'image/x-icon',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.map': 'application/json; charset=utf-8',
  '.png': 'image/png',
  '.svg': 'image/svg+xml; charset=utf-8',
  '.txt': 'text/plain; charset=utf-8',
  '.xlsx': 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet',
  '.webmanifest': 'application/manifest+json; charset=utf-8',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2',
}

function proxyApi(request, response, backendPort) {
  const headers = { ...request.headers, host: `${LOOPBACK}:${backendPort}` }
  const proxy = http.request({
    hostname: LOOPBACK,
    port: backendPort,
    method: request.method,
    path: request.url,
    headers,
  }, (backendResponse) => {
    response.writeHead(backendResponse.statusCode || 502, backendResponse.headers)
    backendResponse.pipe(response)
  })
  proxy.once('error', (error) => {
    appendLog(`Ошибка прокси API: ${error.message}`)
    if (!response.headersSent) {
      response.writeHead(502, { 'Content-Type': 'application/json; charset=utf-8' })
    }
    response.end(JSON.stringify({ success: false, message: 'Локальный решатель временно недоступен' }))
  })
  request.pipe(proxy)
}

function serveFrontend(request, response, frontendRoot) {
  let pathname = '/'
  try {
    pathname = decodeURIComponent(new URL(request.url, 'http://localhost').pathname)
  } catch {
    response.writeHead(400)
    response.end('Bad Request')
    return
  }

  const root = path.resolve(frontendRoot)
  const relativePath = pathname === '/' ? 'index.html' : pathname.replace(/^\/+/, '')
  let filePath = path.resolve(root, relativePath)
  if (filePath !== root && !filePath.startsWith(`${root}${path.sep}`)) {
    response.writeHead(403)
    response.end('Forbidden')
    return
  }

  if (!fs.existsSync(filePath) || fs.statSync(filePath).isDirectory()) filePath = path.join(root, 'index.html')
  if (!fs.existsSync(filePath)) {
    response.writeHead(500, { 'Content-Type': 'text/plain; charset=utf-8' })
    response.end('Интерфейс приложения не собран')
    return
  }

  const extension = path.extname(filePath).toLowerCase()
  const headers = {
    'Content-Type': MIME_TYPES[extension] || 'application/octet-stream',
    'X-Content-Type-Options': 'nosniff',
    'Content-Security-Policy': "default-src 'self'; img-src 'self' data: blob:; style-src 'self' 'unsafe-inline'; script-src 'self'; connect-src 'self'; worker-src 'self' blob:; font-src 'self' data:; object-src 'none'; base-uri 'self'; frame-ancestors 'none'",
  }
  if (path.basename(filePath) === 'index.html' || path.basename(filePath).startsWith('sw.')) {
    headers['Cache-Control'] = 'no-store'
  }
  response.writeHead(200, headers)
  fs.createReadStream(filePath).pipe(response)
}

function startUiServer(frontendRoot, backendPort, uiPort) {
  if (!fs.existsSync(path.join(frontendRoot, 'index.html'))) {
    throw new Error(`Не найден собранный интерфейс: ${frontendRoot}`)
  }
  uiServer = http.createServer((request, response) => {
    if ((request.url || '').startsWith('/api')) proxyApi(request, response, backendPort)
    else serveFrontend(request, response, frontendRoot)
  })
  return new Promise((resolve, reject) => {
    uiServer.once('error', reject)
    uiServer.listen(uiPort, LOOPBACK, resolve)
  })
}

function timestampForPath() {
  return new Date().toISOString().replace(/[:.]/g, '-').replace('T', '_').replace('Z', '')
}

async function createBackup() {
  const result = await dialog.showOpenDialog(mainWindow, {
    title: 'Выберите папку для резервной копии',
    defaultPath: app.getPath('documents'),
    properties: ['openDirectory', 'createDirectory'],
  })
  if (result.canceled || !result.filePaths[0]) return

  const destinationRoot = path.resolve(result.filePaths[0])
  if (destinationRoot === path.resolve(workspaceRoot) || destinationRoot.startsWith(`${path.resolve(workspaceRoot)}${path.sep}`)) {
    await dialog.showMessageBox(mainWindow, {
      type: 'warning',
      title: PRODUCT_NAME,
      message: 'Резервную копию нельзя сохранять внутри рабочей папки приложения.',
    })
    return
  }

  const backupRoot = path.join(destinationRoot, `Расписание-резервная-копия-${timestampForPath()}`)
  fs.mkdirSync(backupRoot, { recursive: false })
  fs.cpSync(path.join(workspaceRoot, 'data'), path.join(backupRoot, 'data'), { recursive: true })
  for (const name of ['latest', 'manual', 'published']) {
    const source = path.join(workspaceRoot, 'output', name)
    if (fs.existsSync(source)) fs.cpSync(source, path.join(backupRoot, 'output', name), { recursive: true })
  }
  fs.writeFileSync(path.join(backupRoot, 'desktop-info.json'), JSON.stringify({
    product: PRODUCT_NAME,
    version: app.getVersion(),
    created_at: new Date().toISOString(),
  }, null, 2), 'utf8')

  await dialog.showMessageBox(mainWindow, {
    type: 'info',
    title: PRODUCT_NAME,
    message: 'Резервная копия создана.',
    detail: backupRoot,
  })
}

function buildApplicationMenu() {
  const template = [
    {
      label: 'Файл',
      submenu: [
        { label: 'Открыть папку данных', click: () => shell.openPath(path.join(workspaceRoot, 'data')) },
        { label: 'Открыть папку результатов', click: () => shell.openPath(path.join(workspaceRoot, 'output')) },
        { label: 'Создать резервную копию…', click: () => createBackup().catch((error) => dialog.showErrorBox(PRODUCT_NAME, error.message)) },
        { type: 'separator' },
        { role: 'quit', label: 'Выход' },
      ],
    },
    {
      label: 'Правка',
      submenu: [
        { role: 'undo', label: 'Отменить' },
        { role: 'redo', label: 'Повторить' },
        { type: 'separator' },
        { role: 'cut', label: 'Вырезать' },
        { role: 'copy', label: 'Копировать' },
        { role: 'paste', label: 'Вставить' },
        { role: 'selectAll', label: 'Выбрать всё' },
      ],
    },
    {
      label: 'Вид',
      submenu: [
        { role: 'reload', label: 'Обновить интерфейс' },
        { type: 'separator' },
        { role: 'resetZoom', label: 'Масштаб 100%' },
        { role: 'zoomIn', label: 'Увеличить' },
        { role: 'zoomOut', label: 'Уменьшить' },
        { type: 'separator' },
        { role: 'togglefullscreen', label: 'Полноэкранный режим' },
      ],
    },
    {
      label: 'Справка',
      submenu: [
        {
          label: 'О приложении',
          click: () => dialog.showMessageBox(mainWindow, {
            type: 'info',
            title: PRODUCT_NAME,
            message: `${PRODUCT_NAME} ${app.getVersion()}`,
            detail: 'Локальный редактор и генератор учебного расписания. Все данные хранятся на этом компьютере.',
          }),
        },
      ],
    },
  ]
  Menu.setApplicationMenu(Menu.buildFromTemplate(template))
}

function createMainWindow(uiPort) {
  mainWindow = new BrowserWindow({
    title: PRODUCT_NAME,
    width: 1440,
    height: 920,
    minWidth: 1024,
    minHeight: 700,
    show: false,
    backgroundColor: '#0f172a',
    autoHideMenuBar: false,
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  })

  mainWindow.webContents.setWindowOpenHandler(({ url }) => {
    if (/^https?:\/\//i.test(url)) shell.openExternal(url)
    return { action: 'deny' }
  })
  mainWindow.once('ready-to-show', () => mainWindow.show())
  mainWindow.on('closed', () => { mainWindow = null })
  mainWindow.loadURL(`http://${LOOPBACK}:${uiPort}`)
}

function httpGet(port, requestPath) {
  return new Promise((resolve, reject) => {
    http.get({ hostname: LOOPBACK, port, path: requestPath, timeout: 5_000 }, (response) => {
      let body = ''
      response.setEncoding('utf8')
      response.on('data', (chunk) => { body += chunk })
      response.on('end', () => resolve({ status: response.statusCode, body }))
    }).once('error', reject)
  })
}

async function runSmokeTest(uiPort) {
  const page = await httpGet(uiPort, '/')
  const api = await httpGet(uiPort, '/api/auth/status')
  if (page.status !== 200 || !page.body.includes('<div id="app">') || api.status !== 200) {
    throw new Error(`Smoke test failed: page=${page.status}, api=${api.status}`)
  }
  appendLog('Smoke test passed')
  process.stdout.write(`SMOKE_OK ui=${page.status} api=${api.status}\n`)
}

async function shutdown() {
  isQuitting = true
  if (uiServer) {
    await new Promise((resolve) => uiServer.close(resolve))
    uiServer = null
  }
  if (backendProcess) {
    backendProcess.kill()
    backendProcess = null
  }
}

async function bootstrap() {
  const paths = resolveRuntimePaths()
  ensureWorkspace(paths.seedRoot)
  appendLog(`Запуск ${PRODUCT_NAME} ${app.getVersion()}, packaged=${app.isPackaged}`)

  const backendPort = await findFreePort()
  const uiPort = await findFreePort()
  startBackend(paths.backendExecutable, backendPort)
  await waitForBackend(backendPort)
  await startUiServer(paths.frontendRoot, backendPort, uiPort)
  appendLog(`Интерфейс запущен: http://${LOOPBACK}:${uiPort}`)

  if (process.argv.includes('--smoke-test')) {
    await runSmokeTest(uiPort)
    await shutdown()
    app.exit(0)
    return
  }

  buildApplicationMenu()
  createMainWindow(uiPort)
}

const hasSingleInstanceLock = app.requestSingleInstanceLock()
if (!hasSingleInstanceLock) {
  app.quit()
} else {
  app.on('second-instance', () => {
    if (mainWindow) {
      if (mainWindow.isMinimized()) mainWindow.restore()
      mainWindow.focus()
    }
  })

  app.whenReady().then(bootstrap).catch(async (error) => {
    appendLog(`Критическая ошибка запуска: ${error.stack || error.message}`)
    dialog.showErrorBox(PRODUCT_NAME, `${error.message}\n\nЖурнал: ${logFile || 'не создан'}`)
    await shutdown()
    app.exit(1)
  })

  app.on('activate', () => {
    if (!mainWindow && uiServer && uiServer.address()) createMainWindow(uiServer.address().port)
  })
  app.on('window-all-closed', () => app.quit())
  app.on('before-quit', () => {
    isQuitting = true
    if (backendProcess) backendProcess.kill()
    if (uiServer) uiServer.close()
  })
}
