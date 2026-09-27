// Node self-test for OES.ClientVM against REAL emitted bytecode (dumped by the
// DISABLED ClientBytecodeDump.ArithFn gtest). Run: node webClient/clientvm.selftest.js
'use strict';
var vm = require('./clientvm.js');

// Real bytecode for:  &AtClient Function Calc(a, b)  c = a*b;  Return c + 2;  EndFunction
var prog = {
  functions: [ { name:'Calc', entry:0, isFunc:true, varCount:5, params:2 } ],
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

var machine = new vm.ClientVM(prog);
var fail = 0;
function check(name, got, want) {
  var ok = got === want;
  console.log((ok ? 'PASS ' : 'FAIL ') + name + '  got=' + got + ' want=' + want);
  if (!ok) fail++;
}

check('Calc(3,4)=a*b+2', machine.call('Calc', [3, 4]), 14);
check('Calc(0,0)=2',     machine.call('Calc', [0, 0]), 2);
check('Calc(5,5)=27',    machine.call('Calc', [5, 5]), 27);
check('Calc(-2,3)=-4',   machine.call('Calc', [-2, 3]), -4);
check('hasClientFn',     machine.hasClientFn('Calc'), true);

// Real bytecode for an intra-module CALL:
//   &AtClient Function Inner(y)  Return y*2;   EndFunction
//   &AtClient Function Outer(x)  Return Inner(x) + 1;  EndFunction
var callProg = {
  functions: [
    { name:'Inner', entry:0, isFunc:true, varCount:2, params:1 },
    { name:'Outer', entry:5, isFunc:true, varCount:3, params:1 }
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
var m2 = new vm.ClientVM(callProg);
check('Inner(5)=10',     m2.call('Inner', [5]), 10);
check('Outer(3)=Inner*+1=7', m2.call('Outer', [3]), 7);   // Inner(3)=6, +1 = 7
check('Outer(10)=21',    m2.call('Outer', [10]), 21);      // Inner(10)=20, +1 = 21

// ---------------------------------------------------------------------------
// Inc 5b — form-context binding. REAL bytecode dumped from the microclient
// base (GET /client-bytecode) for two &НаКлиенте handlers.
// ---------------------------------------------------------------------------

// FORM ATTRIBUTE — list form:  &НаКлиенте Процедура ТестКлиент(Команда) { Сумма = Сумма + 5; }
//   op 70 GET_SCOPE (ЭтаФорма."Сумма" -> slot1),  op 1 ADD slot2 = slot1 + const[1](5),
//   op 71 SET_SCOPE (ЭтаФорма."Сумма" = slot2).  Base is the form scope (array 1, slot 18).
var attrProg = {
  functions: [ { name:'ТестКлиент', entry:0, isFunc:false, varCount:3, params:1 } ],
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
var ma = new vm.ClientVM(attrProg);
ma.call('ТестКлиент', [null]);
check('Сумма 10 -> 15',          ma.ctx.attrs['Сумма'].value, 15);
check('Сумма dirties control 3', ma.mutations()[3], 15);

// MAIN OBJECT FIELD — object form:  &НаКлиенте Процедура Добавить100(Команда) { Объект.Цена = Объект.Цена + 100; }
//   op 70 GET_SCOPE (ЭтаФорма."Объект" -> slot1),  op 70 (-> slot2),
//   op 53 GET_A (slot2."Цена" -> slot3),  op 1 ADD slot4 = slot3 + const[2](100),
//   op 52 SET_A (slot1."Цена" = slot4).
var objProg = {
  functions: [ { name:'Добавить100', entry:0, isFunc:false, varCount:5, params:1 } ],
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
var mo = new vm.ClientVM(objProg);
mo.call('Добавить100', [null]);
check('Объект.Цена 7 -> 107',        mo.ctx.object['Цена'].value, 107);
check('Цена dirties control 5',      mo.mutations()[5], 107);

process.exit(fail ? 1 : 0);
