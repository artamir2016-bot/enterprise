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

## Status — ALL MERGED into `feature/import-forms`

- [x] 1 — Cyrillic identifiers (P0) — verified end-to-end; lexer already lexes Cyrillic
      (iswalpha). `RuntimeTest.CyrillicIdentifiers`. Merge: OES-RU P0.
- [x] 2 — Russian keywords (P1) — `s_ruKeyWordAlias` maps ~35 spellings to their KEY_*;
      boolean value-keyword now set by KEY not spelling (Истина/Ложь). Two-word
      «Для Каждого» → one-word «ДляКаждого» (import still normalises the two-word form).
      `RuntimeTest.RussianKeywords_CES/_VES_IfBlock`. Merge: OES-RU P1.
- [x] 3 — Russian system functions (P2) — 64 aliases via `ibMemberTable::AliasMethod`
      (name→same method number, no new dispatch case); compiler enumerates the alias list
      for global-function resolution. `BuiltInRuntime.RussianSystemFunction_StrLen`.
      Merge: OES-RU P2.
- [x] 4 — Russian object methods (P3) — Array + Map via the same AliasMethod mechanism;
      obj.Method() resolves at runtime through FindMethod's alias fallback (no compiler
      change). `BuiltInRuntime.RussianObjectMethod_Array`. Merge: OES-RU P3.

### Follow-ups (not done)
- Object-method aliases beyond Array/Map (Structure, ValueTable, DynamicList, …) — same
  recipe: add `helper.AliasMethod(...)` lines to each type's `_BindNames`.
- Two-word 1C keyword forms («Для Каждого») as a native single token would need a lexer /
  parser tweak; today the import translator normalises them.
- Russian names in the syntax highlighter / autocomplete surface (cosmetic).
