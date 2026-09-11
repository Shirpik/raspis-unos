#!/usr/bin/env node
import fs from 'node:fs/promises'
import path from 'node:path'
import { buildReferenceAccountingWorkbook } from '../frontend/src/utils/referenceAccountingExport.js'

const input = path.resolve(process.argv[2] || 'outputs/finalization-2026-09-08/hours-report-current.json')
const output = path.resolve(process.argv[3] || 'outputs/finalization-2026-09-08/Вычетка_часов_на_08.09.2026.xlsx')
const hours = JSON.parse(await fs.readFile(input, 'utf8'))
const { workbook } = buildReferenceAccountingWorkbook(hours)
await fs.mkdir(path.dirname(output), { recursive: true })
await workbook.xlsx.writeFile(output)
console.log(output)
