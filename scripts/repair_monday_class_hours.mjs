import fs from 'node:fs'
const base = 'output/week-2026-09-07-v2'
const source = `${base}/output/unlocked`
const target = `${base}/output/monday-repaired`
if (fs.existsSync(target)) throw new Error('Candidate already exists')
fs.cpSync(source, target, { recursive: true })
const s = JSON.parse(fs.readFileSync(`${target}/schedule_all.json`, 'utf8'))
for (const [gid, id, from, to] of [[13, 268, 1, 3], [24, 523, 4, 7]]) {
  const monday = s.groups.find(g => g.group_index === gid).days.find(d => d.date_iso === '2026-09-07')
  const a = monday.slots.find(s => s.slot === from), b = monday.slots.find(s => s.slot === to)
  if (a.lessons.length !== 1 || a.lessons[0].id !== id || b.lessons.length) throw new Error('Unexpected source cell')
  b.lessons = a.lessons; b.text = a.text
  a.lessons = []; a.text = '-'
}
fs.writeFileSync(`${target}/schedule_all.json`, JSON.stringify(s, null, 2))
console.log(target)
