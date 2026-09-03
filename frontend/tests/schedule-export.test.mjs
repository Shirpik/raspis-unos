import test from 'node:test'
import assert from 'node:assert/strict'

import {
  buildSchedulePdfDefinition,
  schedulePdfFilename,
} from '../src/utils/scheduleExport.js'

const schedule = {
  groups: [
    {
      group_index: 0,
      group_name: 'ТЕСТ-1203',
      days: [
        {
          date: '03.09.2026',
          weekday: 'ЧТ',
          slots: [
            {
              slot: 1,
              time: '1 пара (08:30-09:55)',
              text: 'Математика — 1 подгруппа, Иванов Иван Иванович, Лесная | Физика — 2 подгруппа, Петров Пётр Петрович, Лесная',
              lessons: [
                { name: 'Математика', teacher_name: 'Иванов Иван Иванович', room_name: '101', subgroup: 0 },
                { name: 'Физика', teacher_name: 'Петров Пётр Петрович', room_name: '102', subgroup: 1 },
              ],
            },
          ],
        },
        {
          date: '05.09.2026',
          weekday: 'СБ',
          slots: [{ slot: 1, time: '1 пара (08:30-09:45)', text: '-', lessons: [] }],
        },
      ],
    },
  ],
}

test('PDF builder preserves the website layout and structured subgroup entries', () => {
  const definition = buildSchedulePdfDefinition(schedule)

  assert.equal(schedulePdfFilename(schedule), 'Расписание_03-09-05-09_по_образцу.pdf')
  assert.equal(definition.pageOrientation, 'landscape')
  assert.equal(definition.pageSize, 'A2')
  assert.deepEqual(definition.pageMargins, [16, 24, 16, 20])
  assert.equal(definition.content.length, 2)

  const courseTable = definition.content[1]
  assert.equal(courseTable.pageBreak, undefined, 'the last course must not force an empty page')
  assert.equal(courseTable.table.widths.length, 4)

  const lessonCell = courseTable.table.body[1][3]
  assert.match(lessonCell.text, /1 п\/\u0433: Математика/)
  assert.match(lessonCell.text, /2 п\/\u0433: Физика/)
  assert.equal(lessonCell.fillColor, '#808080')
  assert.equal(lessonCell.color, '#FFFFFF')
  assert.deepEqual(definition.footer(2, 4), {
    text: '2 / 4',
    alignment: 'center',
    fontSize: 7,
    margin: [0, 0, 0, 6],
  })
})
