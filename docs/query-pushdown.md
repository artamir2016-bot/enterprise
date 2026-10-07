# Query pushdown — move set work from the interpreter into SQL (T1.1)

Clean-room reimplementation track (backlog item **T1.1**,
[docs/upstream-reimpl-backlog.md](upstream-reimpl-backlog.md)). Spec written from our
own query-engine map + the observed goal; no upstream code is copied.

Companion maps: [query-engine-layers.md](query-engine-layers.md) (L1–L5 floor plan),
[query-language-arc.md](query-language-arc.md) (the arc), [database-layer.md](database-layer.md).

## The problem

Three set-valued constructs are resolved **in C++ at lowering time** and reach the
database only as a flat `col IN (?, ?, …)` parameter list:

| Construct | Current location | What the server sees today |
|---|---|---|
| `IN (SELECT …)` | `queryLowering.cpp` (~909) — the subquery is run via `sq.Execute()` and its rows collected | a flat value list |
| `IN HIERARCHY` | `queryLowering.cpp` (~887) — `ibQueryHierarchyScope` walks the parent map | a flat value list |
| `TOTALS` (multi-level / hierarchical / multi-source) | `queryLowering.cpp` `ExecuteTotals` (~3130) + `queryProvider.cpp` fold | detail rows, folded in C++ |

For small sets this is fine. For a real configuration it is the dominant cost:
every candidate row (potentially the whole table) crosses the wire so C++ can do a
membership test the database can do with an index. The win is to render the set
operation **as SQL the DB executes** — a semi-join, a recursive membership walk, a
grouping fold — so only the surviving/aggregated rows cross the wire.

CAST to a primitive is a separate, smaller item: today primitive CAST is **rejected
at lowering** (`queryLowering.cpp` ~461 — "CAST narrows a reference, it does not
convert values"); reference-narrowing CAST already renders server-side
(`ibQueryExprKind::Cast`). T1.1 adds the value-conversion form.

## Correctness oracle — the parity harness

Every increment is gated by the existing parity pattern (`tests/test_queryParity.cpp`,
`test_queryJoinParity.cpp`, `test_queryTotals.cpp`): the same query is answered two
ways and the results must be identical. Today the two ways are **RAM composer** vs
**SQL**. Pushdown adds a third assertion — **old path (C++-materialised)** must equal
**new path (pushed to SQL)** — so a divergence (NULL three-valued logic, duplicate
handling, ordering) fails a test instead of reaching a user. This is the whole safety
story: we never change WHAT a query answers, only WHERE the set work runs.

## Increment plan (risk-ordered, each shippable + parity-gated)

### Inc 1 — `IN (SELECT …)` as a real semi-join  ⟵ implement first
The smallest self-contained win with the cleanest oracle.

- **IR (L2-1):** extend the `In` IR node (`databaseQueryBuilder.h` `ibQueryExprKind::In`)
  so its right side can be **a subquery (`ibQuerySelect`/sub-IR)**, not only `m_args`
  values. Render `col IN (SELECT … )` (the renderer already emits `col IN (…)`; the
  only change is that the parenthesised body is a rendered SELECT, not a `?`-list).
  All four dialects spell `IN (SELECT …)` identically, so this is one render path.
- **Lowering (L4→L3):** in the `e.m_subquery` branch (~909), instead of
  `sq.Execute()` + collecting rows, **lower the subquery to a sub-IR** and attach it
  to the `In` node. Keep the materialise path as a fallback for the cases the sub-IR
  cannot yet express (correlated refs, a subquery over a RAM-only source), chosen by
  an explicit `CanPushSubquery()` predicate — never silently.
- **Semantics to preserve:** `IN (SELECT …)` is a **semi-join** (existence), so
  duplicate rows in the subquery do not multiply results, and `NOT IN` keeps SQL's
  NULL three-valued logic (a NULL in the subquery makes `NOT IN` unknown → the parity
  test must cover a NULL in the subquery column).
- **Tests:** extend `test_queryJoinParity.cpp` — for a set of predicates, assert
  `materialised == pushed` and `pushed == RAM`. Include: empty subquery, duplicate
  values, NULL in the subquery column, `NOT IN`, a subquery with its own WHERE.

> **Inc 1 design finding (reuse, don't extend the IR).** The semi-join machinery
> ALREADY exists for RLS: `ibSemiJoinExists` (`queryable.h` ~258) rides on
> `ibQueryCondition::m_semiJoin` and renders to `EXISTS (SELECT 1 FROM inner sj WHERE
> sj.innerKey = outer.outerKey AND …)` via `ibMetaIRBuilder::BuildSemiJoinExists`
> (`dbTableProvider.cpp` ~868). So Inc 1 lowers `IN (SELECT)` to an `ibSemiJoinExists`
> leaf and gets the EXISTS render for free — no change to the L2-1 `In` node.
> `x IN (SELECT k FROM T WHERE p)` ≡ `EXISTS (SELECT 1 FROM T sj WHERE sj.k = outer.x AND p)`.
>
> **Critical constraint — RAM has no semi-join.** `RamEvalLeaf` (`queryProvider.cpp`
> ~930) does NOT handle `m_semiJoin`; it DOES evaluate a flat `ibQueryFilterOp::In`
> over `m_values` with correct NULL/empty-set semantics (lines 940-944). So the
> predicate must differ by target: **DB-backed outer source → emit the semi-join leaf;
> RAM outer source → keep the existing `sq.Execute()` materialise + value list.** The
> lowering gates on an `IsServerBacked()`-style capability of the outer queryable, not
> a blind push.
>
> **Push gate `CanPushSemiJoin(e)` — all must hold, else materialise:** not
> `e.m_negated` (NOT IN keeps SQL 3-valued NULL logic — a later inc); subquery is one
> plain source with `GetQueryTableName()` (no JOIN/TOTALS/GROUP BY/UNION/DISTINCT/TOP/
> aggregate projection); exactly one projected plain column (the `innerKey`); outer
> owner DB-backed. **Build:** `ibSemiJoinExists{ m_inner = ResolveFrom(sel.m_from),
> m_where = lowered sel.m_where, m_outerKey = cols.back(), m_innerKey = projectedCol,
> m_op = Equal }` → `ibQueryCondition.m_semiJoin` → `Leaf(cond)`.
>
> This keeps Inc 1 to ONE file (`queryLowering.cpp`, the `e.m_subquery` branch ~909)
> plus a parity test, with the materialise path as the correctness-preserving fallback.

### Inc 2 — `IN HIERARCHY` walked by the server
- Render the hierarchy membership as SQL rather than resolving `ibQueryHierarchyScope`
  in C++. Shape per dialect: a **recursive CTE** over the parent column
  (`WITH RECURSIVE sub AS (SELECT seed … UNION ALL SELECT child JOIN sub …)`) for
  engines that have it (PostgreSQL, SQLite, Firebird 3+), feeding the `IN (SELECT …)`
  built in Inc 1. `HIERARCHY` vs `HIERARCHY ONLY` vs `ELEMENTS` (`ibQueryDimUnfold`)
  choose the seed/÷recursion shape.
- A driver without recursive CTE falls back to the C++ walk (explicit capability flag
  on the dialect, logged — no silent divergence).
- **Tests:** parity over a known parent tree — a node with descendants at depth ≥ 3,
  `HIERARCHY` (self+subtree) vs `HIERARCHY ONLY` (subtree) vs `ELEMENTS` (leaves),
  `NOT IN HIERARCHY`, a cycle guard (recursion must terminate).

### Inc 3 — `TOTALS` folded on the server
- Push the multi-level / grand-total cases ExecuteTotals currently folds in C++ onto
  the server via **GROUPING SETS / ROLLUP** where the dialect has it, keeping the C++
  fold for hierarchical-by-parent-link totals (which need the parent walk) and for
  drivers without GROUPING SETS.
- **Tests:** extend `test_queryTotals.cpp` — multi-dimension totals with grand total;
  assert server-folded tree == C++-folded tree (counts, sums, per-level subtotals).

### Inc 4 — `CAST` to a primitive (value conversion)
- Allow `CAST(expr AS Number/String/Date/Boolean)` as a **value conversion** (distinct
  from the existing reference-narrowing CAST), lowered to the IR `Cast` node with the
  primitive target and rendered per-dialect (`MapType`). Define the conversion
  semantics explicitly (string↔number parsing, date formatting, overflow/NULL on bad
  input) and lock them with parity tests so every dialect and the RAM path agree.

## Non-goals / guard-rails
- **No behaviour change.** Pushdown must be bit-identical to the C++ path on the parity
  suite before it ships; where it cannot be, it does not push (explicit predicate +
  `log()` of the fallback, never silent).
- **Driver capability, not a central switch.** Whether a construct pushes is asked of
  the dialect (`ibDialectDictionary` capability flags), mirroring how the engine already
  decides RETURNING / multi-row VALUES — no `if (firebird)` in the lowering.
- **AOT / wire format** is unaffected (this changes generated SQL, not bytecode), so no
  cache-version bump.
