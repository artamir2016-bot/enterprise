# Designer CLI — 1C:Enterprise 8.3-compatible batch mode

> **FORK FEATURE (not upstream).** Added on the `artamir2016-bot/enterprise` fork so a
> configuration can be loaded, dumped and checked **without the GUI** "load configuration from
> file" dialog. Every edit is marked in-code with an `OES-CLI:` comment so a diff against upstream
> shows exactly what the fork added.

## Goal

Run the Designer **headless** (no window, no event loop) for automation / CI: open an infobase,
load or dump a configuration, and compile-check the configuration or its modules, returning a
process exit code (`0` = ok, `1` = errors / failure). The option grammar mirrors the
`1cv8 DESIGNER …` batch command line so existing 1C automation scripts port with minimal change.

## Invocation

```
designer.exe /F"<dir>" /N"<user>" /P"<pwd>" /CheckConfig /Out"check.log"
designer.exe /F"<dir>" /LoadCfg "<file.mcf>" /UpdateDBCfg /Out"load.log"
designer.exe /S<host[:port]\base> /N<user> /P<pwd> /CheckModules
designer.exe /F"<dir>" /DumpCfg "<out.mcf>"
```

Batch mode is entered automatically when **any** of `/LoadCfg`, `/DumpCfg`, `/CheckConfig`,
`/CheckModules` is present. Without them the Designer starts normally (GUI).

## Options

Keys are case-insensitive. A value may be attached (`/F"path"`) or space-separated (`/F path`).

| Key | Meaning | 1C analogue |
|---|---|---|
| `/F <dir>` | file infobase | `/F` |
| `/S <host[:port]\base>` | server infobase (`\` splits base, `:` splits port) | `/S` |
| `/N <user>` | infobase user | `/N` |
| `/P <password>` | infobase password | `/P` |
| `/LoadCfg <file>` | load configuration from a file into the base | `/LoadCfg` |
| `/DumpCfg <file>` | save the base configuration to a file | `/DumpCfg` |
| `/Out <file>` | write operation messages to a file (UTF-8) | `/Out` |
| `/UpdateDBCfg` | apply the loaded configuration to the database | `/UpdateDBCfg` |
| `/CheckConfig` | compile-check the whole configuration | `/CheckConfig` |
| `/CheckModules` | compile-check every module | `/CheckModules` |
| `/DisableStartupMessages` / `/DisableStartupDialogs` | accepted; batch mode is silent already | same |

Unknown `-SubKey` sub-options (e.g. `-ThinClient`, `-Server`, `-ExtendedModulesCheck`) are
**accepted and ignored** for command-line compatibility.

## Semantics

1. **Open** — the infobase is opened headless with a base `ibSession` (same path the daemon uses),
   authenticating with `/N` `/P`. Opening the session compiles the configuration currently in the
   base; the diagnostics from that compile feed `/CheckConfig` / `/CheckModules` when no `/LoadCfg`
   is given.
2. **`/LoadCfg`** → `ibMetaDataConfigurationBase::LoadConfigFromFile`. With `/UpdateDBCfg` the
   loaded configuration is written to the database (`SaveDatabase`, i.e. restructuring). When
   `/LoadCfg` is combined with a check, the freshly loaded configuration is re-compiled
   (`ibSession::CompileRoot`) and those diagnostics are reported.
3. **`/CheckConfig` / `/CheckModules`** → the compile error chain
   (`ibBackendException::DrainLastErrors`) is drained and printed. Any error → exit code `1`.
4. **`/DumpCfg`** → `ibMetaDataConfigurationBase::SaveConfigToFile`.
5. **Output** — all messages are written to `/Out` (if given) and to `stdout` (so a console or a
   redirect still sees them).

## Exit codes

| Code | Meaning |
|---|---|
| `0` | success — no errors detected |
| `1` | open/auth failed, an operation failed, or the check found compile errors |

## Where it lives

`src/engine/designer/mainApp.{h,cpp}` — `DetectBatchMode` (called from `OnInitCmdLine`),
`ParseBatchArgs`, `RunBatch`, `WriteBatchReport`. The batch path returns from `DoOnRun` before any
window is created, so no wxFrame and no event loop are involved.

## Not covered (follow-ups)

- Loading a **file** configuration with no base at all (today a base is required to host the check).
- 1C sub-key semantics (`-ThinClient` / `-Server` / `-ExtendedModulesCheck`) are ignored rather
  than changing the check scope.
- `/DumpIB` / `/RestoreIB` (infobase dump/restore) — not implemented.
