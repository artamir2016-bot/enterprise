# Russian business logic — fork feature plan

> **FORK FEATURE (not upstream).** This document and every change it references were made on
> the `artamir2016-bot/enterprise` fork to let 1C-style configurations be written and imported
> in Russian. Each change is marked in-code with an `OES-RU:` comment so a diff against upstream
> shows exactly what the fork added and collisions are easy to resolve.

## Goal

Allow business logic to be described in Russian, natively, so imported 1C (BSL) modules run with
minimal / no lossy dictionary translation:

1. **Cyrillic identifiers** — variable / function / attribute names and metadata references in
   Cyrillic (`Валюты`, `СуммаНДС`).
2. **Russian keywords** — `Если / Тогда / КонецЕсли / Для / Пока / Функция …` as aliases of the
   English `KEY_*` set.
3. **Russian system functions** — `Сообщить / СтрДлина / Окр …` as aliases of the built-in
   English names.
4. **Russian object methods** — method dispatch by a Russian name alias.

## Subtasks, priority, branches

| # | Priority | Branch | Scope |
|---|---|---|---|
| 1 | **P0** (foundation) | `feature/ru-lang-identifiers` | Verify + guarantee Cyrillic identifiers compile/run end-to-end (lexer + C-runtime locale). Nothing else matters if names don't lex. |
| 2 | **P1** | `feature/ru-lang-keywords` | Russian keyword spellings as aliases → same `KEY_*` index. Style gate + syntax highlight. |
| 3 | **P2** | `feature/ru-lang-sysfunc` | Russian aliases for the ~98 built-in functions/procedures in `ibSystemManager`. |
| 4 | **P3** | `feature/ru-lang-methods` | Russian aliases for object method dispatch (per-type, largest surface). |

Each subtask: branch from `feature/import-forms`, implement + test, then merge back into
`feature/import-forms` (the fork's working branch) with `--no-ff` so the subtask is a visible unit.

## Marking convention

Every fork edit carries a comment beginning `OES-RU:` explaining WHAT and WHY, so that when this
branch is compared with the upstream file the added lines are self-describing.

## Status

- [ ] 1 — Cyrillic identifiers (P0)
- [ ] 2 — Russian keywords (P1)
- [ ] 3 — Russian system functions (P2)
- [ ] 4 — Russian object methods (P3)
