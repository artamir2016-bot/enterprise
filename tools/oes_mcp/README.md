# OES data MCP — доступ модели к данным базы OES

MCP-сервер, дающий модели **чтение данных инфобазы OES** через настоящий движок
запросов (метаданные: каталоги, документы, ссылки, dot-walk — не сырой SQL по
физическим таблицам).

## Архитектура

```
  модель ──MCP(stdio)──> tools/oes_mcp/server.py ──stdin/stdout JSON──> oes_query.exe
                                                                          │ (backend.dll)
                                                                   открывает ФАЙЛОВУЮ базу
                                                                   + активные метаданные
                                                                   + живую сессию
```

- **`oes_query`** (`tools/oes_query.cpp`, цель CMake `oes_query`) — headless-движок:
  `oes_query --file <baseDir>` открывает базу, печатает `READY`, затем на каждую
  строку stdin (`{"q":"...","limit":N}` или `{"cmd":"meta"}`) отвечает одной
  строкой JSON. Запрос исполняется через `ibQueryParser` → `ibQueryLowering::ExecutePackage`.
- **`server.py`** — FastMCP-сервер: держит один процесс `oes_query` на базу и
  отдаёт инструменты модели.

## Инструменты

- **`oes_run_query(query, limit=100)`** → `{"columns":[...],"rows":[[...]],"count":N}`
  или `{"error":"..."}`. `query` — текст на языке запросов OES, напр.:
  `SELECT Ref, Description FROM Catalog.Компоненты`.
- **`oes_list_objects()`** → `{"objects":[{"name":"...","kind":"Catalog"|"Document"}]}`
  — что можно запрашивать.

## Сборка и запуск

```bat
cmake --build build-perf --target oes_query
set OES_BASE=E:\Projects\OES\testbase\devtracker_base
set OES_BIN=E:\Projects\OES\build-perf\bin\Release
python tools\oes_mcp\server.py          REM обычно запускает MCP-клиент, не вручную
```

## Регистрация (пример конфигурации MCP)

```json
{
  "mcpServers": {
    "oes-data": {
      "command": "C:\\Users\\User\\.pyenv\\pyenv-win\\versions\\3.12.10\\python.exe",
      "args": ["E:\\Projects\\OES\\tools\\oes_mcp\\server.py"],
      "env": {
        "OES_BASE": "E:\\Projects\\OES\\testbase\\devtracker_base",
        "OES_BIN":  "E:\\Projects\\OES\\build-perf\\bin\\Release"
      }
    }
  }
}
```

## Статус

Работает сквозняком: `oes_list_objects` перечисляет объекты; `oes_run_query`
читает **реальные строки** (проверено на базе devtracker_base с записями,
созданными в предприятии — каталог Компоненты вернул 3 строки с кириллицей).
Ключ к headless-чтению: запрос исполняется НА ВОРКЕРЕ сессии
(`ibWorkerPool::RunOnSession`), где жив per-session runtime — на голом главном
потоке скан таблиц возвращал пусто. Файловая Firebird-база — один процесс: держи
её открытой ЛИБО в предприятии, ЛИБО в oes_query, не одновременно.

## Границы (пока)

- **Только чтение** (SELECT). Запись/проведение — не через этот сервер.
- Параметры `&Имя` не биндятся (значения подставляются в текст запроса вызывающей
  стороной). Биндинг параметров — следующий инкремент.
- Одна база на процесс сервера (задаётся `OES_BASE`). Для другой базы — свой
  инстанс с другим env.
- Файловая база (Firebird embedded). Серверные базы — следующий инкремент
  (добавить `--server/--db/--usr/--pwd` в `oes_query`).
