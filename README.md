# Сайт генерации расписания

Проект состоит из Vue-сайта и C++/OR-Tools backend. Поддерживаемый пользовательский интерфейс — браузер.

## Запуск сайта

Соберите frontend и backend, затем запустите единый веб-сервер:

```powershell
cd frontend
npm install
npm run build
cd ..
cmake -S . -B build -DCMAKE_PREFIX_PATH=<путь-к-OR-Tools>
cmake --build build --config Release --target timetable_solver
powershell -ExecutionPolicy Bypass -File scripts/start_site.ps1 -BackendPath build/Release/timetable_solver.exe
```

Сайт откроется по адресу `http://127.0.0.1:4173`. Веб-сервер запускает backend на `127.0.0.1:8080` и передаёт ему запросы `/api`. Данные остаются в `data/timetable_data.json`, а результаты — в `output/`.

Порядок импорта данных, настройки ограничений, генерации и публикации описан в [инструкции диспетчера](docs/OPERATION_GUIDE.md). Технические требования и критерии приёмки находятся в [техническом задании](docs/FINALIZATION_TZ.md).

Для разработки запустите backend с аргументом `8080`, затем `npm run dev` в `frontend`.

## Структура

```text
include/
  config.h          # константы, имена групп/преподавателей
  types.h           # Date, Lesson, BlockInfo, Campus
  date_utils.h      # даты, недели, интервалы пар и УП
  format_utils.h    # строки, CSV, UTF-8 BOM, чтение значений CP-SAT
  model_utils.h     # вспомогательные ограничения и метрики
  diagnostics.h     # проверка входных данных и диагностика
  output_writers.h  # запись txt/csv расписаний
  lessons_data.h    # создание списка занятий
  scheduler.h       # запуск построения модели и решателя

src/
  *.cpp             # реализации модулей
```

## Сборка backend

Пример:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/or-tools
cmake --build build -j
./build/timetable_solver
```

Если OR-Tools подключён иначе, оставь свои текущие настройки сборки и просто добавь все `src/*.cpp` в проект, а `include/` — в include directories.
