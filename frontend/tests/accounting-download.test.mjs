import test from 'node:test'
import assert from 'node:assert/strict'
import { downloadReferenceAccounting } from '../src/utils/accountingDownload.js'

test('reference download uses the server XLSX without requesting an export chunk', async t => {
  const calls = []
  t.mock.method(globalThis, 'fetch', async (url, options) => {
    calls.push({ url, options })
    return new Response(new Uint8Array([80, 75, 3, 4]), { headers: {
      'Content-Type': 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet',
      'Content-Disposition': "attachment; filename*=UTF-8''hours.xlsx",
      'X-Export-Groups': '46', 'X-Export-Sheets': '48',
    } })
  })
  let clicked = false
  const link = { click() { clicked = true }, remove() {} }
  const original = globalThis.document
  globalThis.document = { createElement: () => link, body: { appendChild() {} } }
  t.after(() => { if (original === undefined) delete globalThis.document; else globalThis.document = original })
  t.mock.method(URL, 'createObjectURL', () => 'blob:test')
  t.mock.method(URL, 'revokeObjectURL', () => {})
  t.mock.method(globalThis, 'setTimeout', callback => { callback(); return 0 })
  const result = await downloadReferenceAccounting({ hours: {} })
  assert.equal(clicked, true)
  assert.deepEqual(result, { filename: 'hours.xlsx', groups: 46, sheets: 48 })
  assert.equal(calls.length, 1)
  assert.equal(calls[0].url, '/export/accounting.xlsx')
  assert.equal(calls[0].options.credentials, 'same-origin')
  assert.equal(calls[0].options.method, 'POST')
})

test('reference download reports an authentication failure without starting a local export', async t => {
  t.mock.method(globalThis, 'fetch', async () => new Response(JSON.stringify({ message: 'Требуется вход' }), {
    status: 401, headers: { 'Content-Type': 'application/json' },
  }))
  await assert.rejects(downloadReferenceAccounting({ hours: {} }), /Требуется вход/)
})
