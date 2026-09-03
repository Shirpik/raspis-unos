import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import path from 'node:path'
import ExcelJS from '../frontend/node_modules/exceljs/excel.js'
import { buildScheduleExcelWorkbook, scheduleExcelFilename } from '../frontend/src/utils/scheduleTemplateExport.js'

const projectRoot = path.resolve(import.meta.dirname, '..')
const templatePath = path.join(projectRoot, 'frontend', 'public', 'templates', 'schedule-template.xlsx')
const schedulePath = path.join(projectRoot, 'output', 'latest', 'schedule_all.json')
const outputDir = path.join(projectRoot, 'outputs', '01a0657b-9924-7df3-a37f-2d7d021e8aa9')
const outputPath = path.join(outputDir, 'frontend-export-check.xlsx')

const schedule = JSON.parse(await fs.readFile(schedulePath, 'utf8'))
const templateBytes = await fs.readFile(templatePath)
const result = await buildScheduleExcelWorkbook(schedule, templateBytes)
const generatedBytes = await result.workbook.xlsx.writeBuffer()
await fs.mkdir(outputDir, { recursive: true })
await fs.writeFile(outputPath, generatedBytes)

assert.equal(result.insertedLessons, 349)
assert.equal(result.groups, 46)
assert.equal(result.sheets, 4)
assert.equal(result.dates, 2)
assert.equal(scheduleExcelFilename(schedule), 'Расписание_04-05.09.2026.xlsx')

const sourceBook = new ExcelJS.Workbook()
await sourceBook.xlsx.load(templateBytes)
const generatedBook = new ExcelJS.Workbook()
await generatedBook.xlsx.load(generatedBytes)
assert.deepEqual(generatedBook.worksheets.map(sheet => sheet.name), ['1 курс', '2 курс', '3 курс', '4 курс'])

const scheduleGroups = new Map(schedule.groups.map(group => [group.group_name, group]))
const cellText = cell => cell.value === null || cell.value === undefined ? '' : String(cell.value)
let verifiedLessons = 0

for (const sheet of generatedBook.worksheets) {
  const sourceSheet = sourceBook.getWorksheet(sheet.name)
  assert.ok(sourceSheet)

  // The supplied template already contains the approved earlier days. A
  // Friday/Saturday export must not rewrite them.
  for (let row = 1; row <= 61; row += 1) {
    for (let column = 1; column <= sheet.columnCount; column += 1) {
      assert.deepEqual(sheet.getCell(row, column).value, sourceSheet.getCell(row, column).value,
        `${sheet.name}!${sheet.getCell(row, column).address} changed before Friday`)
    }
  }

  const groupColumns = []
  for (let column = 4; column <= sheet.columnCount; column += 1) {
    const groupName = sheet.getCell(62, column).text.trim()
    if (!groupName) break
    groupColumns.push({ groupName, column })
  }

  for (const { groupName, column } of groupColumns) {
    const group = scheduleGroups.get(groupName)
    assert.ok(group, `${groupName} is absent from the schedule`)

    for (const day of group.days) {
      const firstLessonRow = day.weekday === 'ПТ' ? 63 : day.weekday === 'СБ' ? 78 : null
      assert.ok(firstLessonRow, `Unexpected day ${day.weekday}`)

      for (const slot of day.slots) {
        const first = sheet.getCell(firstLessonRow + (slot.slot - 1) * 2, column)
        const second = sheet.getCell(first.row + 1, column)
        const lessons = slot.lessons || []
        if (!lessons.length) {
          assert.equal(cellText(first), '')
          assert.equal(cellText(second), '')
          assert.equal(first.isMerged, true)
          continue
        }

        for (const lesson of lessons) {
          const ordinal = lesson.subgroup < 0 ? null : ((lesson.subgroup - group.group_index * 2 + 2) % 2) + 1
          const target = ordinal === 2 ? second : first
          assert.match(cellText(target), new RegExp(lesson.name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')))
          if (lesson.room_name !== null && lesson.room_name !== undefined) {
            assert.match(cellText(target), new RegExp(String(lesson.room_name).replace(/[.*+?^${}()|[\]\\]/g, '\\$&')))
          }
          verifiedLessons += 1
        }
      }
    }
  }

  for (let row = 63; row <= 91; row += 1) {
    for (let column = 4; column <= sheet.columnCount; column += 1) {
      assert.doesNotMatch(cellText(sheet.getCell(row, column)), /Колтышев|Письмак/i)
    }
  }
}

assert.equal(verifiedLessons, 349)
console.log(JSON.stringify({
  ok: true,
  outputPath,
  filename: scheduleExcelFilename(schedule),
  lessons: verifiedLessons,
  groups: result.groups,
  sheets: result.sheets,
  earlierDaysPreserved: true,
}, null, 2))
