import fs from 'node:fs/promises'
import path from 'node:path'
import { pathToFileURL } from 'node:url'

const artifactModulePath = path.join(
  process.env.NODE_PATH || '',
  '@oai',
  'artifact-tool',
  'dist',
  'artifact_tool.mjs',
)
const { FileBlob, SpreadsheetFile } = await import(pathToFileURL(artifactModulePath))

const inputPath = path.resolve(process.argv[2])
const previewPath = path.resolve(process.argv[3])
const workbook = await SpreadsheetFile.importXlsx(await FileBlob.load(inputPath))
const sheetNames = workbook.worksheets.items.map(sheet => sheet.name)
const firstSheet = workbook.worksheets.items[0]

const overview = await workbook.inspect({
  kind: 'sheet',
  include: 'id,name',
  maxChars: 10000,
})
const sample = await workbook.inspect({
  kind: 'table',
  sheetId: firstSheet.name,
  range: 'A1:L24',
  include: 'values,formulas',
  tableMaxRows: 24,
  tableMaxCols: 12,
  maxChars: 12000,
})
const preview = await workbook.render({
  sheetName: firstSheet.name,
  range: 'A1:L24',
  scale: 1,
  format: 'png',
})
await fs.mkdir(path.dirname(previewPath), { recursive: true })
await fs.writeFile(previewPath, new Uint8Array(await preview.arrayBuffer()))

console.log(JSON.stringify({
  inputPath,
  sheetCount: sheetNames.length,
  sheetNames,
  overview: overview.ndjson,
  sample: sample.ndjson,
  previewPath,
}, null, 2))
