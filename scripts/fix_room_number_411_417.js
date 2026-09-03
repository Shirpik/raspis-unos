const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const archiveRoot = path.join(root, 'output', 'archive', 'room-number-fix-20260902');
const targets = [
  path.join(root, 'data', 'timetable_data.json'),
  path.join(root, 'output', 'scenarios'),
  path.join(root, 'output', 'latest'),
];

function jsonFiles(target) {
  if (!fs.existsSync(target)) return [];
  const stat = fs.statSync(target);
  if (stat.isFile()) return target.endsWith('.json') ? [target] : [];
  return fs.readdirSync(target, { withFileTypes: true }).flatMap((entry) =>
    jsonFiles(path.join(target, entry.name))
  );
}

function mutate(value, counters) {
  if (Array.isArray(value)) {
    value.forEach((item) => mutate(item, counters));
    return;
  }
  if (!value || typeof value !== 'object') return;

  // Room IDs and responsible teachers remain unchanged. Only the displayed
  // physical room number was entered in reverse in the source directory.
  if (value.id === 47 && value.name === '411') {
    value.name = '417';
    counters.master47 += 1;
  }
  if (value.id === 49 && value.name === '417') {
    value.name = '411';
    counters.master49 += 1;
  }
  if (value.room_id === 47 && value.room_name === '411') {
    value.room_name = '417';
    counters.assignment47 += 1;
  }
  if (value.room_id === 49 && value.room_name === '417') {
    value.room_name = '411';
    counters.assignment49 += 1;
  }
  if (value.requested_room_id === 47 && value.requested_room_name === '411') {
    value.requested_room_name = '417';
    counters.requested47 += 1;
  }
  if (value.requested_room_id === 49 && value.requested_room_name === '417') {
    value.requested_room_name = '411';
    counters.requested49 += 1;
  }

  Object.values(value).forEach((item) => mutate(item, counters));
}

const files = [...new Set(targets.flatMap(jsonFiles))];
const changed = [];
const totals = {
  master47: 0,
  master49: 0,
  assignment47: 0,
  assignment49: 0,
  requested47: 0,
  requested49: 0,
};

for (const file of files) {
  const original = fs.readFileSync(file, 'utf8');
  let data;
  try {
    data = JSON.parse(original);
  } catch {
    continue;
  }
  const counters = Object.fromEntries(Object.keys(totals).map((key) => [key, 0]));
  mutate(data, counters);
  const count = Object.values(counters).reduce((sum, item) => sum + item, 0);
  if (!count) continue;

  const relative = path.relative(root, file);
  const backup = path.join(archiveRoot, relative);
  fs.mkdirSync(path.dirname(backup), { recursive: true });
  if (!fs.existsSync(backup)) fs.copyFileSync(file, backup);

  const eol = original.includes('\r\n') ? '\r\n' : '\n';
  fs.writeFileSync(file, JSON.stringify(data, null, 2).replace(/\n/g, eol) + eol, 'utf8');
  for (const key of Object.keys(totals)) totals[key] += counters[key];
  changed.push({ file: relative, changes: count });
}

const summary = { archive: path.relative(root, archiveRoot), changed_files: changed, totals };
fs.mkdirSync(archiveRoot, { recursive: true });
fs.writeFileSync(
  path.join(archiveRoot, 'change_summary.json'),
  JSON.stringify(summary, null, 2) + '\n',
  'utf8'
);
process.stdout.write(JSON.stringify(summary, null, 2) + '\n');
