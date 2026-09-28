# Managed-form client/server split — the web client engine

Imported 1C-style managed forms annotate each form-module procedure with a
compilation directive — `&НаКлиенте` (AtClient), `&НаСервере` (AtServer),
`&НаСервереБезКонтекста`, `&НаКлиентеНаСервереБезКонтекста` — deciding WHERE it
runs. On the web this is a real split: `&НаКлиенте` bytecode executes in the
browser (`OES.ClientVM`), `&НаСервере` on the session worker, with the form
context marshalled across the boundary.

This doc is the map of the engine and its test matrix. The staged build history
is in `../` plan notes (Increment 5, sub-increments 5a–5e).

## Pieces

| Layer | File | Role |
|---|---|---|
| Directive lex/parse + env enum | `src/engine/backend/compiler/{codeDef.h,translateCode.cpp,compileCode.cpp,byteCode.h}` | `ibExecEnv` per function; `&`-directives + Russian aliases |
| Call classification | `ibClassifyExecCall` (`byteCode.h`) | Direct / ServerHop / Illegal |
| Client-bytecode emitter | `src/engine/frontend/wfrontend.cpp` `FormClientBytecodeInSession` | `GET /client-bytecode` → JSON: functions (name/entry/env), code tape, consts, vars, **formCtx** (name↔controlId bindings) |
| Server-hop RPC | `wfrontend.cpp` `ServerCallInSession` | `POST /srv-call` {proc,args,context} → apply context, run proc via `ibProcUnit::CallAsFunc`, read context back → {ret,context} |
| Routes | `src/engine/wenterprise-server/main.cpp` | `GET /client-bytecode`, `POST /srv-call` |
| Client VM | `webClient/clientvm.js` (`OES.ClientVM`) | a stack/frame interpreter over the bytecode JSON |
| Dispatch + wiring | `webClient/client.html` | button onclick → `OES.tryClientHandler`; transport `OES.srvCall`; `onMessage` → output strip |

## What the VM covers (`OES.ClientVM`)

- **Opcodes**: const/int, let, arithmetic (+ − × ÷ mod invert), compares, not/and/or,
  if/goto, `OPER_CALL` (intra-module), `OPER_CALL_METHOD` (system funcs + form
  methods), member access `GET_A/SET_A`, scope/context/extern `GET_*/SET_*`,
  func/endfunc/ret. Anything else → `OESVMUnsupported`.
- **5b form context**: a `FormScopeProxy` (form attributes + the object handle)
  and an `ObjektProxy` (main-object fields). `Сумма = …` (form attr, `GET_SCOPE/
  SET_SCOPE`) and `Объект.Цена = …` (`GET_A/SET_A`) mutate the client context;
  each write records a dirty `controlId` the browser re-renders.
- **5c server hop**: an `OPER_CALL` whose target is a Server-env function awaits
  `serverHop()` → the injected transport (`POST /srv-call`) ships the context +
  args, applies the returned context. The VM is async for this reason.
- **5d client system funcs + method dispatch**: `Сообщить`/`Message`/… emit a
  message; `ЭтаФорма.<Метод>()` routes to the form's own proc (client → in-page,
  server → hop).
- **5e fallback safety**: an unsupported op **before** any hop → `OESVMUnsupported`
  (caller re-runs the whole handler on the server); **after** a hop →
  `OESVMHalt` (surface the error, do NOT re-run — a re-run would double the hop).
  Client mutations are applied to the DOM only on full success.

## Tests

### Unit / integration — `webClient/clientvm.selftest.js`
`OES.ClientVM` over **real emitted bytecode** (dumped from the compiler / the
microclient base). Covers arithmetic, intra-module calls, 5b form-attr +
object-field binding, 5c server hop (mock transport), 5d Сообщить +
`ЭтаФорма.method`, 5e both fallback classes. Pure Node, no server:

```
node webClient/clientvm.selftest.js      # expect: all PASS, exit 0 (27 checks)
```

### Compiler — `enterprise/tests/test_compiler.cpp` (gtest)
`ClientServerDirectivesParseAndStampEnv`, `ExecCallClassify.RuleTable`,
`CompilerAOT.ExecEnvRoundTrips`, and DISABLED `ClientBytecodeDump.*` dumpers that
print the real bytecode used by the Node suite:

```
oes_tests --gtest_filter=*Compiler*:*ExecEnv*:*ExecCall*
oes_tests --gtest_also_run_disabled_tests --gtest_filter=*ClientBytecodeDump*   # re-dump bytecode
```

### End-to-end (live server) — the microclient base
Build a base with `&НаКлиенте`/`&НаСервере` handlers (`testbase/microclient.json`
via `oes_config_gen` → `designer /LoadCfg /UpdateDBCfg`), serve it, and drive the
browser. Verified behaviours (list form Товары):

- **form attribute** — click `КлиентТест`: `Сумма` increments in-page (0→1→2…),
  the output strip shows `Сообщить` text, **no `/action` POST**.
- **object field** — object form, click `ПлюсСто`: `Объект.Цена` 0→100→200 in-page,
  **no `/action` POST**.
- **server hop** — a `Сумма = УдвоитьНаСервере(Сумма) + 5` handler POSTs
  `/srv-call` and applies the returned context (10 → ×2 server → +5 = 25).

The dispatch hook lives in the `Button` onclick: `await OES.tryClientHandler(name)`
runs it in-page; `false` (unsupported / unknown) falls through to `POST /action`,
so a form with no client bytecode behaves exactly as before (the imported topasig
config, whose forms carry no form-module code, is fully server-driven — no
regression).
