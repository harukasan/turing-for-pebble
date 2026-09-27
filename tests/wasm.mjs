import { readFileSync } from "node:fs";
import { execFileSync } from "node:child_process";
import assert from "node:assert/strict";
const {
  instance: { exports: e },
} = await WebAssembly.instantiate(readFileSync("public/wasm/rd.wasm"), {
  wasi_snapshot_preview1: {
    proc_exit() {
      throw Error("exit");
    },
  },
});
const native = (...args) =>
  Number(
    execFileSync("build/core-test", args.map(String), {
      encoding: "utf8",
    }).trim()
  );
const models = [0, 1];
const init = (model, seed) => {
  const n = e.rd_bytes(),
    p = e.malloc(n),
    s = e.rd_init_model(p, n, model, seed, 0, 0);
  assert(s);
  assert.equal(e.rd_model(s), model);
  return [p, s];
};
for (const model of models)
  for (const count of [0, 1, 100]) {
    const [p, s] = init(model, 42);
    e.rd_step(s, count);
    const hash = native(model, count);
    assert.equal(e.rd_hash(s) >>> 0, hash);
    e.free(p);
    console.log(`model ${model}, steps ${count}: native/Wasm ${hash}`);
  }

// Clock masks built and installed in Wasm give the native field.
for (const model of models)
  for (const font of [0, 1]) {
    const args = [font, 13, 57, 2046, 8, 29, 1];
    const [p, s] = init(model, 42);
    const m = e.malloc(e.cm_bytes(e.rd_width(s), e.rd_height(s)));
    assert.equal(e.cm_build(m, e.rd_width(s), e.rd_height(s), ...args, 1), 0);
    assert.equal(e.rd_mask(s, m), 0);
    e.free(m);
    e.rd_step(s, 200);
    const hash = native(model, 200, ...args);
    assert.equal(e.rd_hash(s) >>> 0, hash);
    e.free(p);
    console.log(`model ${model}, font ${font}, masked: native/Wasm ${hash}`);
  }

// Analog face masks built and installed in Wasm give the native field.
for (const font of [0, 1]) {
  const args = [font, 360, 600, 2046, 8, 29, 1];
  const [p, s] = init(0, 42);
  const m = e.malloc(e.cm_bytes(e.rd_width(s), e.rd_height(s)));
  assert.equal(
    e.cm_build_analog(m, e.rd_width(s), e.rd_height(s), ...args, 1),
    0
  );
  assert.equal(e.rd_mask(s, m), 0);
  e.free(m);
  e.rd_step(s, 200);
  const hash = native(0, 200, "analog", ...args);
  assert.equal(e.rd_hash(s) >>> 0, hash);
  e.free(p);
  console.log(`font ${font}, analog: native/Wasm ${hash}`);
}

// The sweep and hand angles agree with the native tests.
assert.equal(e.cm_hour_angle(11, 59), 1438);
assert.equal(e.cm_minute_angle(59), 1416);
assert.equal(e.cm_sweep_angle(1416, 0, 1000, 1000), 0);
assert.equal(e.cm_sweep_angle(10, 1430, 0, 1000), 10);
assert.equal(e.cm_sweep_angle(0, 0, 0, 0), -1);

// Interpolated rows match the native build.
for (const model of models) {
  const [p, s] = init(model, 42);
  e.rd_step(s, 50);
  let hash = 2166136261;
  for (let y = 0; y < 228; y++) {
    const row = new Uint8Array(e.memory.buffer, e.rd_row(s, y, 0, 3), 800);
    for (const v of row) hash = Math.imul(hash ^ v, 16777619) >>> 0;
  }
  const expected = native("render", model, 50);
  assert.equal(hash, expected);
  e.free(p);
  console.log(`model ${model}, interpolated rows: native/Wasm ${expected}`);
}

// The spiral preset starts from the cut wave (init 1) instead of the disks,
// so its field is checked against the native build with the same vector.
{
  const spiral = [6554, 0, 8192, 410, 32768, -9830, 32768, -21936, 1];
  const buffer = e.malloc(spiral.length * 4);
  new Int32Array(e.memory.buffer, buffer, spiral.length).set(spiral);
  const vector = "p=" + spiral.join(",");
  for (const masked of [false, true]) {
    const n = e.rd_bytes(),
      p = e.malloc(n),
      s = e.rd_init_model(p, n, 1, 42, buffer, spiral.length);
    assert(s);
    const args = masked ? [0, 13, 57, 2046, 8, 29, 1] : [];
    if (masked) {
      const m = e.malloc(e.cm_bytes(e.rd_width(s), e.rd_height(s)));
      assert.equal(e.cm_build(m, e.rd_width(s), e.rd_height(s), ...args, 1), 0);
      assert.equal(e.rd_mask(s, m), 0);
      e.free(m);
    }
    e.rd_step(s, 200);
    const hash = native(1, 200, ...args, vector);
    assert.equal(e.rd_hash(s) >>> 0, hash);
    e.free(p);
    console.log(
      `model 1, fhn-spiral${masked ? ", masked" : ""}: native/Wasm ${hash}`
    );
  }
  e.free(buffer);
}

// Repeated resets reuse the allocator and remain within fixed linear
// memory. The fields alternate between the models, with parameter vectors
// written into Wasm memory.
const vector = e.malloc(10 * 4);
const values = (list) => {
  new Int32Array(e.memory.buffer, vector, 10).set(list);
  return vector;
};
// fhn-hex of lib/presets.ts.
const hex = [1311, 32768, 860, 2150, 19661, -7209, 32768, -9584, 0];
const linear = e.memory.buffer.byteLength;
for (let i = 0; i < 300; i++) {
  const model = i % 8 >= 4 ? 1 : 0,
    n = e.rd_bytes(),
    p = e.malloc(n);
  assert(p);
  const s =
    model === 1
      ? e.rd_init_model(p, n, 1, i, values(hex), 9)
      : e.rd_init(p, n, i);
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
  if (i === 150) {
    // An analog mask, built and installed as the Web does, fits too.
    const w = e.rd_width(s),
      h = e.rd_height(s),
      m = e.malloc(e.cm_bytes(w, h));
    assert(m);
    assert.equal(e.cm_build_analog(m, w, h, 0, 360, 600, 2046, 8, 29, 1, 1), 0);
    assert.equal(e.rd_mask(s, m), 0);
    e.free(m);
  }
  e.free(p);
  assert.equal(e.memory.buffer.byteLength, linear);
}
e.free(vector);
console.log("300 model resets, output rows, fixed Wasm memory: OK");
