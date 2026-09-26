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

process.exit(fail ? 1 : 0);
