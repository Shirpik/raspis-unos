import test from 'node:test'
import assert from 'node:assert/strict'
import http from 'node:http'
import { spawn } from 'node:child_process'
import { once } from 'node:events'
import fs from 'node:fs/promises'
import ExcelJS from '../frontend/node_modules/exceljs/excel.js'

test('production Node serves the SPA, proxies API and exports an editable XLSX', async t => {
  const api = http.createServer((req, res) => {
    res.setHeader('Content-Type', 'application/json')
    if (req.url === '/api/auth/status') res.end('{"authenticated":false}')
    else { res.statusCode = 401; res.end('{"message":"Login required"}') }
  })
  api.listen(0, '127.0.0.1'); await once(api, 'listening')
  t.after(() => api.close())
  const reserve = http.createServer()
  reserve.listen(0, '127.0.0.1'); await once(reserve, 'listening')
  const port = reserve.address().port
  await new Promise(resolve => reserve.close(resolve))
  const child = spawn(process.execPath, ['site/server.mjs'], {
    env: { ...process.env, SITE_PORT: String(port), SITE_API_PORT: String(api.address().port) },
    stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true,
  })
  let log = ''; child.stdout.on('data', data => { log += data }); child.stderr.on('data', data => { log += data })
  t.after(async () => { if (child.exitCode === null) { child.kill(); await once(child, 'exit') } })
  const base = `http://127.0.0.1:${port}`
  for (let attempt = 0; attempt < 100; attempt++) {
    try { if ((await fetch(base)).ok) break } catch {}
    assert.equal(child.exitCode, null, log)
    await new Promise(resolve => setTimeout(resolve, 100))
  }
  const page = await fetch(base + '/schedule')
  assert.equal(page.status, 200, log)
  assert.match(await page.text(), /<html/)
  assert.equal(page.headers.get('cache-control'), 'no-cache')
  assert.deepEqual(await (await fetch(base + '/api/auth/status')).json(), { authenticated: false })
  assert.equal((await fetch(base + '/assets/nonexistent-hash.js')).status, 404)
  assert.equal((await fetch(base + '/export/accounting.xlsx', { method: 'POST' })).status, 401)
  const template = new ExcelJS.Workbook()
  await template.xlsx.load(await fs.readFile('frontend/public/templates/schedule-template.xlsx'))
  const group = template.worksheets[0].getCell(62, 4).text.trim()
  assert(group)
  const payload = { groups: [{ group_index: 0, group_name: group, days: [{
    date_iso: '2026-11-04', date: '04.11.2026', weekday: 'СР',
    slots: [{ slot: 1, text: 'Тест — Преподаватель', lessons: [{ id: 0, name: 'Тест', subgroup: -1, room_name: '101' }] }],
  }] }] }
  const exported = await fetch(base + '/export/schedule.xlsx', { method: 'POST',
    headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) })
  assert.equal(exported.status, 200, await exported.clone().text())
  assert.equal(exported.headers.get('x-export-dates'), '1')
  assert.equal(exported.headers.get('x-export-lessons'), '1')
  const workbook = new ExcelJS.Workbook()
  await workbook.xlsx.load(Buffer.from(await exported.arrayBuffer()))
  assert(workbook.worksheets.length > 0)
  payload.generation_scope = 'selected_dates'; payload.status = 'scoped'
  payload.scope_from = payload.scope_to = '2026-11-04'
  payload.groups[0].days.unshift({ ...payload.groups[0].days[0], date_iso: '2026-11-02', date: '02.11.2026', weekday: 'ПН' })
  const scoped = await fetch(base + '/export/schedule.xlsx', { method: 'POST',
    headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) })
  assert.equal(scoped.status, 200)
  assert.equal(scoped.headers.get('x-export-dates'), '1')
  assert.equal(scoped.headers.get('x-export-lessons'), '1')
})
