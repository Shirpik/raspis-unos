#!/usr/bin/env node

import { readFile, mkdir, writeFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

import {
  createSchedulePdf,
  schedulePdfFilename,
} from '../frontend/src/utils/scheduleExport.js'

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url))
const repositoryRoot = path.resolve(scriptDirectory, '..')

function parseArguments(argv) {
  const options = { input: null, output: null }
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index]
    if (argument === '--help' || argument === '-h') {
      options.help = true
      continue
    }
    if (argument !== '--input' && argument !== '--output') {
      throw new Error(`Неизвестный аргумент: ${argument}`)
    }
    const value = argv[index + 1]
    if (!value || value.startsWith('--')) {
      throw new Error(`Для ${argument} нужен путь`)
    }
    options[argument.slice(2)] = value
    index += 1
  }
  return options
}

function usage() {
  return [
    'Экспорт итогового расписания в PDF в том же формате, что и кнопка на сайте.',
    '',
    'node scripts/export_schedule_pdf.mjs [--input <schedule_all.json>] [--output <file-or-directory>]',
    '',
    'По умолчанию:',
    '  --input  output/latest/schedule_all.json',
    '  --output output/pdf/',
  ].join('\n')
}

function resolveFromRepository(value) {
  return path.isAbsolute(value) ? path.normalize(value) : path.resolve(repositoryRoot, value)
}

function resolveOutputPath(value, filename) {
  if (!value) return path.join(repositoryRoot, 'output', 'pdf', filename)
  const resolved = resolveFromRepository(value)
  return path.extname(resolved).toLowerCase() === '.pdf'
    ? resolved
    : path.join(resolved, filename)
}

async function main() {
  const options = parseArguments(process.argv.slice(2))
  if (options.help) {
    process.stdout.write(`${usage()}\n`)
    return
  }

  const inputPath = resolveFromRepository(options.input || path.join('output', 'latest', 'schedule_all.json'))
  const schedule = JSON.parse(await readFile(inputPath, 'utf8'))
  if (!Array.isArray(schedule?.groups) || schedule.groups.length === 0) {
    throw new Error(`В ${inputPath} нет групп для экспорта`)
  }

  const filename = schedulePdfFilename(schedule)
  const outputPath = resolveOutputPath(options.output, filename)
  const pdfBuffer = await createSchedulePdf(schedule).getBuffer()

  await mkdir(path.dirname(outputPath), { recursive: true })
  await writeFile(outputPath, Buffer.from(pdfBuffer))

  process.stdout.write(`${JSON.stringify({
    input: inputPath,
    output: outputPath,
    groups: schedule.groups.length,
    bytes: pdfBuffer.length,
  }, null, 2)}\n`)
}

main().catch(error => {
  process.stderr.write(`Ошибка экспорта PDF: ${error.message}\n`)
  process.exitCode = 1
})
