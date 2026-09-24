import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { loadCore, WasmSimulation, effective } from '../lib/wasm-simulation.ts';
import { FloatSimulation } from '../lib/float-simulation.ts';
globalThis.fetch = async () =>
  new Response(readFileSync('public/wasm/rd.wasm'));
const api = await loadCore();
const pixels = { data: new Uint8ClampedArray(200 * 228 * 4) };
const params = { feed: 0.029, kill: 0.057, da: 1, db: 0.5, dt: 1 };
assert.equal(effective(params).feed, 950 / 32768);
for (let i = 0; i < 30; i++) {
  const s = new WasmSimulation(api, i % 2, 42);
  assert.equal(s.steps, 0);
  s.step(params, 10);
  assert.equal(s.steps, 10);
  s.seedAt(0, 0);
  s.render(pixels, 'green', true);
  for (let p = 0; p < pixels.data.length; p += 4) {
    assert.equal(pixels.data[p + 3], 255);
    for (let c = 0; c < 3; c++) assert.equal(pixels.data[p + c] % 85, 0);
  }
  s.dispose();
  s.dispose();
}
for (const [mode, width] of [
  [0, 200],
  [1, 100],
]) {
  const reference = new FloatSimulation(width, 42);
  const bytes = api.rd_bytes(mode);
  const allocation = api.malloc(bytes);
  const state = api.rd_init(allocation, bytes, mode, 42);
  assert(state);
  for (let y = 0; y < reference.height; y++)
    for (let x = 0; x < width; x++) {
      const index = y * width + x;
      assert.equal(
        reference.field.b[index] > 0,
        api.rd_get(state, x, y, 1) > 0,
      );
      if (mode === 1) {
        assert.equal(
          reference.field.a[index],
          api.rd_get(state, x, y, 0) / 32768,
        );
        assert.equal(
          reference.field.b[index],
          api.rd_get(state, x, y, 1) / 32768,
        );
      }
    }
  reference.seedAt(0, 0);
  assert.equal(api.rd_seed(state, 0, 0, 6), 0);
  if (mode === 1)
    for (let y = 0; y < reference.height; y++)
      for (let x = 0; x < width; x++) {
        const index = y * width + x;
        assert.equal(
          reference.field.b[index],
          api.rd_get(state, x, y, 1) / 32768,
        );
      }
  reference.step(params);
  assert.equal(reference.steps, 1);
  reference.render(pixels, 'green', true);
  assert(
    pixels.data.every((value, i) =>
      i % 4 === 3 ? value === 255 : value % 85 === 0,
    ),
  );
  api.free(allocation);
}
console.log(
  'TypeScript adapters: Wasm loading, 30 resets, Float32 initial fields, stepping, seeding, RGB2 rows, disposal passed',
);
