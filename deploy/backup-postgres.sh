#!/usr/bin/env bash
set -Eeuo pipefail

retention_days="${BACKUP_RETENTION_DAYS:-210}"

make_backup() {
  local stamp temporary target
  stamp="$(date -u +%Y-%m-%dT%H-%M-%SZ)"
  temporary="/backups/.timetable-${stamp}.dump.tmp"
  target="/backups/timetable-${stamp}.dump"

  pg_dump --format=custom --compress=6 --file="${temporary}"
  pg_restore --list "${temporary}" >/dev/null
  mv "${temporary}" "${target}"
  sha256sum "${target}" >"${target}.sha256"
  find /backups -type f \( -name 'timetable-*.dump' -o -name 'timetable-*.dump.sha256' \) \
    -mtime "+${retention_days}" -delete
  echo "[$(date -u +%FT%TZ)] verified backup created: ${target}"
}

while true; do
  make_backup
  sleep 86400 &
  wait $!
done
