// =============================================================================
// OES.ClientVM — a minimal browser-side interpreter for &НаКлиенте form-module
// bytecode (managed-form client/server split, Increment 5a).
//
// It runs the CLIENT-env bytecode JSON served by GET /client-bytecode (emitted by
// wfrontendFormClientBytecode) — NOT a WASM port of ibProcUnit; a faithful subset
// re-implementation of the interpreter's ABI. Anything it does not implement
// (an opcode / a cross-frame extern / a server call) throws OESVMUnsupported, and
// the caller falls back to the existing server /action dispatch — so coverage gaps
// degrade to server execution, never to a broken form.
//
// ABI it mirrors (from src/engine/backend/compiler/procUnit.cpp):
//   * Each instruction has 4 operand pairs: p{1..4}{a,i}  (a = m_numArray = which
//     frame/module: 0 = the running function's LOCAL frame; else a module/context
//     slot — NOT supported here yet). i = m_numIndex = slot / const index / jump IP.
//   * TYPE_DELTA scheme: an arithmetic/compare opcode is  base + delta*(OPER_END+1)
//     where OPER_END+1 = 141; delta 0 = dynamic, 1 = number, 2 = string,
//     3 = date, 4 = boolean. So  op % 141  is the base opcode.
//   * OPER_CONST(6): local[p1i] = consts[p2i].   OPER_CONSTN(15): local[p1i] = p2i.
//   * OPER_LET(13): local[p1] = read(p2).  OPER_ADD/SUB/MULT/DIV/MOD: p1 = p2 (op) p3.
//   * OPER_IF(12): if empty(read(p1)) jump to p2i.   OPER_GOTO(9): jump to p1i.
//   * OPER_RET(8): if p1i != NORET return read(p1).  OPER_FUNC(29): entry marker (no-op here).
// This module is loaded both in the browser (client.html) and under node (self-test).
// =============================================================================
(function (root) {
  'use strict';

  // codeDef.h has 75 enumerators (OPER_NOP=0 … OPER_END=74), so the TYPE_DELTA
  // stride is OPER_END+1 = 75 (an arithmetic/compare op is base + delta*75).
  var OPER_END_PLUS_1 = 75;
  // Base opcodes, exact codeDef.h ordinal values.
  var OP = {
    NOP:0, ADD:1, DIV:2, MULT:3, SUB:4, NOT:5, AND:6, OR:7, RET:8, GOTO:9,
    FOR:10, FOREACH:11, IN:12, IF:13, LET:14, CONST:15, CONSTN:16, NEXT:17,
    NEXT_ITER:18, MOD:19, INVERT:20, ITER:21, GT:22, EQ:23, LS:24, GE:25,
    LE:26, NE:27, TRY:28, RAISE:29, RAISE_T:30, FUNC:31, ENDFUNC:32,
    CALL:37, SET:38, SETREF:39, SETCONST:40,
    // Member / bound-variable access (codeDef.h ordinals). SET_A/GET_A are
    // dotted-member get/set on an object base; the SCOPE / CONTEXT / EXTERN
    // family are the form-module's bound-variable handles (ЭтаФорма scope,
    // ThisObject/ThisForm context, export externs) — all resolved the same
    // parent + property-name way (procUnit.cpp: OPER_SET_SCOPE shares the
    // OPER_SET_A body, OPER_GET_SCOPE the OPER_GET_A body). Inc 5b binds
    // these to the client-side form context so &НаКлиенте edits
    // Объект.Реквизит and form attributes without a server hop.
    SET_A:52, GET_A:53,
    GET_EXTERN:68, SET_EXTERN:69, GET_SCOPE:70, SET_SCOPE:71,
    GET_CONTEXT:72, SET_CONTEXT:73,
    END:74
  };

  var DEF_VAR_NORET  = -1;    // OPER_RET sentinel: index1==NORET → procedure (no value)
  // Operand "array" code (ResolveRead/ResolveWrite, procUnit.cpp):
  //   array <= 0            → LOCAL frame slot [index]  (0 and negatives alike)
  //   array == DEF_VAR_CONST→ CONST pool [index]        (read only)
  //   array  > 0 (other)    → OUTER frame (module/context) — not supported in 5a
  var DEF_VAR_CONST  = 1000;
  // Declarator opcodes carried on the tape — no runtime effect (frame is pre-sized).
  var DECLARATOR = { 33:1 /*FUNC_PARAM*/, 34:1 /*FUNC_LOCAL*/, 35:1 /*CTX_BEGIN*/, 36:1 /*CTX_END*/ };

  function OESVMUnsupported(msg) { this.name = 'OESVMUnsupported'; this.message = msg; }
  OESVMUnsupported.prototype = Object.create(Error.prototype);

  // ---------------------------------------------------------------------------
  // Form context (Inc 5b). The server ships a `formCtx` block next to the
  // bytecode describing the mutable state a &НаКлиенте handler can touch:
  //   { attrs:  [ {name, value, controlId} ],           // form attributes (ЭтаФорма.<name>)
  //     objectName: "Объект",                            // the main-object handle name
  //     object: [ {name, value, controlId} ] }           // its fields (Объект.<name>)
  // The VM binds two proxies over it — the FORM SCOPE (resolves attribute
  // names + the object handle) and the OBJECT (resolves the object's fields) —
  // and records every write so the caller can push it back into the DOM
  // (dirty controls) with no server round-trip.
  // ---------------------------------------------------------------------------
  function num(v) { return v === '' || v === undefined || v === null ? 0 : (isNaN(Number(v)) ? v : Number(v)); }

  function FormContext(spec) {
    spec = spec || {};
    this.objectName = spec.objectName || null;
    this.attrs = {};       // name -> { value, controlId }
    this.object = {};      // name -> { value, controlId }
    this.dirty = {};       // controlId -> new value (what to re-render)
    (spec.attrs || []).forEach(function (a) {
      this.attrs[a.name] = { value: num(a.value), controlId: a.controlId };
    }, this);
    (spec.object || []).forEach(function (f) {
      this.object[f.name] = { value: num(f.value), controlId: f.controlId };
    }, this);
    this.objekt = new ObjektProxy(this);
    this.scope  = new FormScopeProxy(this);
  }
  FormContext.prototype.markDirty = function (cell) {
    if (cell && cell.controlId !== undefined && cell.controlId !== null)
      this.dirty[cell.controlId] = cell.value;
  };

  // The form scope (ЭтаФорма): a property is either the main-object handle
  // (returns the object proxy) or a form attribute (returns/sets its value).
  function FormScopeProxy(ctx) { this.__ctx = ctx; this.__isProxy = true; }
  FormScopeProxy.prototype.get = function (prop) {
    var ctx = this.__ctx;
    if (prop === ctx.objectName) return ctx.objekt;
    if (Object.prototype.hasOwnProperty.call(ctx.attrs, prop)) return ctx.attrs[prop].value;
    throw new OESVMUnsupported('form scope has no client member "' + prop + '"');
  };
  FormScopeProxy.prototype.set = function (prop, val) {
    var ctx = this.__ctx;
    if (Object.prototype.hasOwnProperty.call(ctx.attrs, prop)) {
      var cell = ctx.attrs[prop]; cell.value = val; ctx.markDirty(cell); return;
    }
    throw new OESVMUnsupported('cannot set form scope member "' + prop + '" on the client');
  };

  // The main object (Объект): its fields are the object's attributes.
  function ObjektProxy(ctx) { this.__ctx = ctx; this.__isProxy = true; }
  ObjektProxy.prototype.get = function (prop) {
    var ctx = this.__ctx;
    if (Object.prototype.hasOwnProperty.call(ctx.object, prop)) return ctx.object[prop].value;
    throw new OESVMUnsupported('object has no client field "' + prop + '"');
  };
  ObjektProxy.prototype.set = function (prop, val) {
    var ctx = this.__ctx;
    if (Object.prototype.hasOwnProperty.call(ctx.object, prop)) {
      var cell = ctx.object[prop]; cell.value = val; ctx.markDirty(cell); return;
    }
    throw new OESVMUnsupported('cannot set object field "' + prop + '" on the client');
  };

  // A loaded program: functions by name + the shared code / const tables +
  // (5b) the form context proxies.
  function ClientVM(json) {
    this.code = json.code || [];
    this.consts = (json.consts || []).map(decodeConst);
    this.fns = {};
    (json.functions || []).forEach(function (f) { this.fns[f.name] = f; }, this);
    this.ctx = new FormContext(json.formCtx);
  }

  // Const pool entry → JS value. ibValueTypes: 1=BOOL 2=NUMBER 3=DATE 4=STRING
  // (the tag the emitter wrote); value is the stringified form.
  function decodeConst(c) {
    switch (c.type) {
      case 2: return c.value === '' ? 0 : Number(c.value);   // NUMBER
      case 1: return c.value === 'true' || c.value === '1' || c.value === 'Да';  // BOOLEAN
      case 4: default: return c.value;                        // STRING / other
    }
  }

  function isEmpty(v) {
    return v === undefined || v === null || v === 0 || v === false || v === '';
  }

  ClientVM.prototype.hasClientFn = function (name) { return !!this.fns[name]; };

  // Run a client function by name. `args` seed the leading local slots (params).
  ClientVM.prototype.call = function (name, args) {
    var fn = this.fns[name];
    if (!fn) throw new OESVMUnsupported('no client function ' + name);
    var frame = newFrame(Number(fn.varCount) || 0);
    args = args || [];
    for (var a = 0; a < args.length && a < frame.length; a++) frame[a] = args[a];
    this._budget = GUARD_MAX;               // shared across the whole call tree
    this.ctx.dirty = {};                    // reset the re-render set for this run
    return this.execFrom(Number(fn.entry), frame);
  };

  // (Re)bind the form context from a spec — the browser calls this right
  // before call() with values read live from the DOM inputs, so a handler
  // sees the user's latest edits (Inc 5b).
  ClientVM.prototype.bindContext = function (spec) { this.ctx = new FormContext(spec); };

  // Controls the last call() mutated: { controlId: newValue }. The browser
  // side writes these back into the DOM inputs to re-render (Inc 5b), so a
  // client handler's effect on Объект.Реквизит / form attributes is visible
  // with no server round-trip.
  ClientVM.prototype.mutations = function () { return this.ctx.dirty; };

  function newFrame(n) { var f = new Array(n); for (var k = 0; k < n; k++) f[k] = undefined; return f; }
  var GUARD_MAX = 1000000;   // runaway backstop across the whole call tree

  // Execute the tape from `entryIp` over `frame` (the function's local slots),
  // returning the RET value. Recurses for OPER_CALL. Throws OESVMUnsupported for
  // anything outside the subset so the caller falls back to the server.
  ClientVM.prototype.execFrom = function (entryIp, frame) {
    var self = this;
    function read(arrA, idx) {
      var a = Number(arrA);
      if (a === DEF_VAR_CONST) return self.consts[idx];   // const pool
      if (a <= 0) return frame[idx];                        // local frame slot
      throw new OESVMUnsupported('cross-frame operand (extern/context/module)');
    }
    function write(arrA, idx, val) {
      if (Number(arrA) <= 0) { frame[idx] = val; return; }
      throw new OESVMUnsupported('cross-frame write (extern/context/module)');
    }
    // The BASE of a member / scope / context access. A local slot may hold a
    // proxy (e.g. the object handle fetched via GET_SCOPE); an outer-frame
    // operand (array > 0) is the form's bound scope — ЭтаФорма — which is the
    // only outer handle the client models (Inc 5b). Anything else the member
    // resolution below rejects, so the caller falls back to the server.
    function resolveBase(arrA, idx) {
      if (Number(arrA) <= 0) return frame[idx];
      return self.ctx.scope;
    }
    // Resolve `.prop` on a base that is either a form-context proxy or a plain
    // JS object. A non-object base means the client can't model it → server.
    function memberGet(bse, prop) {
      if (bse && bse.__isProxy) return bse.get(prop);
      if (bse && typeof bse === 'object') return bse[prop];
      throw new OESVMUnsupported('member get on non-object base');
    }
    function memberSet(bse, prop, val) {
      if (bse && bse.__isProxy) { bse.set(prop, val); return; }
      if (bse && typeof bse === 'object') { bse[prop] = val; return; }
      throw new OESVMUnsupported('member set on non-object base');
    }

    var ip = entryIp, ret;
    while (ip >= 0 && ip < this.code.length) {
      if (--self._budget < 0) throw new OESVMUnsupported('instruction budget exceeded');
      var u = this.code[ip];
      var raw = Number(u.op);
      var base = raw % OPER_END_PLUS_1;
      var delta = Math.floor(raw / OPER_END_PLUS_1);   // 0 dyn,1 num,2 str,3 date,4 bool

      if (DECLARATOR[base]) { ip++; continue; }        // FUNC_PARAM / FUNC_LOCAL / CTX_* — tape markers

      switch (base) {
        case OP.FUNC:  break;                               // entry marker — no-op
        case OP.NOP:   break;
        case OP.CONST: write(u.p1a, u.p1i, this.consts[u.p2i]); break;
        case OP.CONSTN: write(u.p1a, u.p1i, Number(u.p2i)); break;
        case OP.LET:   write(u.p1a, u.p1i, read(u.p2a, u.p2i)); break;
        case OP.ADD: {
          var l = read(u.p2a, u.p2i), r = read(u.p3a, u.p3i);
          write(u.p1a, u.p1i, (delta === 2 || typeof l === 'string' || typeof r === 'string')
            ? String(l) + String(r) : Number(l) + Number(r));
          break;
        }
        case OP.SUB:  write(u.p1a, u.p1i, Number(read(u.p2a,u.p2i)) - Number(read(u.p3a,u.p3i))); break;
        case OP.MULT: write(u.p1a, u.p1i, Number(read(u.p2a,u.p2i)) * Number(read(u.p3a,u.p3i))); break;
        case OP.DIV:  write(u.p1a, u.p1i, Number(read(u.p2a,u.p2i)) / Number(read(u.p3a,u.p3i))); break;
        case OP.MOD:  write(u.p1a, u.p1i, Number(read(u.p2a,u.p2i)) % Number(read(u.p3a,u.p3i))); break;
        case OP.INVERT: write(u.p1a, u.p1i, -Number(read(u.p2a,u.p2i))); break;
        case OP.NOT:  write(u.p1a, u.p1i, isEmpty(read(u.p2a,u.p2i))); break;
        case OP.AND:  write(u.p1a, u.p1i, !isEmpty(read(u.p2a,u.p2i)) && !isEmpty(read(u.p3a,u.p3i))); break;
        case OP.OR:   write(u.p1a, u.p1i, !isEmpty(read(u.p2a,u.p2i)) || !isEmpty(read(u.p3a,u.p3i))); break;
        case OP.EQ:   write(u.p1a, u.p1i, read(u.p2a,u.p2i) == read(u.p3a,u.p3i)); break;
        case OP.NE:   write(u.p1a, u.p1i, read(u.p2a,u.p2i) != read(u.p3a,u.p3i)); break;
        case OP.GT:   write(u.p1a, u.p1i, Number(read(u.p2a,u.p2i)) >  Number(read(u.p3a,u.p3i))); break;
        case OP.LS:   write(u.p1a, u.p1i, Number(read(u.p2a,u.p2i)) <  Number(read(u.p3a,u.p3i))); break;
        case OP.GE:   write(u.p1a, u.p1i, Number(read(u.p2a,u.p2i)) >= Number(read(u.p3a,u.p3i))); break;
        case OP.LE:   write(u.p1a, u.p1i, Number(read(u.p2a,u.p2i)) <= Number(read(u.p3a,u.p3i))); break;
        case OP.IF:   if (isEmpty(read(u.p1a, u.p1i))) { ip = Number(u.p2i) - 1; } break;
        case OP.GOTO: ip = Number(u.p1i) - 1; break;
        case OP.CALL: {
          // p1 = return dest; p2a = module (0 = this client module only);
          // p2i = callee entry IP; p3a = param count; p3i = callee var count.
          // The next p3a opcodes are OPER_SET/SETREF/SETCONST binding each arg.
          if (Number(u.p2a) !== 0)
            throw new OESVMUnsupported('cross-module call (server/common module)');
          var retA = u.p1a, retI = u.p1i;
          var calleeEntry = Number(u.p2i), pcount = Number(u.p3a), vcount = Number(u.p3i);
          var cf = newFrame(vcount);
          for (var pi = 0; pi < pcount; pi++) {
            ip++;
            var su = this.code[ip];
            var sbase = Number(su.op) % OPER_END_PLUS_1;
            if (sbase === OP.SETCONST) {
              if (Number(su.p1i) >= 0) cf[pi] = this.consts[Number(su.p1i)];   // default/omitted → leave undefined
            } else {                    // OPER_SET / OPER_SETREF — arg source is p1
              cf[pi] = read(su.p1a, su.p1i);
            }
          }
          write(retA, retI, this.execFrom(calleeEntry, cf));
          break;
        }
        // MEMBER read: dest = p1, base = p2, prop = consts[p3i].
        // (procUnit.cpp: OPER_GET_SCOPE shares the OPER_GET_A body — parent +
        //  property-name resolve. A bare form attribute `Сумма` compiles to
        //  GET_SCOPE(ЭтаФорма, "Сумма"); `Объект.Цена` to GET_A(Объект, "Цена").)
        case OP.GET_A:
        case OP.GET_SCOPE: {
          var gbase = resolveBase(u.p2a, u.p2i);
          var gprop = this.consts[Number(u.p3i)];
          write(u.p1a, u.p1i, memberGet(gbase, gprop));
          break;
        }
        // MEMBER write: base = p1, prop = consts[p2i], src = p3.
        // (OPER_SET_SCOPE shares the OPER_SET_A body.)
        case OP.SET_A:
        case OP.SET_SCOPE: {
          var sbaseV = resolveBase(u.p1a, u.p1i);
          var sprop = this.consts[Number(u.p2i)];
          memberSet(sbaseV, sprop, read(u.p3a, u.p3i));
          break;
        }
        // HANDLE copy: dest = p1, source handle = p2 (a bound context/export
        // slot). procUnit copies the staged handle value out to the dest temp;
        // here the only client-modelled handle is the form scope.
        case OP.GET_CONTEXT:
        case OP.GET_EXTERN:
          write(u.p1a, u.p1i, resolveBase(u.p2a, u.p2i));
          break;
        // SLOT copy back to a handle: dest slot = p1, source = p3 (rare —
        // handles are read-only in practice; kept for completeness).
        case OP.SET_CONTEXT:
        case OP.SET_EXTERN:
          write(u.p1a, u.p1i, read(u.p3a, u.p3i));
          break;
        case OP.RET:
          if (Number(u.p1i) !== DEF_VAR_NORET) ret = read(u.p1a, u.p1i);
          return ret;
        case OP.ENDFUNC: case OP.END: return ret;
        default:
          throw new OESVMUnsupported('opcode ' + base + ' not implemented (client VM)');
      }
      ip++;
    }
    return ret;
  };

  var api = { ClientVM: ClientVM, OESVMUnsupported: OESVMUnsupported, OP: OP };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;   // node self-test
  root.OES = root.OES || {};
  root.OES.ClientVM = api;                                                     // browser
})(typeof globalThis !== 'undefined' ? globalThis : this);
