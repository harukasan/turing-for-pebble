import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {
  loadCore,
  WasmSimulation,
  effective,
  buildMask,
  maskBit,
  maskLevels,
} from '../lib/wasm-simulation.ts';
import { FloatSimulation } from '../lib/float-simulation.ts';
import {
  CLOCK_FONTS,
  clockPixels,
  fontIndex,
  loadFonts,
} from '../lib/clock-fonts.ts';
globalThis.fetch = async (url) =>
  new Response(
    readFileSync(
      (url instanceof Request ? url.url : url.toString()).endsWith('.json')
        ? 'public/fonts/clock-fonts.json'
        : 'public/wasm/rd.wasm',
    ),
  );
const api = await loadCore();
await loadFonts();
const VALUE_ONE = 2 ** 24;
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
          api.rd_get(state, x, y, 0) / VALUE_ONE,
        );
        assert.equal(
          reference.field.b[index],
          api.rd_get(state, x, y, 1) / VALUE_ONE,
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
          api.rd_get(state, x, y, 1) / VALUE_ONE,
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

// The mask built by the core from its compiled glyphs equals the JSON
// glyphs the preview draws, widened by the square halo and mapped to cells.
const times = [
  new Date(2000, 0, 1, 0, 0),
  new Date(2046, 7, 29, 13, 57),
  new Date(2099, 11, 31, 23, 59),
];
for (const font of CLOCK_FONTS)
  for (const width of [100, 200])
    for (const halo of [0, 2, 3])
      for (const date of times) {
        const height = (width * 228) / 200;
        const expected = new Uint8Array(((width + 7) >> 3) * height);
        clockPixels(date, font, (px, py) => {
          for (
            let y = Math.max(0, py - halo);
            y <= Math.min(227, py + halo);
            y++
          )
            for (
              let x = Math.max(0, px - halo);
              x <= Math.min(199, px + halo);
              x++
            ) {
              const gx = Math.floor((x * width) / 200),
                gy = Math.floor((y * height) / 228);
              expected[gy * ((width + 7) >> 3) + (gx >> 3)] |= 1 << (gx & 7);
            }
        });
        const mask = buildMask(api, width, height, fontIndex(font), date, halo);
        assert.deepEqual(
          mask,
          expected,
          `${font} ${width} ${halo} ${date.toISOString()}`,
        );
      }

// Masked cells hold the equilibrium in both engines, through a step.
const clockDate = times[1];
for (const [engine, width] of [
  [0, 200],
  [1, 100],
  ['float', 100],
]) {
  const s =
    engine === 'float'
      ? new FloatSimulation(width, 42)
      : new WasmSimulation(api, engine, 42);
  if (engine !== 'float') assert.equal(s.components.length, 6);
  const mask = buildMask(api, s.width, s.height, 1, clockDate, 1);
  const levels = maskLevels(mask, s.width, s.height);
  s.setMask(mask);
  s.seedAt(Math.floor(s.width / 2), Math.floor(s.height / 2), 20);
  s.step(params, 1);
  let count = 0;
  for (let y = 0; y < s.height; y++)
    for (let x = 0; x < s.width; x++) {
      const level = levels[y * s.width + x];
      assert.equal(level === 0, maskBit(mask, s.width, x, y) === 1);
      assert.equal(s.maskLevel(x, y), level);
      if (level) continue;
      count++;
      assert.equal(s.get(x, y, 1), 0);
    }
  assert(count > 0);
  s.setMask(null);
  assert.equal(
    s.maskLevel(Math.floor(s.width / 2), Math.floor(s.height / 2)),
    5,
  );
  s.dispose();
}
console.log(
  'Clock masks: core glyphs equal the JSON glyphs for both fonts, mask levels agree and B stays 0 in masked cells',
);
