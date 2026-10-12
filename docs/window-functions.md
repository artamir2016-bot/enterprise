# Window functions — `… OVER (PARTITION BY … ORDER BY …)` (T1.2)

Clean-room reimplementation track (backlog **T1.2**,
[docs/upstream-reimpl-backlog.md](upstream-reimpl-backlog.md)). Spec written from our own
query-engine map + the observed goal; no upstream code is copied.

Companion maps: [query-engine-layers.md](query-engine-layers.md) (L1–L5 floor plan),
[query-pushdown.md](query-pushdown.md) (the sibling pushdown track, same parity discipline),
[database-layer.md](database-layer.md) (dialects + `ibSqlFeatures`).

## The goal

A **window function** computes an aggregate **per row over a window of related rows**,
WITHOUT collapsing the result the way `GROUP BY` does. The canonical form:

```
SELECT
    Warehouse,
    Period,
    Qty,
    SUM(Qty) OVER (PARTITION BY Warehouse)                      AS WarehouseTotal,
    SUM(Qty) OVER (PARTITION BY Warehouse ORDER BY Period)      AS RunningTotal
FROM Document.GoodsMovement.Lines
```

Every detail row survives; each carries its partition total and its running total. This
is the difference from `GROUP BY`: **PARTITION BY keeps the rows, GROUP BY folds them.**

### Why the server, not the interpreter
A window is defined over the *whole partition* — to compute one row's value the engine
must see every other row in its partition. Done in C++ that means materialising the
partition (or the whole result if unpartitioned) into RAM and folding it per row — exactly
the cost T1.1 is removing. The database computes a window with one pass and its indexes, so
a window function **pushes to SQL or it does not run** (see "RAM path" below). This is the
same "push the set work to the DBMS; RAM is the fallback" rule as T1.1 — except here the
fallback is *not available*, so the gate is an **error**, never a silent RAM fold that
would be O(partition²) or wrong.

## Ground truth — where it plugs in (verified)

| Layer | File / symbol | Today | Window change |
|---|---|---|---|
| Keywords | `queryKeywords.h` `ibQueryKeyword` (~L69 Sum/Count/Min/Max/Avg) | no OVER/PARTITION | add `Over`, `Partition` (`By`/`Order` reuse the existing GROUP/ORDER BY tokens) |
| AST | `queryAst.h` `ibQueryAstExpr` kind `Func` (L87–96: `m_func`, `m_star`, `m_distinctArg`, `m_arg`) | aggregate call only | add an optional **window spec** on the Func node (partition list + order list) |
| AST projection | `queryAst.h` `ibQueryProjection` (L132) `m_expr`/`m_alias`/`m_star` | holds a Func expr | unchanged — a windowed aggregate is still one projection expr |
| Parser | `queryParser.cpp` `ParseAggregate()` (L841) | reads `FN([DISTINCT] arg)` | after the `)`, if next is `OVER`, parse `OVER ( [PARTITION BY e,…] [ORDER BY e[dir],…] )` onto the Func node |
| Lowering | `queryLowering.cpp` aggregate build (L1439–1468) `b.Aggregate(fn,…)` | GROUP-BY aggregate | a windowed Func lowers to a new **window projection** (NOT a GROUP BY aggregate), gated on DB-backed source + `ibSqlFeatures::m_window` |
| L2-1 IR | `databaseQueryBuilder.h` `ibQueryExprKind` (L56) / `ibQueryExpr` (L91) | `Func` | add kind `Window` (wraps fn + arg + partition exprs + order items) OR window fields on `Func` |
| Render | `dbTableProvider.cpp` (aggregate → SQL) + `queryRender.cpp` | `FN(arg)` | append ` OVER (PARTITION BY … ORDER BY …)` when the expr carries a window spec |
| Dialect | `databaseLayer.h` `ibSqlFeatures::m_window` (L68, **already exists, unused**) | always false | each dialect sets it true where the engine has window functions (PostgreSQL ✓, SQLite ≥3.25 ✓, Firebird 3+ ✓); gate render on it |
| RAM | `queryProvider.cpp` fold | GROUP-BY fold | **no window fold** — a window over a RAM/computed source is an error (`log()`+raise), never a silent O(n²) |

**No AOT / wire bump.** The query AST and the L2-1 IR are rebuilt per execution from the
query text (`ParsePackage` re-parses; the builder is per-run) — neither is serialized into
`sys_bytecode_cache`. A new AST/IR field changes generated SQL only, so `kAOTFormatVersion`
is untouched (same property T1.1 relied on).

## Grammar (what the parser accepts)

```
windowedAggregate := aggregate  "OVER"  "("  [ partitionClause ]  [ orderClause ]  ")"
aggregate         := ( SUM | COUNT | MIN | MAX | AVG )  "("  [ DISTINCT ]  ( "*" | expr )  ")"
partitionClause   := "PARTITION" "BY"  expr  { "," expr }
orderClause       := "ORDER" "BY"  orderItem  { "," orderItem }
orderItem         := expr  [ "ASC" | "DESC" ]
```

- Both clauses optional: `SUM(x) OVER ()` = the grand total on every row.
- **No frame clause** (`ROWS/RANGE BETWEEN …`) in the initial increments — the default
  frame (RANGE UNBOUNDED PRECEDING … CURRENT ROW when ORDER BY is present; whole partition
  when not) is what a running/partition total needs. An explicit frame is a later increment.
- `DISTINCT` inside a windowed aggregate is **rejected** at parse (most engines forbid
  `SUM(DISTINCT x) OVER (…)`; refuse rather than emit dialect-divergent SQL).

## Correctness oracle — parity + a known-answer fixture

The T1.1/parity discipline applies, adapted: a window has **no C++ fold to compare against**
(that is the point), so the oracle is a **known-answer fixture on a real SQLite** (SQLite
≥3.25 has window functions, and it is the always-embedded test driver). Each increment adds
cases to a new `tests/test_queryWindow.cpp` built on the `ComputedServerFix` harness
(appData + in-memory SQLite + pool + `PhysicalQ`), asserting the rendered
`FN(arg) OVER (…)` returns the hand-computed per-row values:

- partition total: every row in a partition carries the same sum; sums differ per partition;
- running total: ordered cumulative sum within a partition resets at the partition boundary;
- unpartitioned `OVER ()`: grand total on every row, row count preserved (NOT collapsed);
- `COUNT(*) OVER (PARTITION BY k)`: partition size on each row;
- NULL handling in the ordering / argument matches SQL (NULLs sort, NULL arg skipped by SUM);
- the no-push gate: a window over a RAM/computed source raises, with a clear message.

## Increment plan (risk-ordered, each shippable + oracle-gated)

### Inc 1 — `PARTITION BY` only  — **DONE**
Landed: keywords `Over`/`Partition`; parser accepts `FN(arg) OVER ( [PARTITION BY …] [ORDER BY …] )`
on an aggregate (ORDER BY parsed but lowering defers it to Inc 2); AST window fields on the `Func`
node; L3 `ibQueryColumnExpr::Window`; lowering routes a windowed Func through `SelectExpr` (rows
preserved, never `ExecuteAggregate`), gated on a single DB source; L2-1 `ibQueryExpr` OVER fields +
render; renderer gates on `ibSqlFeatures::m_window`. Tests: `tests/test_queryWindow.cpp` (3 e2e on
SQLite) + `tests/test_queryL4Parser.cpp` (6 parse cases). DISTINCT-in-window and window-over-RAM are
rejected; window ORDER BY raises "Inc 2".


The smallest useful window: `FN(arg) OVER (PARTITION BY e, …)` — a partition aggregate on
every detail row, no ordering, no frame. Covers the dominant report need ("the row plus its
group's total") and exercises the whole pipe end to end.
- Keywords `Over`, `Partition`; parser accepts the OVER-with-partition tail on an aggregate.
- AST: window spec (partition expr list) on the `Func` node; a flag `m_hasWindow`.
- Lowering: a windowed Func becomes a window projection; **gate**: outer source DB-backed AND
  `GetDialect().m_window` true AND no `DISTINCT` arg — else `ThrowQueryException` (window needs
  a server with window functions). No interaction with GROUP BY in Inc 1 (a query mixing a
  bare GROUP BY aggregate and a window is a later inc; Inc 1 handles windows in a
  non-grouped SELECT).
- IR + render: `ibQueryExprKind::Window` carrying fn/arg/partition; render `FN(arg) OVER
  (PARTITION BY …)`.
- Tests: partition total, unpartitioned `OVER ()`, `COUNT(*) OVER (PARTITION BY k)`, the
  no-push gate.

### Inc 2 — `ORDER BY` in the window (running aggregates)  — **DONE**
Landed: the window `ORDER BY` (parsed since Inc 1) now lowers and executes. L3
`ibQueryColumnExpr::Window` carries `m_windowOrderBy` (expr, ascending); L2-1 `ibQueryExpr` carries
`m_windowOrder`; the Func renderer appends `ORDER BY e [DESC], …` inside the OVER. The dialect's
default frame (unbounded preceding … current row) is implied, not spelled, so every engine
accumulates the same way. Lowering resolves each order expr to a plain column of the single source.
Tests: `tests/test_queryWindow.cpp` — running total within a partition that resets at the boundary,
and DESC accumulation (full oes_tests 1497/1497).

### Inc 3 — ranking functions  — **DONE**
Landed: `ROW_NUMBER()`, `RANK()`, `DENSE_RANK()` — no argument, ORDER BY inside OVER required
(enforced at parse). Keywords `RowNumber`/`Rank`/`DenseRank` + `ibIsRankingKeyword` (their own family,
distinct from the aggregates); parser `ParseRanking` (empty parens, required OVER + ORDER BY) sharing
the extracted `ParseOverClause`; `ibWindowAggFn` extended; lowering builds the Window node with no
argument; the Func renderer emits `ROW_NUMBER() OVER (…)` with empty parens. Tests:
`tests/test_queryWindow.cpp` (RANK skips after a tie 1,1,3,4; DENSE_RANK does not 1,1,2,3; ROW_NUMBER
distinct) + `tests/test_queryL4Parser.cpp` (5 parse cases incl. ORDER-BY-required, OVER-required,
no-argument). Full oes_tests 1505/1505.

### Inc 4 — explicit frame + window coexisting with GROUP BY
`ROWS/RANGE BETWEEN … AND …`; and a window computed over the *result of* a GROUP BY
(window over grouped rows). Both are real but niche; specified when reached.

## Non-goals / guard-rails
- **No RAM window.** A window over a computed/RAM source raises (clear message), never a
  silent O(partition²) fold. The gate is a capability question asked of the dialect
  (`ibSqlFeatures::m_window`) + the source (DB-backed), mirroring T1.1 — no `if (firebird)`.
- **No frame in Inc 1–3.** Default frame only; explicit `ROWS/RANGE` is Inc 4.
- **AOT / wire format unaffected** (AST + IR are per-execution, not serialized).
- **Named answers, not rendered strings, in tests** — assert row values from a real SQLite,
  the engine the dialect flag promises.
