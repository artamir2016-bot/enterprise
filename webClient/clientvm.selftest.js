// Node self-test for OES.ClientVM against REAL emitted bytecode (dumped by the
// DISABLED ClientBytecodeDump gtests and from the live microclient base).
// Run: node webClient/clientvm.selftest.js
//
// call() is async (Inc 5c: a Server-env call hops over HTTP mid-handler), so the
// whole suite awaits.
'use strict';
var vm = require('./clientvm.js');

var fail = 0;
function check(name, got, want) {
  var ok = got === want;
  console.log((ok ? 'PASS ' : 'FAIL ') + name + '  got=' + got + ' want=' + want);
  if (!ok) fail++;
}

// Real bytecode for:  &AtClient Function Calc(a, b)  c = a*b;  Return c + 2;  EndFunction
var prog = {
  functions: [ { name:'Calc', entry:0, isFunc:true, varCount:5, params:2, env:'client' } ],
  code: [
    {op:31,p1a:0,p1i:0,p2a:0,p2i:0,p3a:2,p3i:5,p4a:0,p4i:0},        // FUNC
    {op:33,p1a:0,p1i:0,p2a:-1,p2i:-1,p3a:0,p3i:0,p4a:0,p4i:0},      // FUNC_PARAM a -> slot0
    {op:33,p1a:0,p1i:1,p2a:-1,p2i:-1,p3a:0,p3i:0,p4a:0,p4i:0},      // FUNC_PARAM b -> slot1
    {op:3, p1a:0,p1i:2,p2a:0,p2i:0,p3a:0,p3i:1,p4a:0,p4i:0},        // MULT slot2 = slot0*slot1
    {op:1, p1a:-3,p1i:4,p2a:0,p2i:2,p3a:1000,p3i:0,p4a:0,p4i:0},    // ADD slot4 = slot2 + const[0](=2)
    {op:8, p1a:-3,p1i:4,p2a:0,p2i:0,p3a:0,p3i:0,p4a:0,p4i:0},       // RET slot4
    {op:32,p1a:0,p1i:0,p2a:0,p2i:0,p3a:0,p3i:0,p4a:0,p4i:0},        // ENDFUNC
    {op:74,p1a:0,p1i:0,p2a:0,p2i:0,p3a:0,p3i:0,p4a:0,p4i:0}         // END
  ],
  consts: [ {type:2, value:'2'} ]
};

// Real bytecode for an intra-module CALL:
//   &AtClient Function Inner(y)  Return y*2;   EndFunction
//   &AtClient Function Outer(x)  Return Inner(x) + 1;  EndFunction
var callProg = {
  functions: [
    { name:'Inner', entry:0, isFunc:true, varCount:2, params:1, env:'client' },
    { name:'Outer', entry:5, isFunc:true, varCount:3, params:1, env:'client' }
  ],
  code: [
    {op:31,p1a:0,p1i:0,p2a:0,p2i:0,p3a:1,p3i:2,p4a:0,p4i:0},        // 0 FUNC Inner
    {op:33,p1a:0,p1i:0,p2a:-1,p2i:-1,p3a:0,p3i:0,p4a:0,p4i:0},      // 1 FUNC_PARAM y
    {op:3, p1a:-3,p1i:1,p2a:0,p2i:0,p3a:1000,p3i:0,p4a:0,p4i:0},    // 2 MULT slot1 = y * const[0](2)
    {op:8, p1a:-3,p1i:1,p2a:0,p2i:0,p3a:0,p3i:0,p4a:0,p4i:0},       // 3 RET slot1
    {op:32,p1a:0,p1i:0,p2a:0,p2i:0,p3a:0,p3i:0,p4a:0,p4i:0},        // 4 ENDFUNC
    {op:31,p1a:0,p1i:0,p2a:0,p2i:0,p3a:1,p3i:3,p4a:0,p4i:0},        // 5 FUNC Outer
    {op:33,p1a:0,p1i:0,p2a:-1,p2i:-1,p3a:0,p3i:0,p4a:0,p4i:0},      // 6 FUNC_PARAM x
    {op:37,p1a:-3,p1i:1,p2a:0,p2i:0,p3a:1,p3i:2,p4a:0,p4i:0},       // 7 CALL Inner(entry0,1 param,vc2)->slot1
    {op:38,p1a:0,p1i:0,p2a:0,p2i:0,p3a:0,p3i:0,p4a:0,p4i:0},        // 8 SET arg0 = x (slot0)
    {op:1, p1a:-3,p1i:2,p2a:-3,p2i:1,p3a:1000,p3i:1,p4a:0,p4i:0},   // 9 ADD slot2 = callret + const[1](1)
    {op:8, p1a:-3,p1i:2,p2a:0,p2i:0,p3a:0,p3i:0,p4a:0,p4i:0},       // 10 RET slot2
    {op:32,p1a:0,p1i:0,p2a:0,p2i:0,p3a:0,p3i:0,p4a:0,p4i:0},        // 11 ENDFUNC
    {op:74,p1a:0,p1i:0,p2a:0,p2i:0,p3a:0,p3i:0,p4a:0,p4i:0}         // 12 END
  ],
  consts: [ {type:2,value:'2'}, {type:2,value:'1'} ]
};

// Inc 5b — FORM ATTRIBUTE (list form):  Сумма = Сумма + 5.
var attrProg = {
  functions: [ { name:'ТестКлиент', entry:0, isFunc:false, varCount:3, params:1, env:'client' } ],
  code: [
    {op:31,p1a:0,p1i:0,p2a:0,p2i:0,p3a:1,p3i:3,p4a:0,p4i:0},        // FUNC
    {op:33,p1a:0,p1i:0,p2a:-1,p2i:-1,p3a:0,p3i:0,p4a:0,p4i:0},      // FUNC_PARAM Команда
    {op:70,p1a:-3,p1i:1,p2a:1,p2i:18,p3a:0,p3i:0,p4a:0,p4i:0},      // GET_SCOPE slot1 = scope."Сумма"
    {op:1, p1a:-3,p1i:2,p2a:-3,p2i:1,p3a:1000,p3i:1,p4a:0,p4i:0},   // ADD slot2 = slot1 + const[1](5)
    {op:71,p1a:1,p1i:18,p2a:0,p2i:0,p3a:-3,p3i:2,p4a:0,p4i:0},      // SET_SCOPE scope."Сумма" = slot2
    {op:32,p1a:0,p1i:0,p2a:0,p2i:0,p3a:0,p3i:0,p4a:0,p4i:0},        // ENDFUNC
    {op:74,p1a:0,p1i:0,p2a:0,p2i:0,p3a:0,p3i:0,p4a:0,p4i:0}         // END
  ],
  consts: [ {type:4,value:'Сумма'}, {type:2,value:'5'} ],
  formCtx: { attrs: [ { name:'Сумма', value:'10', controlId: 3 } ], objectName:null, object: [] }
};

// Inc 5b — MAIN OBJECT FIELD (object form):  Объект.Цена = Объект.Цена + 100.
var objProg = {
  functions: [ { name:'Добавить100', entry:0, isFunc:false, varCount:5, params:1, env:'client' } ],
  code: [
    {op:31,p1a:0,p1i:0,p2a:0,p2i:0,p3a:1,p3i:5,p4a:0,p4i:0},        // FUNC
    {op:33,p1a:0,p1i:0,p2a:-1,p2i:-1,p3a:0,p3i:0,p4a:0,p4i:0},      // FUNC_PARAM Команда
    {op:70,p1a:-3,p1i:1,p2a:1,p2i:17,p3a:0,p3i:0,p4a:0,p4i:0},      // GET_SCOPE slot1 = scope."Объект"
    {op:70,p1a:-3,p1i:2,p2a:1,p2i:17,p3a:0,p3i:0,p4a:0,p4i:0},      // GET_SCOPE slot2 = scope."Объект"
    {op:53,p1a:-3,p1i:3,p2a:-3,p2i:2,p3a:0,p3i:1,p4a:0,p4i:0},      // GET_A slot3 = slot2."Цена"
    {op:1, p1a:-3,p1i:4,p2a:-3,p2i:3,p3a:1000,p3i:2,p4a:0,p4i:0},   // ADD slot4 = slot3 + const[2](100)
    {op:52,p1a:-3,p1i:1,p2a:0,p2i:1,p3a:-3,p3i:4,p4a:0,p4i:0},      // SET_A slot1."Цена" = slot4
    {op:32,p1a:0,p1i:0,p2a:0,p2i:0,p3a:0,p3i:0,p4a:0,p4i:0},        // ENDFUNC
    {op:74,p1a:0,p1i:0,p2a:0,p2i:0,p3a:0,p3i:0,p4a:0,p4i:0}         // END
  ],
  consts: [ {type:4,value:'Объект'}, {type:4,value:'Цена'}, {type:2,value:'100'} ],
  formCtx: { attrs: [], objectName:'Объект', object: [ { name:'Цена', value:'7', controlId: 5 } ] }
};

// Inc 5c — SERVER HOP. REAL bytecode dumped from the microclient list form:
//   &НаСервере Функция УдвоитьНаСервере(Знач Ч)  Возврат Ч * 2;  КонецФункции
//   &НаКлиенте Процедура ТестКлиент(Команда)     Сумма = УдвоитьНаСервере(Сумма) + 5;
// The client fetches Сумма (10), calls the SERVER proc (transport doubles → 20),
// then + 5 = 25.  The server proc's body is on the tape but never run client-side.
var srvProg = {
  functions: [
    { name:'УдвоитьНаСервере', entry:0, isFunc:true,  varCount:2, params:1, env:'server' },
    { name:'ТестКлиент',       entry:5, isFunc:false, varCount:4, params:1, env:'client' }
  ],
  code: [
    {op:31,p1a:0,p1i:0,p2a:0,p2i:0,p3a:1,p3i:2,p4a:0,p4i:0},        // 0 FUNC УдвоитьНаСервере
    {op:33,p1a:1,p1i:0,p2a:-1,p2i:-1,p3a:0,p3i:0,p4a:0,p4i:0},      // 1 FUNC_PARAM Ч
    {op:3, p1a:-3,p1i:1,p2a:0,p2i:0,p3a:1000,p3i:0,p4a:0,p4i:0},    // 2 MULT slot1 = Ч * const[0](2)  [server-only]
    {op:8, p1a:-3,p1i:1,p2a:0,p2i:0,p3a:0,p3i:0,p4a:0,p4i:0},       // 3 RET slot1
    {op:32,p1a:0,p1i:0,p2a:0,p2i:0,p3a:0,p3i:0,p4a:0,p4i:0},        // 4 ENDFUNC
    {op:31,p1a:0,p1i:0,p2a:0,p2i:0,p3a:1,p3i:4,p4a:0,p4i:0},        // 5 FUNC ТестКлиент
    {op:33,p1a:0,p1i:0,p2a:-1,p2i:-1,p3a:0,p3i:0,p4a:0,p4i:0},      // 6 FUNC_PARAM Команда
    {op:70,p1a:-3,p1i:1,p2a:1,p2i:18,p3a:0,p3i:1,p4a:0,p4i:0},      // 7 GET_SCOPE slot1 = scope."Сумма"
    {op:37,p1a:-3,p1i:2,p2a:0,p2i:0,p3a:1,p3i:2,p4a:0,p4i:0},       // 8 CALL УдвоитьНаСервере(entry0)->slot2
    {op:38,p1a:-3,p1i:1,p2a:0,p2i:1,p3a:0,p3i:0,p4a:0,p4i:0},       // 9 SET arg0 = slot1 (Сумма)
    {op:1, p1a:-3,p1i:3,p2a:-3,p2i:2,p3a:1000,p3i:2,p4a:0,p4i:0},   // 10 ADD slot3 = ret + const[2](5)
    {op:71,p1a:1,p1i:18,p2a:0,p2i:1,p3a:-3,p3i:3,p4a:0,p4i:0},      // 11 SET_SCOPE scope."Сумма" = slot3
    {op:32,p1a:0,p1i:0,p2a:0,p2i:0,p3a:0,p3i:0,p4a:0,p4i:0},        // 12 ENDFUNC
    {op:74,p1a:0,p1i:0,p2a:0,p2i:0,p3a:0,p3i:0,p4a:0,p4i:0}         // 13 END
  ],
  consts: [ {type:2,value:'2'}, {type:4,value:'Сумма'}, {type:2,value:'5'} ],
  formCtx: { attrs: [ { name:'Сумма', value:'10', controlId: 3 } ], objectName:null, object: [] }
};

(async function main() {
  var machine = new vm.ClientVM(prog);
  check('Calc(3,4)=a*b+2', await machine.call('Calc', [3, 4]), 14);
  check('Calc(0,0)=2',     await machine.call('Calc', [0, 0]), 2);
  check('Calc(5,5)=27',    await machine.call('Calc', [5, 5]), 27);
  check('Calc(-2,3)=-4',   await machine.call('Calc', [-2, 3]), -4);
  check('hasClientFn',     machine.hasClientFn('Calc'), true);

  var m2 = new vm.ClientVM(callProg);
  check('Inner(5)=10',         await m2.call('Inner', [5]), 10);
  check('Outer(3)=Inner*+1=7', await m2.call('Outer', [3]), 7);
  check('Outer(10)=21',        await m2.call('Outer', [10]), 21);

  var ma = new vm.ClientVM(attrProg);
  await ma.call('ТестКлиент', [null]);
  check('Сумма 10 -> 15',          ma.ctx.attrs['Сумма'].value, 15);
  check('Сумма dirties control 3', ma.mutations()[3], 15);

  var mo = new vm.ClientVM(objProg);
  await mo.call('Добавить100', [null]);
  check('Объект.Цена 7 -> 107',    mo.ctx.object['Цена'].value, 107);
  check('Цена dirties control 5',  mo.mutations()[5], 107);

  // 5c — server hop. Mock transport doubles the single arg; returns no context.
  var hops = [];
  var transport = function (proc, args, context) {
    hops.push({ proc: proc, args: args, context: context });
    return Promise.resolve({ ret: 2 * Number(args[0]), context: null });
  };
  var ms = new vm.ClientVM(srvProg, { transport: transport });
  check('hasClientFn(ТестКлиент)',        ms.hasClientFn('ТестКлиент'), true);
  check('server proc is NOT a client fn', ms.hasClientFn('УдвоитьНаСервере'), false);
  await ms.call('ТестКлиент', [null]);
  check('hop count = 1',            hops.length, 1);
  check('hop proc name',            hops[0] && hops[0].proc, 'УдвоитьНаСервере');
  check('hop arg = Сумма (10)',     hops[0] && hops[0].args[0], 10);
  check('hop shipped context',      hops[0] && hops[0].context.attrs['Сумма'], 10);
  check('Сумма = 2*10 + 5 = 25',    ms.ctx.attrs['Сумма'].value, 25);
  check('Сумма dirties control 3',  ms.mutations()[3], 25);

  process.exit(fail ? 1 : 0);
})();
