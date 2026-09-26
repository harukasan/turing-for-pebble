import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import assert from 'node:assert/strict';
const {
  instance: { exports: e },
} = await WebAssembly.instantiate(readFileSync('public/wasm/rd.wasm'), {
  wasi_snapshot_preview1: {
    proc_exit() {
      throw Error('exit');
    },
  },
});
const native = (...args) =>
  Number(
    execFileSync('build/core-test', args.map(String), {
      encoding: 'utf8',
    }).trim(),
  );
// Gray-Scott runs in every mode, FitzHugh-Nagumo in the Q15 modes.
const cases = [
  [0, 0],
  [0, 1],
  [0, 2],
  [0, 3],
  [1, 1],
  [1, 3],
];
const init = (model, mode, seed) => {
  const n = e.rd_bytes(mode),
    p = e.malloc(n),
    s = e.rd_init_model(p, n, mode, model, seed, 0, 0);
  assert(s);
  assert.equal(e.rd_model(s), model);
  return [p, s];
};
for (const [model, mode] of cases)
  for (const count of [0, 1, 100]) {
    const [p, s] = init(model, mode, 42);
    e.rd_step(s, count);
    const hash = native(model, mode, count);
    assert.equal(e.rd_hash(s) >>> 0, hash);
    e.free(p);
    console.log(`model ${model}, mode ${mode}, steps ${count}: native/Wasm ${hash}`);
  }

// Clock masks built and installed in Wasm give the native field.
for (const [model, mode] of cases)
  for (const font of [0, 1]) {
    const args = [font, 13, 57, 2046, 8, 29, 1];
    const [p, s] = init(model, mode, 42);
    const m = e.malloc(e.cm_bytes(e.rd_width(s), e.rd_height(s)));
    assert.equal(e.cm_build(m, e.rd_width(s), e.rd_height(s), ...args), 0);
    assert.equal(e.rd_mask(s, m), 0);
    e.free(m);
    e.rd_step(s, 200);
    const hash = native(model, mode, 200, ...args);
    assert.equal(e.rd_hash(s) >>> 0, hash);
    e.free(p);
    console.log(
      `model ${model}, mode ${mode}, font ${font}, masked: native/Wasm ${hash}`,
    );
  }

// Interpolated rows of every grid match the native build.
for (const [model, mode] of cases) {
  const [p, s] = init(model, mode, 42);
  e.rd_step(s, 50);
  let hash = 2166136261;
  for (let y = 0; y < 228; y++) {
    const row = new Uint8Array(e.memory.buffer, e.rd_row(s, y, 0, 3), 800);
    for (const v of row) hash = Math.imul(hash ^ v, 16777619) >>> 0;
  }
  const expected = native('render', model, mode, 50);
  assert.equal(hash, expected);
  e.free(p);
  console.log(
    `model ${model}, mode ${mode}, interpolated rows: native/Wasm ${expected}`,
  );
}

// FitzHugh-Nagumo is rejected in the packed modes.
for (const mode of [0, 2]) {
  const n = e.rd_bytes(mode),
    p = e.malloc(n);
  assert.equal(e.rd_init_model(p, n, mode, 1, 42, 0, 0), 0);
  e.free(p);
}

// Repeated resets reuse the allocator and remain within fixed linear
// memory. The Q15 modes alternate between the models, with parameter
// vectors written into Wasm memory.
const vector = e.malloc(10 * 4);
const values = (list) => {
  new Int32Array(e.memory.buffer, vector, 10).set(list);
  return vector;
};
// fhn-hex of lib/presets.ts.
const hex = [1311, 32768, 573, 1434, 19661, -7209, 32768, -9585, 0];
const linear = e.memory.buffer.byteLength;
for (let i = 0; i < 300; i++) {
  const mode = i % 4,
    model = (mode === 1 || mode === 3) && i % 8 >= 4 ? 1 : 0,
    n = e.rd_bytes(mode),
    p = e.malloc(n);
  assert(p);
  const s =
    model === 1
      ? e.rd_init_model(p, n, mode, 1, i, values(hex), 9)
      : e.rd_init(p, n, mode, i);
  assert(s);
  assert.equal(e.rd_model(s), model);
  if (model === 1) {
    assert.equal(e.rd_set_params(s, values([0, 0, 0, 0, 0, 0, 0, 0, 0]), 9), 0);
    assert.equal(e.rd_set_params(s, values(hex), 5), -1);
    assert.equal(e.rd_params(s, 0, 0, 0, 0, 0), -1);
  } else {
    assert.equal(e.rd_params(s, 0, 0, 0, 0, 0), 0);
    assert.equal(e.rd_set_params(s, values([0, 0, 0, 0, 0]), 5), 0);
  }
  assert.equal(e.rd_step(s, 1), 0);
  const row = e.rd_row(s, 227, 2, 1);
  assert(row);
  const pixels = new Uint8Array(e.memory.buffer, row, 800);
  for (let x = 0; x < 200; x++) assert.equal(pixels[x * 4 + 3], 255);
  e.free(p);
  assert.equal(e.memory.buffer.byteLength, linear);
}
e.free(vector);
console.log('300 mode and model resets, output rows, fixed Wasm memory: OK');
