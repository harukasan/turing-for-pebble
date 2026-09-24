import test from 'node:test';
import assert from 'node:assert/strict';
import { Simulation, presets } from './simulation.ts';
const base = {da:1,db:.5,dt:1,feed:.029,kill:.057};
test('homogeneous A remains at equilibrium',()=>{
  const s = new Simulation(50,57); s.a.fill(1); s.b.fill(0); s.step(base,10);
  assert.ok(s.a.every(v=>Math.abs(v-1)<1e-6)); assert.ok(s.b.every(v=>v===0));
});
test('same seed and parameters reproduce identical fields',()=>{
  const a=new Simulation(50,57,42), b=new Simulation(50,57,42), c=new Simulation(50,57,43);
  a.step(base,120); b.step(base,120); c.step(base,120);
  assert.deepEqual(a.b,b.b); assert.notDeepEqual(a.b,c.b); assert.equal(a.steps,120);
});
test('all presets stay finite and produce spatial variation after 2000 steps',()=>{
  for(const p of presets){
    const s=new Simulation(50,57);s.step({...base,...p},2000);
    assert.ok(s.a.every(v=>Number.isFinite(v)&&v>=0&&v<=1));
    assert.ok(s.b.every(v=>Number.isFinite(v)&&v>=0&&v<=1));
    assert.ok(Math.max(...s.b)-Math.min(...s.b)>.05,p.name);
  }
});
test('seed crosses periodic boundary',()=>{
  const s=new Simulation(50,57);s.a.fill(1);s.b.fill(0);s.seedAt(0,0,2);
  assert.equal(s.b[49],.25);assert.equal(s.b[56*50],.25);
});
