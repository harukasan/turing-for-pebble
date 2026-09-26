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
for (let mode = 0; mode < 4; mode++)
  for (const count of [0, 1, 100]) {
    const n = e.rd_bytes(mode),
      p = e.malloc(n),
      s = e.rd_init(p, n, mode, 42);
    assert(s);
    e.rd_step(s, count);
    const native = Number(
      execFileSync('build/core-test', [String(mode), String(count)], {
        encoding: 'utf8',
      }).trim(),
    );
    assert.equal(e.rd_hash(s) >>> 0, native);
    e.free(p);
    console.log(`mode ${mode}, steps ${count}: native/Wasm ${native}`);
  }

// Clock masks built and installed in Wasm give the native field.
for (const mode of [0, 1, 2, 3])
  for (const font of [0, 1]) {
    const args = [font, 13, 57, 2046, 8, 29, 1];
    const n = e.rd_bytes(mode),
      p = e.malloc(n),
      s = e.rd_init(p, n, mode, 42);
    const m = e.malloc(e.cm_bytes(e.rd_width(s), e.rd_height(s)));
    assert.equal(e.cm_build(m, e.rd_width(s), e.rd_height(s), ...args), 0);
    assert.equal(e.rd_mask(s, m), 0);
    e.free(m);
    e.rd_step(s, 200);
    const native = Number(
      execFileSync('build/core-test', [mode, 200, ...args].map(String), {
        encoding: 'utf8',
      }).trim(),
    );
    assert.equal(e.rd_hash(s) >>> 0, native);
    e.free(p);
    console.log(`mode ${mode}, font ${font}, masked: native/Wasm ${native}`);
  }

// Analog face masks built and installed in Wasm give the native field.
for (const mode of [0, 1, 2, 3])
  for (const font of [0, 1]) {
    const args = [font, 360, 600, 2046, 8, 29, 1];
    const n = e.rd_bytes(mode),
      p = e.malloc(n),
      s = e.rd_init(p, n, mode, 42);
    const m = e.malloc(e.cm_bytes(e.rd_width(s), e.rd_height(s)));
    assert.equal(
      e.cm_build_analog(m, e.rd_width(s), e.rd_height(s), ...args),
      0,
    );
    assert.equal(e.rd_mask(s, m), 0);
    e.free(m);
    e.rd_step(s, 200);
    const native = Number(
      execFileSync(
        'build/core-test',
        [mode, 200, 'analog', ...args].map(String),
        { encoding: 'utf8' },
      ).trim(),
    );
    assert.equal(e.rd_hash(s) >>> 0, native);
    e.free(p);
    console.log(`mode ${mode}, font ${font}, analog: native/Wasm ${native}`);
  }

// The sweep and hand angles agree with the native tests.
assert.equal(e.cm_hour_angle(11, 59), 1438);
assert.equal(e.cm_minute_angle(59), 1416);
assert.equal(e.cm_sweep_angle(1416, 0, 1000, 1000), 0);
assert.equal(e.cm_sweep_angle(10, 1430, 0, 1000), 10);
assert.equal(e.cm_sweep_angle(0, 0, 0, 0), -1);

// Interpolated rows of every grid match the native build.
for (let mode = 0; mode < 4; mode++) {
  const n = e.rd_bytes(mode),
    p = e.malloc(n),
    s = e.rd_init(p, n, mode, 42);
  e.rd_step(s, 50);
  let hash = 2166136261;
  for (let y = 0; y < 228; y++) {
    const row = new Uint8Array(e.memory.buffer, e.rd_row(s, y, 0, 3), 800);
    for (const v of row) hash = Math.imul(hash ^ v, 16777619) >>> 0;
  }
  const native = Number(
    execFileSync('build/core-test', ['render', String(mode), '50'], {
      encoding: 'utf8',
    }).trim(),
  );
  assert.equal(hash, native);
  e.free(p);
  console.log(`mode ${mode}, interpolated rows: native/Wasm ${native}`);
}

// Repeated mode resets reuse the allocator and remain within fixed linear memory.
const linear = e.memory.buffer.byteLength;
for (let i = 0; i < 300; i++) {
  const mode = i % 4,
    n = e.rd_bytes(mode),
    p = e.malloc(n);
  assert(p);
  const s = e.rd_init(p, n, mode, i);
  assert(s);
  assert.equal(e.rd_params(s, 0, 0, 0, 0, 0), 0);
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
    assert.equal(e.cm_build_analog(m, w, h, 0, 360, 600, 2046, 8, 29, 1), 0);
    assert.equal(e.rd_mask(s, m), 0);
    e.free(m);
  }
  e.free(p);
  assert.equal(e.memory.buffer.byteLength, linear);
}
console.log('300 mode resets, output rows, fixed Wasm memory: OK');
