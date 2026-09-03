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

const projectRoot = path.resolve(import.meta.dirname, '..')
const inputPath = path.resolve(process.argv[2] || path.join(projectRoot, 'outputs', 'frontend-export-check.xlsx'))
const templatePath = path.join(projectRoot, 'frontend', 'public', 'templates', 'schedule-template.xlsx')
const previewDir = path.resolve(process.argv[3] || path.join(
  projectRoot,
  'outputs',
  '01a0657b-9924-7df3-a37f-2d7d021e8aa9',
  'frontend_export_previews',
))

const workbook = await SpreadsheetFile.importXlsx(await FileBlob.load(inputPath))
const template = await SpreadsheetFile.importXlsx(await FileBlob.load(templatePath))
const sheetWidths = { '1 курс': 'N', '2 курс': 'O', '3 курс': 'O', '4 курс': 'N' }
let placedCells = 0

await fs.mkdir(previewDir, { recursive: true })
for (const sheet of workbook.worksheets.items) {
  const endColumn = sheetWidths[sheet.name]
  if (!endColumn) throw new Error(`Неожиданный лист: ${sheet.name}`)
  const sourceSheet = template.worksheets.getItem(sheet.name)
  const before = sourceSheet.getRange(`A1:${endColumn}61`).values
  const after = sheet.getRange(`A1:${endColumn}61`).values
  if (JSON.stringify(before) !== JSON.stringify(after)) throw new Error(`${sheet.name}: изменены дни до пятницы`)

  for (const firstRow of [63, 65, 67, 69, 71, 73, 75, 78, 80, 82, 84, 86, 88, 90]) {
    const values = sheet.getRange(`D${firstRow}:${endColumn}${firstRow + 1}`).values
    for (const row of values) {
      for (const value of row) if (value !== null && value !== '') placedCells += 1
    }
  }

  const preview = await workbook.render({ sheetName: sheet.name, autoCrop: 'all', scale: 1, format: 'png' })
  const safeName = sheet.name.replaceAll(/[\\/:*?"<>|]/g, '_')
  await fs.writeFile(path.join(previewDir, `${safeName}.png`), new Uint8Array(await preview.arrayBuffer()))
}

const formulaErrors = await workbook.inspect({
  kind: 'match',
  searchTerm: '#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A',
  options: { useRegex: true, maxResults: 100 },
  maxChars: 3000,
})
if (/#REF!|#DIV\/0!|#VALUE!|#NAME\?|#N\/A/.test(formulaErrors.ndjson)) {
  throw new Error(`Ошибки формул: ${formulaErrors.ndjson}`)
}
if (placedCells !== 349) throw new Error(`В Excel найдено ${placedCells} занятий вместо 349`)

console.log(JSON.stringify({
  ok: true,
  inputPath,
  sheets: workbook.worksheets.items.map(sheet => sheet.name),
  placedCells,
  earlierDaysPreserved: true,
  formulaErrors: 0,
  previewDir,
}, null, 2))
