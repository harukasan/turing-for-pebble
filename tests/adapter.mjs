import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import {
  loadCore,
  WasmSimulation,
  effective,
  buildAnalogMask,
  buildMask,
  maskBit,
  maskLevels,
} from "../lib/wasm-simulation.ts";
import { execFileSync } from "node:child_process";
import { FloatSimulation } from "../lib/float-simulation.ts";
import { FhnSimulation } from "../lib/fhn-simulation.ts";
import {
  RD_PARAM_MAX,
  defaultParametersFor,
  modelIndex,
  parameterOrder,
  parameterRanges,
  parameterVector,
  presetById,
  presetParameters,
  vectorValid,
} from "../lib/presets.ts";
import {
  analogPixels,
  hourAngle,
  minuteAngle,
  sweepAngle,
  TURN,
} from "../lib/clock-face.ts";
import {
  PALETTES,
  PALETTE_CUSTOM,
  PALETTE_COUNT,
  paletteColor,
} from "../lib/palettes.ts";
import {
  CLOCK_FONTS,
  clockPixels,
  fontIndex,
  loadFonts,
} from "../lib/clock-fonts.ts";
globalThis.fetch = async (url) =>
  new Response(
    readFileSync(
      (url instanceof Request ? url.url : url.toString()).endsWith(".json")
        ? "public/fonts/clock-fonts.json"
        : "public/wasm/rd.wasm"
    )
  );
const api = await loadCore();
await loadFonts();
const VALUE_ONE = 2 ** 24;
const pixels = { data: new Uint8ClampedArray(200 * 228 * 4) };
const params = {
  model: "gray-scott",
  feed: 0.029,
  kill: 0.057,
  da: 1,
  db: 0.5,
  dt: 1,
};
assert.equal(effective(params).feed, 950 / 32768);
for (let i = 0; i < 30; i++) {
  const s = new WasmSimulation(api, i);
  assert.equal(s.steps, 0);
  s.step(params, 10);
  assert.equal(s.steps, 10);
  s.seedAt(0, 0);
  s.render(pixels, "green", true);
  for (let p = 0; p < pixels.data.length; p += 4) {
    assert.equal(pixels.data[p + 3], 255);
    for (let c = 0; c < 3; c++) assert.equal(pixels.data[p + c] % 85, 0);
  }
  s.dispose();
  s.dispose();
}
// The Float32 reference seeds the same field as the core, value for value.
{
  const reference = new FloatSimulation(120, 42);
  const bytes = api.rd_bytes();
  const allocation = api.malloc(bytes);
  const state = api.rd_init(allocation, bytes, 42);
  assert(state);
  const same = () => {
    for (let y = 0; y < reference.height; y++)
      for (let x = 0; x < reference.width; x++) {
        const index = y * reference.width + x;
        assert.equal(
          reference.field.a[index],
          api.rd_get(state, x, y, 0) / VALUE_ONE
        );
        assert.equal(
          reference.field.b[index],
          api.rd_get(state, x, y, 1) / VALUE_ONE
        );
      }
  };
  assert.equal(reference.height, api.rd_height(state));
  same();
  reference.seedAt(0, 0);
  assert.equal(api.rd_seed(state, 0, 0, 6), 0);
  same();
  reference.step(params);
  assert.equal(reference.steps, 1);
  reference.render(pixels, "green", true);
  assert(
    pixels.data.every((value, i) =>
      i % 4 === 3 ? value === 255 : value % 85 === 0
    )
  );
  api.free(allocation);
}
console.log(
  "TypeScript adapters: Wasm loading, 30 resets, Float32 initial fields, stepping, seeding, RGB2 rows, disposal passed"
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
    for (const [halo, showDate] of [
      [0, true],
      [2, true],
      [3, true],
      [1, false],
    ])
      for (const date of times) {
        const height = (width * 228) / 200;
        const expected = new Uint8Array(((width + 7) >> 3) * height);
        clockPixels(
          date,
          font,
          (px, py) => {
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
          },
          showDate
        );
        const mask = buildMask(
          api,
          width,
          height,
          fontIndex(font),
          date,
          halo,
          showDate
        );
        assert.deepEqual(
          mask,
          expected,
          `${font} ${width} ${halo} ${showDate} ${date.toISOString()}`
        );
      }

// The analog mask of a grid is the face bitmap drawn by the preview,
// widened by the square halo and mapped to cells, and the JS angles agree
// with the core.
for (let t = 0; t < 720; t++) {
  const [h, m] = [Math.floor(t / 60), t % 60];
  assert.equal(hourAngle(h, m), api.cm_hour_angle(h, m));
  assert.equal(hourAngle(h + 12, m), api.cm_hour_angle(h + 12, m));
  assert.equal(minuteAngle(m), api.cm_minute_angle(m));
}
assert.equal(sweepAngle(api, TURN - 24, 0, 1000, 1000), 0);
assert.throws(() => sweepAngle(api, TURN, 0, 0, 1000));
const faces = [
  [hourAngle(13, 57), minuteAngle(57)],
  [0, 0],
  [1438, 1416],
];
for (const font of CLOCK_FONTS)
  for (const width of [100, 200])
    for (const halo of [0, 2, 3])
      for (const [hour, minute] of faces) {
        const date = times[1];
        const height = (width * 228) / 200;
        const bitmap = analogPixels(api, fontIndex(font), hour, minute, date);
        const expected = new Uint8Array(((width + 7) >> 3) * height);
        for (let py = 0; py < 228; py++)
          for (let px = 0; px < 200; px++) {
            if (!maskBit(bitmap, 200, px, py)) continue;
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
          }
        const mask = buildAnalogMask(
          api,
          width,
          height,
          fontIndex(font),
          hour,
          minute,
          date,
          halo
        );
        assert.deepEqual(mask, expected, `${font} ${width} ${halo} ${hour}`);
      }

// Masked cells hold the equilibrium in both engines, through a step, for
// the digits and for the analog face.
const clockDate = times[1];
for (const [engine, face] of [
  ["wasm", "digital"],
  ["float", "digital"],
  ["wasm", "analog"],
  ["float", "analog"],
]) {
  const s =
    engine === "float"
      ? new FloatSimulation(120, 42)
      : new WasmSimulation(api, 42);
  if (engine !== "float") {
    assert.equal(s.components.length, 6);
    assert.equal(
      s.components.reduce((sum, bytes) => sum + bytes, 0),
      s.bytes
    );
  }
  const mask =
    face === "analog"
      ? buildAnalogMask(api, s.width, s.height, 1, 360, 600, clockDate, 1)
      : buildMask(api, s.width, s.height, 1, clockDate, 1);
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
    5
  );
  s.dispose();
}
// Interpolated renders of the same field agree between the Wasm core and
// the Float32 renderer, up to Q15 rounding at color boundaries.
{
  const wasm = new WasmSimulation(api, 42);
  assert.equal(wasm.width, 120);
  assert.equal(wasm.height, 136);
  wasm.step(params, 300);
  const float = new FloatSimulation(120, 42);
  for (let y = 0; y < 136; y++)
    for (let x = 0; x < 120; x++) {
      float.field.a[y * 120 + x] = wasm.get(x, y, 0);
      float.field.b[y * 120 + x] = wasm.get(x, y, 1);
    }
  const a = { data: new Uint8ClampedArray(200 * 228 * 4) };
  const b = { data: new Uint8ClampedArray(200 * 228 * 4) };
  for (const { id } of PALETTES) {
    wasm.render(a, id, true, true);
    float.render(b, id, true, true);
    let differing = 0;
    for (let i = 0; i < a.data.length; i += 4)
      differing +=
        a.data[i] !== b.data[i] ||
        a.data[i + 1] !== b.data[i + 1] ||
        a.data[i + 2] !== b.data[i + 2];
    assert(differing / (200 * 228) < 0.01, `${id}: ${differing} pixels differ`);
  }
  wasm.dispose();
}

console.log(
  "Clock masks: core glyphs equal the JSON glyphs for both fonts, analog masks equal the splatted face bitmap, mask levels agree and B stays 0 in masked cells for both faces"
);

// The TypeScript ranges of every model's vector agree with rd_check_params
// at each entry's bounds, and the unused entries must be 0.
{
  const scratch = api.malloc(RD_PARAM_MAX * 4);
  const core = (model, vector) => {
    new Int32Array(api.memory.buffer, scratch, RD_PARAM_MAX).set(vector);
    return (
      api.rd_check_params(
        modelIndex[model],
        scratch,
        parameterOrder[model].length
      ) === 0 && vector.slice(parameterOrder[model].length).every((v) => !v)
    );
  };
  for (const model of ["gray-scott", "fhn"]) {
    const base = parameterVector(defaultParametersFor(model));
    assert(vectorValid(model, base) && core(model, base));
    parameterRanges(model).forEach(([low, high], i) => {
      for (const value of [low - 1, low, high, high + 1]) {
        const vector = [...base];
        vector[i] = value;
        assert.equal(
          vectorValid(model, vector),
          core(model, vector),
          `${model} ${i} ${value}`
        );
      }
    });
    const extra = [...base];
    extra[RD_PARAM_MAX - 1] = 1;
    assert(!vectorValid(model, extra) && !core(model, extra));
  }
  api.free(scratch);
}

// FitzHugh-Nagumo through the TypeScript adapters.
const preset = (id) => presetParameters(presetById(id));
const stripes = preset("fhn-stripes"),
  hex = preset("fhn-hex"),
  spiral = preset("fhn-spiral");

// The defaults of rd_init_model are the maze and fhn-stripes presets.
for (const [model, id] of [
  [0, "maze"],
  [1, "fhn-stripes"],
]) {
  const params = preset(id);
  const expected = parameterVector(params).slice(
    0,
    parameterOrder[params.model].length
  );
  const defaults = execFileSync(
    "build/core-test",
    ["defaults", String(model)],
    {
      encoding: "utf8",
    }
  )
    .trim()
    .split(" ")
    .map(Number);
  assert.deepEqual(defaults, expected, id);
  const bytes = api.rd_bytes();
  const a = api.malloc(bytes),
    b = api.malloc(bytes),
    vector = api.malloc(40);
  new Int32Array(api.memory.buffer, vector, 10).set(parameterVector(params));
  const first = api.rd_init_model(a, bytes, model, 42, 0, 0);
  const second = api.rd_init_model(
    b,
    bytes,
    model,
    42,
    vector,
    expected.length
  );
  api.rd_step(first, 20);
  api.rd_step(second, 20);
  assert.equal(api.rd_hash(first), api.rd_hash(second));
  [a, b, vector].forEach((pointer) => api.free(pointer));
}

// Effective parameters: diffusion folded by 20, k and rest in Q13.
{
  const q = effective(hex);
  assert.equal(q.k, (Math.floor((-7209 + 2) / 4) * 4) / 32768);
  assert.equal(q.rest, (Math.floor((-9584 + 2) / 4) * 4) / 32768);
  assert.equal(q.du, (Math.round(1311 / 20) * 20) / 32768);
  assert.equal(q.init, 0);
  assert.equal(q.model, "fhn");
}

// A simulation takes only parameters of its own model.
{
  const s = new WasmSimulation(api, 42, stripes);
  assert.equal(s.model, "fhn");
  assert.throws(() => s.step(params));
  s.dispose();
  const f = new FloatSimulation(120, 42, stripes);
  assert.throws(() => f.step(params));
}

// The initial fields of both engines agree: the same seeded cells, and
// values within the Q13 rounding of rest and v = rest / av.
const tolerance = 3 / 32768;
for (const p of [stripes, hex, spiral]) {
  const wasm = new WasmSimulation(api, 42, p);
  const float = new FloatSimulation(120, 42, p);
  const compare = () => {
    for (let y = 0; y < wasm.height; y++)
      for (let x = 0; x < 120; x++)
        for (const species of [0, 1]) {
          const a = wasm.get(x, y, species),
            b = float.get(x, y, species);
          assert(Math.abs(a - b) <= tolerance, `${x} ${y} ${a} ${b}`);
        }
  };
  compare();
  wasm.seedAt(10, 10);
  float.seedAt(10, 10);
  compare();
  wasm.dispose();
}

// Masked cells hold u at rest in both engines, through a step.
for (const engine of ["wasm", "float"]) {
  const s =
    engine === "float"
      ? new FloatSimulation(120, 42, hex)
      : new WasmSimulation(api, 42, hex);
  const mask = buildMask(api, s.width, s.height, 0, clockDate, 1);
  s.setMask(mask);
  s.seedAt(Math.floor(s.width / 2), Math.floor(s.height / 2), 20);
  s.step(hex, 1);
  const rest = (hex.rest + 2) / 4;
  let count = 0;
  for (let y = 0; y < s.height; y++)
    for (let x = 0; x < s.width; x++) {
      if (s.maskLevel(x, y)) continue;
      count++;
      assert(Math.abs(s.get(x, y, 1) - rest) <= tolerance);
    }
  assert(count > 0);
  s.dispose();
}

// Interpolated renders of the same FitzHugh-Nagumo field agree between
// the Wasm core and the Float32 renderer, up to Q15 rounding at color
// boundaries.
{
  const wasm = new WasmSimulation(api, 42, spiral);
  wasm.step(spiral, 300);
  const float = new FloatSimulation(120, 42, spiral);
  assert(float.field instanceof FhnSimulation);
  for (let y = 0; y < 136; y++)
    for (let x = 0; x < 120; x++) {
      float.field.v[y * 120 + x] = 4 * wasm.get(x, y, 0) - 2;
      float.field.u[y * 120 + x] = 4 * wasm.get(x, y, 1) - 2;
    }
  const a = { data: new Uint8ClampedArray(200 * 228 * 4) };
  const b = { data: new Uint8ClampedArray(200 * 228 * 4) };
  wasm.render(a, "green", true, true);
  float.render(b, "green", true, true);
  let differing = 0,
    lit = 0;
  for (let i = 0; i < a.data.length; i += 4) {
    differing += a.data[i] !== b.data[i] || a.data[i + 1] !== b.data[i + 1];
    lit += a.data[i + 1] > 85;
  }
  assert(lit > 1000, `${lit} lit pixels`);
  assert(differing / (200 * 228) < 0.01, `${differing} pixels differ`);
  wasm.dispose();
}
console.log(
  "FitzHugh-Nagumo: default vectors, effective parameters, initial fields, seeds, masks, and interpolated renders agree"
);

// Every pixel of the core's nearest rendering is the lib/palettes.ts color
// of the covering cell's B, for every palette, quantized or not, and the
// custom palette takes the stops of setPalette.
{
  const sim = new WasmSimulation(api, 42);
  sim.step(params, 300);
  const out = { data: new Uint8ClampedArray(200 * 228 * 4) };
  const check = (palette, quantize, custom) => {
    sim.render(out, palette, quantize, false);
    for (let y = 0; y < 228; y++)
      for (let x = 0; x < 200; x++) {
        const value =
          sim.get(Math.floor((x * 120) / 200), Math.floor((y * 136) / 228), 1) *
          VALUE_ONE;
        const expected = paletteColor(palette, value, quantize, custom);
        const i = (y * 200 + x) * 4;
        assert.deepEqual(
          [out.data[i], out.data[i + 1], out.data[i + 2], out.data[i + 3]],
          [...expected, 255],
          `palette ${palette} quantize ${quantize} at ${x},${y}`
        );
      }
  };
  for (let palette = 0; palette < PALETTE_CUSTOM; palette++)
    for (const quantize of [false, true]) check(palette, quantize);
  check(PALETTE_CUSTOM, true, PALETTES[0].stops);
  const custom = [
    [255, 0, 0],
    [0, 255, 0],
    [0, 0, 255],
    [255, 255, 255],
    [12, 34, 56],
  ];
  sim.setPalette(custom);
  for (const quantize of [false, true]) check(PALETTE_CUSTOM, quantize, custom);
  assert.throws(() => sim.setPalette([[1, 2, 3]]));
  assert.throws(() => sim.setPalette(Array(9).fill([1, 2, 3])));
  check(PALETTE_CUSTOM, true, custom);
  assert.throws(() => sim.render(out, PALETTE_COUNT, true));
  assert.throws(() => sim.render(out, "unknown", true));
  sim.dispose();
}
console.log(
  `Palettes: ${PALETTE_COUNT - 1} built-in palettes and the custom stops equal lib/palettes.ts on every pixel`
);
