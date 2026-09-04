# OES development MCP server

A dependency-free **stdio MCP server** that lets Claude Code develop OES
configurations and drive running apps. Registered in the repo `.mcp.json` as
`oes-dev` (variant B — a thin shim over the CLI tools + the in-app `--testagent`;
an HTTP endpoint embedded in the Configurator/Enterprise is a later migration, tool
names stay stable).

## Tools

| Tool | What it does | Backed by |
|------|--------------|-----------|
| `oes_config_generate` | Build a NEW `.mcf` from a friendly JSON spec (greenfield) | `oes_config_gen.exe` |
| `oes_config_edit` | MERGE a JSON patch onto an existing `.mcf` — add new objects/attributes/modules or edit existing ones (idempotent by name) | `oes_config_edit.exe` |
| `oes_config_load` | Load an `.mcf` into a file base and apply it to the DB | `designer.exe /LoadCfg /UpdateDBCfg` |
| `oes_config_check` | Compile-check the base's modules (errors with module+line) | `designer.exe /CheckModules` |
| `oes_app` | Forward any command to a RUNNING app's test agent — Enterprise (forms, DB objects, UI) or Configurator (menus, breakpoints, debug, profiler) | in-app `--testagent` TCP |

The JSON spec/patch shape is documented in `src/engine/backend/metadataConfigSpec.h`:
`{ name, catalogs:[{name, attributes:[{name, type, length, precision, scale}]}],
documents, enums, constants, commonModules:[{name, code}] }`, `type` ∈
`String|Number|Date|Boolean|ref`.

## Live-app usage

Start the app with the agent, then call `oes_app`:
- Enterprise (DB objects + UI): `enterprise.exe --file=<base> --testagent=1651`
  → `oes_app {port:1651, cmd:"openForm", args:{name:"Товары", kind:"object"}}`,
  `listControls`, `getControlValue`, `setControlValue`, `pressCommand`,
  `getDiagnostics`, `getMessages`, `listWindows`, …
- Configurator (development/debug): `designer.exe --file=<base> --testagent=1652`
  → `oes_app {port:1652, cmd:"invokeMenu", args:{path:["Debug","Performance profiler"]}}`,
  `setBreakpoint`, `debugState`, `readTreeList`, `openMetaEditor`, …

## Configuration

`.mcp.json` sets the Python interpreter, the script path, and `OES_BIN` (the
directory with `designer.exe` / `enterprise.exe` / `oes_config_*.exe`). Adjust
`OES_BIN` if you build elsewhere. The server needs Python 3.8+ (stdlib only).

## Development loop it enables

1. `oes_config_edit` — add/modify metadata objects on the `.mcf`.
2. `oes_config_load` — apply to the base.
3. `oes_config_check` — compile-check.
4. `oes_app` — open the object/form in a running Enterprise and verify controls,
   or drive the Configurator (breakpoints, profiler) live.

## Follow-ups

- A JSON config **read/dump** verb (currently `/DumpCfg` writes binary `.mcf`;
  wiring `ibJsonProvider` into a batch verb would give an LLM-readable structure dump).
- Migrate the transport to an HTTP/MCP endpoint embedded in the apps (variant A).
