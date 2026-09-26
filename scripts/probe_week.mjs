import fs from 'node:fs'
import { execFileSync } from 'node:child_process'

const base = JSON.parse(fs.readFileSync('.tmp/google-sheets/week-20260928-candidate.json', 'utf8'))
for (const group of base.groups) group.class_hour_enabled = false
const ordinary = base.lessons.filter(l => !l.is_block && !l.is_pp && l.total_slots > 0)
for (let n = 1; n <= ordinary.length; n += 1) {
  const d = structuredClone(base)
  for (const lesson of d.lessons) { lesson.total_slots = 0; lesson.generation_active = false }
  for (const source of ordinary.slice(0, n)) {
    const lesson = d.lessons.find(x => x.id === source.id)
    lesson.total_slots = source.total_slots
    lesson.generation_active = true
  }
  fs.writeFileSync('.tmp/google-sheets/probe.json', JSON.stringify(d))
  fs.copyFileSync('.tmp/google-sheets/probe.json', 'data/timetable_data.json')
  let out = ''
  const output = `output/probe2-${n}`
  try {
    out = execFileSync('.tmp/audit-build-clean/Release/timetable_solver.exe', ['--generate', '--draft-semester', '--output', output, '--locks', '.tmp/google-sheets/locks-empty.json'], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] })
  } catch (error) { out = (error.stdout || '') + (error.stderr || '') }
  const metrics = fs.existsSync(`${output}/solver_metrics.json`) ? JSON.parse(fs.readFileSync(`${output}/solver_metrics.json`)) : null
  console.log(n, metrics?.weeks?.[0]?.status || 'FAIL', ordinary[n - 1]?.id, ordinary[n - 1]?.name)
}
