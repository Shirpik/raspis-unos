// Extract presentation only; historical students, teachers and hours are not bundled.
import fs from 'node:fs/promises'
import crypto from 'node:crypto'
import ExcelJS from '../frontend/node_modules/exceljs/excel.js'

const source = process.argv[2]
if (!source) throw new Error('Pass the original accounting XLSX')
const bytes = await fs.readFile(source)
const book = new ExcelJS.Workbook()
await book.xlsx.load(bytes)
const copy = value => JSON.parse(JSON.stringify(value))
function sheetStyle(sheet, weekCol, totalCol, dataRow, footerRow) {
  const cols = [...Array(weekCol - 1)].map((_, i) => sheet.getColumn(i+1).width || 8.43)
  const cells = []
  for (let row=1;row<dataRow;row++) for(let col=1;col<weekCol;col++) {
    const cell=sheet.getCell(row,col)
    cells.push({row,col,style:copy(cell.style),value:cell.type===ExcelJS.ValueType.Merge?null:cell.value})
  }
  return { cols, weekWidth:sheet.getColumn(weekCol).width, metaWidths:[0,1,2,3,4].map(i=>sheet.getColumn(totalCol+i).width),
    rowHeights:[...Array(dataRow)].map((_,i)=>sheet.getRow(i+1).height || sheet.properties.defaultRowHeight || 15),
    fixedCells:cells,
    weekStyles:[...Array(dataRow)].map((_,i)=>copy(sheet.getCell(i+1,weekCol).style)),
    fixedDataStyles:[...Array(weekCol-1)].map((_,i)=>copy(sheet.getCell(dataRow,i+1).style)),
    metaStyles:[...Array(dataRow)].map((_,i)=>[0,1,2,3,4].map(j=>copy(sheet.getCell(i+1,totalCol+j).style))),
    footerStyles:[0,1,2].map(i=>[...Array(totalCol+4)].map((_,j)=>copy(sheet.getCell(footerRow+i,j+1).style))),
    merges:sheet.model.merges.filter(range=>Number(range.match(/\d+$/)?.[0])<dataRow),
    pageSetup:copy(sheet.pageSetup),properties:copy(sheet.properties),views:copy(sheet.views),
  }
}
const template={
  source:'_Учёт часов в СПО 2 семестр (1) (1).xlsx',sha256:crypto.createHash('sha256').update(bytes).digest('hex'),
  summary:sheetStyle(book.getWorksheet('пр'),9,33,3,72),
  group:sheetStyle(book.getWorksheet('ТМ-1417'),10,34,10,29),
  schedule:sheetStyle(book.getWorksheet('Расписание'),1,25,5,4),
}
await fs.writeFile('frontend/src/utils/accountingTemplate.js','// Presentation extracted from the supplied workbook; no historical facts.\nexport default '+JSON.stringify(template,null,2)+'\n')
console.log(JSON.stringify({sha256:template.sha256,summaryMerges:template.summary.merges,groupMerges:template.group.merges,groupFonts:template.group.fixedDataStyles.map(s=>s.font?.name)}))
