import test from 'node:test';
import assert from 'node:assert/strict';
import { Simulation, presets } from './simulation.ts';
import { FloatSimulation } from './float-simulation.ts';
const base = { da: 1, db: 0.5, dt: 1, feed: 0.029, kill: 0.057 };
void test('homogeneous A remains at equilibrium', () => {
  const s = new Simulation(50, 57);
  s.a.fill(1);
  s.b.fill(0);
  s.step(base, 10);
  assert.ok(s.a.every((v) => Math.abs(v - 1) < 1e-6));
  assert.ok(s.b.every((v) => v === 0));
});
void test('same seed and parameters reproduce identical fields', () => {
  const a = new Simulation(50, 57, 42),
    b = new Simulation(50, 57, 42),
    c = new Simulation(50, 57, 43);
  a.step(base, 120);
  b.step(base, 120);
  c.step(base, 120);
  assert.deepEqual(a.b, b.b);
  assert.notDeepEqual(a.b, c.b);
  assert.equal(a.steps, 120);
});
void test('baseline presets stay finite and produce spatial variation after 2000 steps', () => {
  for (const p of presets.slice(0, 4)) {
    const s = new Simulation(50, 57);
    s.step({ ...base, ...p }, 2000);
    assert.ok(s.a.every((v) => Number.isFinite(v) && v >= 0 && v <= 1));
    assert.ok(s.b.every((v) => Number.isFinite(v) && v >= 0 && v <= 1));
    assert.ok(Math.max(...s.b) - Math.min(...s.b) > 0.05, p.name);
  }
});
void test('thin-line preset develops variation with display-coordinate seeds', () => {
  const s = new FloatSimulation(100, 42);
  s.step({ ...base, ...presets[4] });
  s.field.step({ ...base, ...presets[4] }, 1999);
  assert.ok(Math.max(...s.field.b) - Math.min(...s.field.b) > 0.05);
});
void test('seed crosses periodic boundary', () => {
  const s = new Simulation(50, 57);
  s.a.fill(1);
  s.b.fill(0);
  s.seedAt(0, 0, 2);
  assert.equal(s.b[49], 0.25);
  assert.equal(s.b[56 * 50], 0.25);
});
