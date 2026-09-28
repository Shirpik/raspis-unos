# Production deployment

The existing host nginx remains unchanged. It serves the frontend and proxies
`/api/` to the application published only on `127.0.0.1:8080`.

Before the first build, place the OR-Tools 9.15 Linux archive at
`deploy/or-tools.tar.gz` and create `deploy/.env` containing a strong value for
`POSTGRES_PASSWORD`. Then run from the repository root:

```bash
docker compose --env-file deploy/.env -f deploy/compose.yaml build
docker compose --env-file deploy/.env -f deploy/compose.yaml up -d
```

On its first database-backed startup the application imports the existing
`data/timetable_data.json` into PostgreSQL. The JSON file is retained as a
rollback source, but PostgreSQL is authoritative while
`TIMETABLE_DATABASE_URL` is set. Application revisions are stored in
`timetable_state_history`.

The backup service creates and validates a compressed PostgreSQL dump every
day in `deploy/backups` and deletes files only after 210 days. Copy this folder
to separate storage for protection against total server or disk loss.
