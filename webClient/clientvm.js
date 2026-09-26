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

  // A loaded program: functions by name + the shared code / const tables.
  function ClientVM(json) {
    this.code = json.code || [];
    this.consts = (json.consts || []).map(decodeConst);
    this.fns = {};
    (json.functions || []).forEach(function (f) { this.fns[f.name] = f; }, this);
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
  // Returns the function's value (or undefined for a procedure). Throws
  // OESVMUnsupported the moment it meets something outside the 5a subset.
  ClientVM.prototype.call = function (name, args) {
    var fn = this.fns[name];
    if (!fn) throw new OESVMUnsupported('no client function ' + name);

    var frame = new Array(Number(fn.varCount) || 0);
    for (var k = 0; k < frame.length; k++) frame[k] = undefined;
    args = args || [];
    for (var a = 0; a < args.length && a < frame.length; a++) frame[a] = args[a];

    var self = this;
    function readLocal(arrA, idx) {
      var a = Number(arrA);
      if (a === DEF_VAR_CONST) return self.consts[idx];   // const pool
      if (a <= 0) return frame[idx];                       // local frame slot
      throw new OESVMUnsupported('cross-frame operand (extern/context/module)');
    }
    function writeLocal(arrA, idx, val) {
      if (Number(arrA) <= 0) { frame[idx] = val; return; } // local frame slot
      throw new OESVMUnsupported('cross-frame write (extern/context/module)');
    }

    var ip = Number(fn.entry);          // points at OPER_FUNC
    var guard = 0, GUARD_MAX = 1000000; // runaway backstop
    var ret;

    while (ip >= 0 && ip < this.code.length) {
      if (++guard > GUARD_MAX) throw new OESVMUnsupported('instruction budget exceeded');
      var u = this.code[ip];
      var raw = Number(u.op);
      var base = raw % OPER_END_PLUS_1;
      var delta = Math.floor(raw / OPER_END_PLUS_1);   // 0 dyn,1 num,2 str,3 date,4 bool

      if (DECLARATOR[base]) { ip++; continue; }        // FUNC_PARAM / FUNC_LOCAL / CTX_* — tape markers

      switch (base) {
        case OP.FUNC:  break;                               // entry marker — no-op
        case OP.NOP:   break;
        case OP.CONST: writeLocal(u.p1a, u.p1i, this.consts[u.p2i]); break;
        case OP.CONSTN: writeLocal(u.p1a, u.p1i, Number(u.p2i)); break;
        case OP.LET:   writeLocal(u.p1a, u.p1i, readLocal(u.p2a, u.p2i)); break;
        case OP.ADD: {
          var l = readLocal(u.p2a, u.p2i), r = readLocal(u.p3a, u.p3i);
          writeLocal(u.p1a, u.p1i, (delta === 2 || typeof l === 'string' || typeof r === 'string')
            ? String(l) + String(r) : Number(l) + Number(r));
          break;
        }
        case OP.SUB:  writeLocal(u.p1a, u.p1i, Number(readLocal(u.p2a,u.p2i)) - Number(readLocal(u.p3a,u.p3i))); break;
        case OP.MULT: writeLocal(u.p1a, u.p1i, Number(readLocal(u.p2a,u.p2i)) * Number(readLocal(u.p3a,u.p3i))); break;
        case OP.DIV:  writeLocal(u.p1a, u.p1i, Number(readLocal(u.p2a,u.p2i)) / Number(readLocal(u.p3a,u.p3i))); break;
        case OP.MOD:  writeLocal(u.p1a, u.p1i, Number(readLocal(u.p2a,u.p2i)) % Number(readLocal(u.p3a,u.p3i))); break;
        case OP.INVERT: writeLocal(u.p1a, u.p1i, -Number(readLocal(u.p2a,u.p2i))); break;
        case OP.NOT:  writeLocal(u.p1a, u.p1i, isEmpty(readLocal(u.p2a,u.p2i))); break;
        case OP.AND:  writeLocal(u.p1a, u.p1i, !isEmpty(readLocal(u.p2a,u.p2i)) && !isEmpty(readLocal(u.p3a,u.p3i))); break;
        case OP.OR:   writeLocal(u.p1a, u.p1i, !isEmpty(readLocal(u.p2a,u.p2i)) || !isEmpty(readLocal(u.p3a,u.p3i))); break;
        case OP.EQ:   writeLocal(u.p1a, u.p1i, readLocal(u.p2a,u.p2i) == readLocal(u.p3a,u.p3i)); break;
        case OP.NE:   writeLocal(u.p1a, u.p1i, readLocal(u.p2a,u.p2i) != readLocal(u.p3a,u.p3i)); break;
        case OP.GT:   writeLocal(u.p1a, u.p1i, Number(readLocal(u.p2a,u.p2i)) >  Number(readLocal(u.p3a,u.p3i))); break;
        case OP.LS:   writeLocal(u.p1a, u.p1i, Number(readLocal(u.p2a,u.p2i)) <  Number(readLocal(u.p3a,u.p3i))); break;
        case OP.GE:   writeLocal(u.p1a, u.p1i, Number(readLocal(u.p2a,u.p2i)) >= Number(readLocal(u.p3a,u.p3i))); break;
        case OP.LE:   writeLocal(u.p1a, u.p1i, Number(readLocal(u.p2a,u.p2i)) <= Number(readLocal(u.p3a,u.p3i))); break;
        case OP.IF:   if (isEmpty(readLocal(u.p1a, u.p1i))) { ip = Number(u.p2i) - 1; } break;
        case OP.GOTO: ip = Number(u.p1i) - 1; break;
        case OP.RET:
          if (Number(u.p1i) !== DEF_VAR_NORET) ret = readLocal(u.p1a, u.p1i);
          return ret;
        case OP.ENDFUNC: case OP.END: return ret;
        default:
          throw new OESVMUnsupported('opcode ' + base + ' not implemented (client VM 5a)');
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
