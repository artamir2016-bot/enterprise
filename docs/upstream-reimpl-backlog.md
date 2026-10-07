# Upstream reimplementation backlog (clean-room)

Prioritised candidates distilled from **484 upstream commits** on
`open-enterprise-solutions/enterprise@develop` accumulated since our fork point
(`f22bcfa7`, 2026-08-16) through `0d4a7395` (2026-10-05). Snapshot taken 2026-10-05.

## ⚠️ License boundary — clean-room is mandatory

Upstream **relicensed to PolyForm Noncommercial** at commit `7e744013`
(2026-08-23) and **moved its design docs into a private submodule** (`docs/private/`,
all `docs/*.md` removed from the public tree). Consequences:

- **We may NOT merge / cherry-pick / copy** upstream code or docs from `7e744013`
  onward (467 commits). Their code is *source-available*, not license-compatible
  with our LGPL-2.1 fork.
- Process for every item below: **(1)** read their implementation + commit message
  as a *reference only* → **(2)** write our OWN spec (a `docs/*.md`) describing the
  behaviour/design in our words → **(3)** implement from the spec with our own code
  and tests.
- The **16 commits before the relicense** (between our fork 2026-08-16 and
  2026-08-23) are still LGPL 2.1 and *could* be merged directly — but they are the
  oldest/smallest of the set; treat case-by-case.
- We only have **commit messages + code** to reverse-engineer from — the design
  docs are gone. Specs must be written from behaviour, not from their prose.

Commit hashes below are **reference pointers into upstream history** for study, not
things to merge.

---

## Tier 1 — highest value (performance + query engine)

These are the dominant themes upstream and the biggest wins for an imported real-world
1C configuration (where query + runtime cost dominates).

### T1.1 — Query pushdown to the server
Move set/relational work from the interpreter into SQL the DB executes.
- `IN (SELECT …)` compiled as a real **semi-join** to the server, not a parameter list
  (`8dd608e6`, `44183190`, `c5d9453a`).
- `IN HIERARCHY` walked by the server; hierarchy totals folded by keys (`00145890`, `645e9ee0`, `19d42e12`).
- `TOTALS` fold on the server; `BY … PERIODS`; turnovers fold on the server
  (`06273313`, `9ffd1a70`, `c4f6eac4`, `f38da764`).
- `CAST` to a primitive; declare a nested source to the server; a column answers for itself
  (`e5378281`, `036557fa`, `55e2b364`).
- **Spec:** extend `docs/query-engine-layers.md` (L3/L4 pushdown). **Effort:** L (multi-increment).

### T1.2 — Window functions
`OVER (…)`, window aggregates computed over the source alone (joins/order outside the
window), a figure names the grouping it is computed over, `SPLIT` (one read, several folds).
Refs: `eaa585a9`, `6b462826`, `1ead15ab`, `abe3044b`, `349606e2`, `6e822c59`.
- **Spec:** new `docs/window-functions.md`. **Effort:** L.

### T1.3 — `ibValue` / `ibString` / `ibNumber` memory + speed
- `ibValue` is **24 bytes on x64** (refcount fills the hole before the union) (`ff640173`).
- `ibString`: statically-zeroed pool (no lazy-init guard), inline read, one block per
  text, class counted from block size, shared handles in the union
  (`7e3c34a2`, `b7d568c6`, `f58a3f71`, `be1c09c8`, `445a4936`, `d36fff2e`).
- `ibNumber`: non-exact division limb-by-limb, number assigned onto number directly,
  two immediate numbers divided on the stack (`054909f2`, `ef5a675f`).
- **Spec:** update `docs/fnumber.md` + a new `docs/value-layout.md`. **Effort:** M. **Risk:** touches hot core + AOT format.

### T1.4 — Interpreter frame + call fast-paths
`Execute`'s frame **5.6 → 1.8 KB**; a function reached by its index (not name); a script
call without the session lock / cold messages / declarators; LINQ without per-row
allocations + a lighter lambda call; `Structure`'s method table is the type's (−2 KB/row);
container key hash + build-in-place.
Refs: `f31d100c`, `2a8856b0`, `97b8f05b`, `f4a62361`, `571b7afa`, `b3b14521`.
- **Spec:** update `docs/compiler-pipeline.md` + `docs/factories.md`. **Effort:** M.

---

## Tier 2 — major user-facing subsystems

### T2.1 — Reports / data composition (big theme)
- **Conditional appearance** (условное оформление) for reports and lists; "filled"
  comparisons (`cdc4500e`).
- **Composer metatype** — a report declares what it reads (`bb18bb69`, `8af9b47c`, `db753d29`).
- **Cross-tables** — column groups, two axes in one fold, each heading its own branch
  (`4efc4437`, `ae1b4d22`, `58aa9fba`).
- Report **variants / saved settings** survive the form; author's sort decides order;
  detail a figure by a reader-chosen field (`8af9b47c`, `688407a3`, `187e47b3`, `e6c2b722`).
- **Spec:** extend `docs/report-engine.md` + `docs/data-composer.md`. **Effort:** L.

### T2.2 — Own date engine `ibDateTime`
A wall-clock reading in **8 bytes**, reached only through its own API, replacing
`wxDateTime`; a date shifted in a script loop; Format on macOS.
Refs: `506a232e`, `7a62b4d5`, `cc6c85f3`.
- **Spec:** new `docs/date-engine.md` (mirror the `ibNumber` story). **Effort:** M. **Risk:** wide callsite sweep + AOT/serialization.

### T2.3 — Spreadsheet: foreign formats + printing
Read/write **Excel in & out, Word out**; print preview fit-to-width; automatic row
height; grid borders/merge/selection of merged cells; sheet formats "read what we write".
Refs: `5082c8ee`, `c29f54fc`, `ccc0096e`, `9b688da0`, `ed37be52`, `02d24bd1`, `326f9ce1`.
- **Spec:** extend `docs/spreadsheet-editor.md`. **Effort:** L (Excel/Word I/O is the cost).

### T2.4 — `appserver` — several bases in one process
An application server holds many bases, each with one owner; a background run works in
its manager's base; a rented connection is given back.
Refs: `10d25a32`, `7ec25bd0`, `45cd9a71`, `67673aba`.
- **Spec:** new `docs/appserver.md`. **Effort:** L. **Risk:** session-ownership model.

---

## Tier 3 — script language & platform capabilities

### T3.1 — JSON from script
`JSONReader` / `JSONWriter` / `ReadJSON` / `WriteJSON` + the two kind enumerations;
values born held; strings as runtime strings. Refs: `f94b719a`, `d127d3f0`, `0f639b9b`.
- **Spec:** `docs/script-json.md`. **Effort:** M.

### T3.2 — HTTP(S) from script
`HTTPConnection` / `HTTPRequest` / `HTTPResponse`, secure connection, on cpp-httplib +
Mbed TLS (both vendored as submodules). Refs: `1afe8ea9`, `0604e5d7`, `994fb7f6`, `c3bff6da`.
- **Spec:** `docs/script-http.md`. **Effort:** M. **Note:** adds 3rd-party deps (check licenses).

### T3.3 — Text/binary IO + Container family
`TextReader` / `TextWriter` / `BinaryData` / `TextEncoding`; `Container` keeps string
keys verbatim, `Get(key)`→value|Undefined; `KeyValue`; bytes→text through one door.
Refs: `27ce4a41`, `7432a266`, `0e530ef4`, `8b49a6ba`. **Effort:** M.

### T3.4 — Value table / query-result conveniences
`Total`, `FindRows`, `Sort` (one/several keys, per-key direction); rows reach a script
value table through one fast load; `Table.Clone`.
Refs: `ff74bc01`, `60053434`, `12fd32ec`, `cd908cd3`, `5095e7ca`. **Effort:** S–M.

### T3.5 — Functional options
A stored value that decides what the interface shows (feature toggles driving UI).
Refs: `05047715`, `18f0c05e`, `493846af`. **Effort:** M.

---

## Tier 4 — assistant / tooling / infra

### T4.1 — MCP server (assistant drives the platform)
An MCP server lets an assistant build/drive a configuration over the designer;
conditions expressed as trees over MCP; the sandbox (arbitrary code in a live session,
a transaction that undoes it). Refs: `729daa86`, `cdc4500e`, `999c55e5`, `861140d5`.
- **Note:** overlaps our own tooling direction (test-agent / gui_probe). **Effort:** L.

### T4.2 — Debugger hardening
Shutdown-race fix (macOS), one thread per debug socket, frame-read safety, "says why a
session ended". Refs: `2474492a`, `7de8aec2`, `53d2075b`, `a271c76f`. **Effort:** S–M.

### T4.3 — Memory-safety sweep (ASan/UBSan) — adopt the PRINCIPLE
Upstream ran ASan+UBSan and fixed a class of "value born owned" / use-before-exists /
double-free bugs (`c2317683`, `fa9f5ae6`, `4d20e9c2`, `5dd55bbe`, `835f6fbb`).
- **Action (not copy):** stand up our own ASan+UBSan CI job and fix what *we* find.
  Independent of license. **Effort:** M. **Value:** high (correctness).

### T4.4 — CI / build portability — adopt the PRINCIPLE
Compiler cache on all jobs, Firebird for real in CI, wxWidgets without PCH, submodule
auto-fetch. Refs: `49bb5da3`, `5da8886e`, `d378e0b9`. Reimplement in our own CI. **Effort:** S.

---

## Suggested order

1. **T4.3 + T4.4** (ASan/UBSan + CI) — principle-level, no license entanglement, de-risks everything else.
2. **T1.3 + T1.4** (value/string/number layout + interpreter frame) — foundational perf, unblocks later specs; do early, before more code piles on the current layout.
3. **T1.1 + T1.2** (query pushdown + window functions) — biggest runtime win for imported configs.
4. **T2.2** (`ibDateTime`) — foundational type; the longer we wait the wider the sweep.
5. **T2.1 / T2.3 / T2.4** (reports / spreadsheet / appserver) — large user-facing subsystems, scheduled per demand.
6. **T3.x / T4.1** (script JSON/HTTP/IO, functional options, MCP) — additive capabilities, independent, pick by need.

Each item becomes: a spec `docs/<name>.md` first, then increments with their own tasks.
