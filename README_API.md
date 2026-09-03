# Timetable API

При запуске `.exe` поднимается локальный API:

```text
http://127.0.0.1:8080
```

Генерация расписания при запуске **не выполняется**. Она запускается только запросом:

```http
POST /api/schedule/regenerate
```

Данные для генератора лежат в отдельном файле:

```text
data/timetable_data.json
```

Текущая версия структуры данных — `schema_version: 3`. При первом запуске
старый файл автоматически дополняется рабочими периодами, настройками кабинетов,
заменами и корректировками учета часов. Перед изменением создается версия для
отката через `/api/versions`.

Результаты генерации лежат отдельно:

```text
output/latest/
```

`.gitignore` я не добавлял и не менял.

## Расписание

```http
POST /api/schedule/regenerate
GET  /api/schedule
GET  /api/schedule/group/0
GET  /api/schedule/group/ИСП-3304
```

## Полный файл данных

```http
GET /api/data
PUT /api/data
```

`PUT /api/data` полностью заменяет `data/timetable_data.json` телом запроса.

## Настройки дат семестра

```http
GET   /api/settings
PATCH /api/settings
PUT   /api/settings
```

Пример:

```powershell
Invoke-RestMethod -Method Patch `
  -Uri http://127.0.0.1:8080/api/settings `
  -ContentType "application/json; charset=utf-8" `
  -Body '{"start_date":"2026-01-12","end_date":"2026-06-19"}'
```

## Группы

```http
GET    /api/groups
POST   /api/groups
GET    /api/groups/{id}
PUT    /api/groups/{id}
PATCH  /api/groups/{id}
DELETE /api/groups/{id}
PATCH  /api/groups/bulk
```

Массовое изменение рабочих дат и пар:

```json
{
  "ids": [0, 1, 2],
  "patch": {
    "work_period": {"from": "2026-09-01", "to": "2026-12-30"},
    "work_days": [
      {"weekday": 1, "enabled": true, "start_slot": 2, "end_slot": 5},
      {"weekday": 2, "enabled": true, "start_slot": 1, "end_slot": 6}
    ]
  }
}
```

Вместо `ids` можно передать `"all": true`. Дни, которых нет в частичном
`work_days`, сохраняют прежние настройки.

Пример добавления группы:

```powershell
Invoke-RestMethod -Method Post `
  -Uri http://127.0.0.1:8080/api/groups `
  -ContentType "application/json; charset=utf-8" `
  -Body '{"name":"ИСП-3306","parts":2}'
```

## Преподаватели

```http
GET    /api/teachers
POST   /api/teachers
GET    /api/teachers/{id}
PUT    /api/teachers/{id}
PATCH  /api/teachers/{id}
DELETE /api/teachers/{id}
PATCH  /api/teachers/bulk
```

Пример добавления преподавателя:

```powershell
Invoke-RestMethod -Method Post `
  -Uri http://127.0.0.1:8080/api/teachers `
  -ContentType "application/json; charset=utf-8" `
  -Body '{"name":"Иванов"}'
```

Для преподавателя доступны дополнительные поля:

```json
{
  "default_room": 12,
  "campus_priority": [0, 1],
  "room_responsibility": "Лаборатория электротехники",
  "work_period": {"from": "2026-09-01", "to": "2026-12-30"},
  "work_days": [
    {"weekday": 1, "enabled": true, "start_slot": 1, "end_slot": 5}
  ]
}
```

`default_room` должен ссылаться на существующий кабинет. Приоритет площадок и
закрепленный кабинет являются мягкими предпочтениями; рабочее время — жестким
ограничением решателя.

## Пары / занятия

```http
GET    /api/lessons
POST   /api/lessons
GET    /api/lessons/{id}
PUT    /api/lessons/{id}
PATCH  /api/lessons/{id}
DELETE /api/lessons/{id}
```

Формат занятия:

```json
{
  "group": 0,
  "subgroup": -1,
  "teacher": 7,
  "total_slots": 12,
  "name": "Экономика",
  "subject_id": -1,
  "is_lab": false,
  "is_block": false,
  "plan_active": true,
  "allowed_campuses": [0, 1]
}
```

Пояснения:

```text
subgroup = -1  вся группа
subgroup = group_id * 2      первая подгруппа
subgroup = group_id * 2 + 1  вторая подгруппа
campus 0 = Лесная
campus 1 = Кривоусова
is_block = true для УП-блоков
plan_active = false оставляет строку в истории, но исключает ее из нового плана
```

Пример добавления пары:

```powershell
Invoke-RestMethod -Method Post `
  -Uri http://127.0.0.1:8080/api/lessons `
  -ContentType "application/json; charset=utf-8" `
  -Body '{"group":0,"subgroup":-1,"teacher":7,"total_slots":10,"name":"Новый предмет","subject_id":-1,"is_lab":false,"is_block":false,"allowed_campuses":[0,1]}'
```

## Недоступность групп

```http
GET    /api/unavailable
POST   /api/unavailable
GET    /api/unavailable/{id}
PUT    /api/unavailable/{id}
PATCH  /api/unavailable/{id}
DELETE /api/unavailable/{id}
```

Пример:

```powershell
Invoke-RestMethod -Method Post `
  -Uri http://127.0.0.1:8080/api/unavailable `
  -ContentType "application/json; charset=utf-8" `
  -Body '{"group":1,"from":"2026-03-20","to":"2026-03-30"}'
```

После любых изменений данных нужно вызвать:

```powershell
Invoke-RestMethod -Method Post http://127.0.0.1:8080/api/schedule/regenerate
```

## Недоступность групп / праздники

`/api/unavailable` теперь поддерживает:

- `dates`: массив конкретных дат, например `["2026-02-23", "2026-03-09"]`;
- `text`: текст, который попадёт в первую пару этого дня;
- `all_groups: true`: применить день ко всем группам;
- старые поля `group`, `from`, `to` тоже работают.

Пример для всех групп:

```powershell
Invoke-RestMethod -Method Post `
  -Uri http://127.0.0.1:8080/api/unavailable `
  -ContentType "application/json; charset=utf-8" `
  -Body '{"all_groups":true,"dates":["2026-05-01","2026-05-09"],"text":"Праздник"}'
```

Пример для одной группы:

```powershell
Invoke-RestMethod -Method Post `
  -Uri http://127.0.0.1:8080/api/unavailable `
  -ContentType "application/json; charset=utf-8" `
  -Body '{"group":0,"dates":["2026-03-20","2026-03-21"],"text":"Сборы"}'
```

После изменения вызови:

```powershell
Invoke-RestMethod -Method Post http://127.0.0.1:8080/api/schedule/regenerate
```

## Замены преподавателей

```http
GET    /api/substitutions
POST   /api/substitutions
GET    /api/substitutions/{uid}
PUT    /api/substitutions/{uid}
PATCH  /api/substitutions/{uid}
DELETE /api/substitutions/{uid}
GET    /api/accounting/substitutions.csv
```

Пример активной замены конкретной пары:

```json
{
  "date": "2026-09-10",
  "slot": 3,
  "lesson_id": 125,
  "absent_teacher": 7,
  "substitute_teacher": 18,
  "hours": 2,
  "status": "active",
  "reason": "Больничный",
  "comment": "Согласовано"
}
```

Активная замена вычитает часы у отсутствующего преподавателя и начисляет их
заменяющему. CSV предназначен для отдельного журнала замен в Excel.

## Учет часов и занятость

```http
GET /api/hours
GET /api/accounting/teacher-occupancy

GET    /api/accounting-adjustments
POST   /api/accounting-adjustments
PATCH  /api/accounting-adjustments/{uid}
DELETE /api/accounting-adjustments/{uid}
```

`/api/hours` возвращает по группам и преподавателям:

- плановые часы из активных вклеек;
- поставленные в расписание часы;
- зачтенные фактические часы с учетом замен и ручных корректировок;
- остаток и разбивку по учебным неделям.

`/api/accounting/teacher-occupancy` возвращает фактическую занятость по дате и
номеру пары. В интерфейсе эти записи выводятся матрицей
«преподаватель × день недели × пара».

## Проверка, профили решателя и управление генерацией

```http
GET  /api/audit
GET  /api/schedule/preflight
GET  /api/schedule/quota-balance
GET  /api/settings/solver-profiles
POST /api/settings/solver-profile/fast
POST /api/settings/solver-profile/balanced
POST /api/settings/solver-profile/final

POST /api/schedule/regenerate
GET  /api/schedule/progress
POST /api/schedule/cancel
GET  /api/schedule/quality
GET  /api/schedule/solver-metrics
```

Перед генерацией рекомендуется исправить ошибки из `/api/audit` и
`/api/schedule/preflight`. Предупреждения о вакансиях не блокируют запуск.

## История и откат данных

```http
GET  /api/versions
GET  /api/versions/{id}
POST /api/versions/{id}/restore
```

Импорт новой версии вклеек не удаляет старые занятия: отсутствующие строки
получают `plan_active: false`. История замен и фактических корректировок
сохраняется отдельно и остается главным источником факта после обновления плана.
