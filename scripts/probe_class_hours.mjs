import fs from 'node:fs'
import { execFileSync } from 'node:child_process'
const base = JSON.parse(fs.readFileSync('.tmp/google-sheets/nolab6-base.json', 'utf8'))
const only = !process.argv[2] || process.argv[2] === 'all' ? null : Number(process.argv[2])
const upto = process.argv[3] == null ? null : Number(process.argv[3])
for (const group of base.groups) group.class_hour_enabled = false
for (const group of base.groups) {
  if ([1, 12].includes(group.curator_teacher)) continue
  if (only != null && group.id !== only) continue
  if (upto != null && group.id > upto) continue
  const d = structuredClone(base)
  d.groups.find(g => g.id === group.id).class_hour_enabled = true
  fs.writeFileSync('data/timetable_data.json', JSON.stringify(d))
  const output = `output/ch-probe-${group.id}`
  try { execFileSync('.tmp/audit-build-clean/Release/timetable_solver.exe', ['--generate', '--draft-semester', '--output', output, '--locks', '.tmp/google-sheets/locks-empty.json'], { stdio: 'ignore' }) } catch {}
  const metricsPath = `${output}/solver_metrics.json`
  const ok = fs.existsSync(metricsPath)
  console.log(group.id, ok ? 'OK' : 'FAIL', group.name, group.curator_teacher)
}
