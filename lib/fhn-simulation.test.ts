import test from "node:test";
import assert from "node:assert/strict";
import { FhnSimulation } from "./fhn-simulation.ts";
import { FloatSimulation } from "./float-simulation.ts";
import {
  presetById,
  presetParameters,
  type FhnParameters,
} from "./simulation.ts";

const preset = (id: string) =>
  presetParameters(presetById(id)!) as FhnParameters;

/** Fraction of 10 x 10 cell blocks holding a cell with |u - rest| > 0.25. */
function fill(u: Float32Array, width: number, height: number, rest: number) {
  let blocks = 0,
    filled = 0;
  for (let by = 0; by < height; by += 10)
    for (let bx = 0; bx < width; bx += 10) {
      blocks++;
      let found = false;
      for (let y = by; y < Math.min(height, by + 10) && !found; y++)
        for (let x = bx; x < Math.min(width, bx + 10) && !found; x++)
          found = Math.abs(u[y * width + x] - rest) > 0.25;
      filled += Number(found);
    }
  return filled / blocks;
}

const inRange = (values: Float32Array) =>
  values.every((v) => Number.isFinite(v) && v >= -2 && v <= 2);

void test("a uniform field at rest 0 with k = 0 stays exactly 0", () => {
  const s = new FhnSimulation(40, 45);
  const p = {
    du: 1,
    dv: 1,
    ru: 0.25,
    rv: 1,
    av: 1,
    k: 0,
    dt: 1,
    rest: 0,
    init: 0,
  };
  s.fill(p);
  s.step(p, 20);
  assert.ok(s.u.every((v) => v === 0));
  assert.ok(s.v.every((v) => v === 0));
});

void test("the same seed and parameters reproduce identical fields", () => {
  const p = preset("fhn-stripes");
  const a = new FloatSimulation(60, 42, p),
    b = new FloatSimulation(60, 42, p),
    c = new FloatSimulation(60, 43, p);
  for (let i = 0; i < 100; i++) {
    a.step(p);
    b.step(p);
    c.step(p);
  }
  assert.ok(a.field instanceof FhnSimulation);
  assert.ok(b.field instanceof FhnSimulation);
  assert.ok(c.field instanceof FhnSimulation);
  assert.deepEqual(a.field.u, b.field.u);
  assert.notDeepEqual(a.field.u, c.field.u);
  assert.equal(a.steps, 100);
});

void test("the stripes preset fills the watch grid within 3,000 steps", () => {
  const p = preset("fhn-stripes");
  const s = new FloatSimulation(120, 42, p);
  assert.ok(s.field instanceof FhnSimulation);
  s.field.step(p, 3000);
  const u = s.field.u;
  assert.ok(Math.max(...u) - Math.min(...u) > 1);
  assert.ok(fill(u, 120, 136, p.rest) >= 0.9);
  assert.ok(inRange(u) && inRange(s.field.v));
});

void test("the spiral preset keeps moving after 3,000 steps", () => {
  const p = preset("fhn-spiral");
  const s = new FloatSimulation(120, 42, p);
  assert.ok(s.field instanceof FhnSimulation);
  s.field.step(p, 3000);
  const before = s.field.u.slice();
  s.field.step(p, 1);
  let change = 0;
  for (let i = 0; i < before.length; i++)
    change += Math.abs(s.field.u[i] - before[i]);
  assert.ok(change / before.length > 0.005);
  assert.ok(Math.max(...s.field.u) > 0.5);
  assert.ok(inRange(s.field.u) && inRange(s.field.v));
});

void test("masked cells hold u at rest", () => {
  const p = preset("fhn-hex");
  const s = new FloatSimulation(100, 42, p);
  const mask = new Uint8Array(((100 + 7) >> 3) * 114);
  for (let y = 40; y < 70; y++)
    for (let x = 30; x < 60; x++) mask[y * 13 + (x >> 3)] |= 1 << (x & 7);
  s.setMask(mask);
  s.seedAt(45, 55, 30);
  for (let i = 0; i < 200; i++) s.step(p);
  assert.ok(s.field instanceof FhnSimulation);
  let masked = 0;
  for (let y = 0; y < 114; y++)
    for (let x = 0; x < 100; x++) {
      if (s.maskLevel(x, y)) continue;
      masked++;
      assert.equal(s.field.u[y * 100 + x], Math.fround(p.rest));
      assert.equal(s.get(x, y, 1), (Math.fround(p.rest) + 2) / 4);
    }
  assert.equal(masked, 30 * 30);
  assert.ok(inRange(s.field.u) && inRange(s.field.v));
});

void test("parameters of the other model are rejected", () => {
  const s = new FloatSimulation(50, 42, preset("fhn-stripes"));
  assert.throws(() => s.step(presetParameters(presetById("maze")!)));
});
