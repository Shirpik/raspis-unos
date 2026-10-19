import JSZip from 'jszip'

// ExcelJS writes every note in a fixed 98 x 59 pt box. Resize the exported
// note shapes so their date lists can be read, including in desktop Excel.
export async function writeReferenceAccountingBuffer(workbook) {
  const zip=await JSZip.loadAsync(await workbook.xlsx.writeBuffer())
  for(const file of Object.keys(zip.files).filter(name=>/\/vmlDrawing\d+\.vml$/.test(name))){
    const sheet=workbook.getWorksheet(Number(file.match(/vmlDrawing(\d+)/)[1]))
    if(!sheet)continue
    const xml=await zip.file(file).async('string')
    zip.file(file,xml.replace(/<v:shape\b[^>]*>[\s\S]*?<\/v:shape>/g,shape=>{
      const row=Number(shape.match(/<x:Row>(\d+)<\/x:Row>/)?.[1]),col=Number(shape.match(/<x:Column>(\d+)<\/x:Column>/)?.[1])
      const note=sheet.getCell(row+1,col+1).note
      const text=typeof note==='string'?note:(note?.texts||[]).map(item=>item.text).join('')
      if(!text)return shape
      // Excel's default 11 pt comment font needs about 15 pt per line.
      // Its VML anchor overrides the style height, and worksheet rows can
      // have different heights. Convert points using the actual row sizes.
      const lines=text.split('\n').length,width=col===2?285:520,height=Math.max(65,lines*16+32)
      let endRow=row,remaining=height+1.5
      while(remaining>=(sheet.getRow(endRow+1).height??sheet.properties.defaultRowHeight??15)){
        remaining-=sheet.getRow(endRow+1).height??sheet.properties.defaultRowHeight??15
        endRow++
      }
      const endOffset=Math.ceil(remaining*4/3)
      return shape.replace(/width:[\d.]+pt;height:[\d.]+pt/,`width:${width}pt;height:${height}pt`)
        .replace(/<x:Anchor>[\s\S]*?<\/x:Anchor>/,`<x:Anchor>${col+1}, 15, ${row}, 2, ${col+6}, 15, ${endRow}, ${endOffset}</x:Anchor>`)
    }))
  }
  return zip.generateAsync({type:'uint8array',compression:'DEFLATE'})
}
