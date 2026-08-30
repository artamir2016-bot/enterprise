# Data composition (СКД) — engine

Status: **core built** (`ibDataComposer`, `ibCompositionRenderer`), covered by
`tests/test_dataComposer.cpp`, renders into our spreadsheet and exports to `.xlsx`.

## What this is

A data-composition engine of the kind a 1C developer knows as *СКД* (Система
Компоновки Данных): a declarative report where the author says **what** to read and
**how to group/aggregate/order** it, and the engine produces a grouped, totalled
result — not hand-written per-report code.

## Provenance and licence

- The **architecture** is adapted from the report engine of **onebase**
  (github.com/ivanarama/onebase), which is **MIT-licensed** — permissive, so its
  design may be studied and re-implemented, with attribution (see `NOTICE.md`). No
  onebase source is copied; this is an independent C++ implementation over our own
  `ibValue` / `ibNumber` / `ibSpreadsheetDescription` types.
- The upstream OES composition engine (PolyForm Noncommercial) was used **only for
  high-level ideas** — no source read or ported.

## The idea (analysed from onebase)

A composition splits into two things kept apart:

1. **Schema** (the blueprint, authored once): the query/rows to read; ordered
   **groupings** (hierarchical); **measures** (fields that are aggregated, each with
   an aggregate kind); ordering; and — in the full model — computed measures,
   filters, parameters, conditional appearance, and named *variants*.
2. **Settings** (the user's choices at run time): which groupings/measures are on,
   filter values, the selected variant. Kept separate so a user arrangement can be
   saved without touching the schema.

The **core algorithm** (`onebase` `report/compose`, ported here):

```
Compose(rows, schema):
  groups = BuildGroups(rows, level = 0)
  grand  = Aggregate(all rows)               # grand total

BuildGroups(rows, level):
  if level == len(schema.groupings): return []    # leaf
  field = schema.groupings[level]
  for each row: bucket[ key(row[field]) ].append(row)   # first-seen order
  for each bucket b:
     g.key       = b.keyValue
     g.subtotals = Aggregate(b.rows)                     # per-group aggregation
     g.children  = BuildGroups(b.rows, level + 1)        # recurse (nested groupings)
     (or g.details = b.rows  at the leaf, if requested)
  sort groups by schema.sort
```

`Aggregate` walks the measures: `Sum`/`Avg` accumulate exactly through `ibNumber`,
`Count` is the row count, `Min`/`Max` compare values. Group keys are rendered to a
stable string so rows with the same value bucket together regardless of the driver's
returned type.

## Our implementation

`src/engine/backend/dataComposer/`

- **`dataComposer.h/.cpp`** — the schema, the group/result tree, and the composer:
  - `ibComposeRow` — one source row (field → `ibValue`, case-insensitive lookup).
  - `ibCompositionSchema` — `m_groupings`, `m_measures` (`ibCompositionMeasure`:
    field + `ibAggregate` + title), `m_sort`, `m_grandTotal`, `m_detail`.
  - `ibCompositionResult` / `ibCompositionGroup` — the tree (`m_key`, `m_count`,
    `m_subtotals`, `m_children`, `m_details`) + `m_grandTotal`.
  - `ibDataComposer::Compose(rows, schema)` — the algorithm above.
- **`compositionRenderer.h/.cpp`** — `ibCompositionRenderer::Render(result, schema,
  out)` turns the tree into an `ibSpreadsheetDescription`: a header row, one row per
  group (key indented by nesting level, subtotals in the measure columns, bold),
  optional detail rows, and a bold `Total` row. Because the output is our spreadsheet
  document, a composed report **exports to Excel via `ibXlsxExporter` with no extra
  step** (compose → spreadsheet → xlsx).

The core is **DB-free**: it composes rows a caller already produced (query engine,
script, or test). That is what makes the grouping/aggregation testable in isolation
and is why the core tests need no database.

- **`compositionSource.h/.cpp`** — the bridge to the query engine (the one file that
  knows about the database layer): `ibCompositionSource::RowsFromResultSet(rs)`
  drains a driver result set (`ibDatabaseResultSet` from
  `ibDatabaseLayer::RunQueryWithResults` / a prepared statement) into typed rows
  (numbers exact via `ibNumber`, NULL → empty), and `Compose(rs, schema)` drains +
  composes in one call. Covered by a live in-memory SQLite test.

## Follow-ups (the rest of a full СКД)

Built deliberately as a bounded, tested core. The remaining pieces, each its own step:

1. ~~Query wiring~~ — **done** (`ibCompositionSource`). Next: a schema that names a
   query *text*/source and runs it through the L4 query language, rather than the
   caller executing the query and handing over the result set.
2. **Computed measures** — a measure with an expression over other measures
   (dependency-ordered evaluation), via our script/expression evaluator.
3. **Filters & parameters** — declared parameters substituted into the query;
   user row filters (`eq/ne/gt/…/contains`) applied before grouping.
4. **Cross-tabulation** — column dimensions (a pivot), producing a second axis.
5. **Conditional appearance** — per-row/cell styling rules evaluated on subtotals.
6. **A Composer metaobject + settings persistence + designer UI** — so a report is
   metadata with saved variants and a settings form, not only a C++ call.
