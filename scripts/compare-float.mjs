// Compare the released Wasm core with the Float32 references at the same
// grid resolution, initial field, and effective parameters: Gray-Scott
// against lib/simulation.ts (Q15, with da and db folded with the 1/20 of
// the Laplacian) and FitzHugh-Nagumo against lib/fhn-simulation.ts (du and
// dv folded, k and rest in Q13).
import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { Simulation } from "../lib/simulation.ts";
import { FhnSimulation } from "../lib/fhn-simulation.ts";
import {
  parameterOrder,
  parameterVector,
  presetById,
  presetParameters,
  presets,
} from "../lib/presets.ts";
import { effective as coreEffective } from "../lib/wasm-simulation.ts";

const wasm = readFileSync("public/wasm/rd.wasm");
const { instance } = await WebAssembly.instantiate(wasm, {
  wasi_snapshot_preview1: {
    proc_exit() {
      throw new Error("Wasm exit");
    },
  },
});
const api = instance.exports;
const GRAY_SCOTT_PRESETS = presets.filter(
  (preset) => preset.model === "gray-scott"
);
const FHN_PRESETS = ["fhn-stripes", "fhn-hex", "fhn-spiral"];
const seeds = [42, 1234];
const checkpoints = [0, 1, 10, 100, 1000];
const requestedBase = { da: 1, db: 0.5, dt: 1 };
const VALUE_ONE = 2 ** 24;
const q15 = (value) => Math.round(value * 32768);
const effective = (value) => q15(value) / 32768;

// Cell-by-cell differences of both species between the Float32 fields
// (species 0 and 1) and the core values given by fixedValue, with the
// fraction of cells differing by more than 1% of the value range.
function metrics(fields, fixedValue, range, width, height) {
  const length = width * height;
  const species = [];
  for (let kind = 0; kind < 2; kind++) {
    const values = fields[kind];
    let absolute = 0,
      square = 0,
      maximum = 0,
      changedOnePercent = 0;
    let sumX = 0,
      sumY = 0,
      sumXX = 0,
      sumYY = 0,
      sumXY = 0;
    for (let y = 0; y < height; y++)
      for (let x = 0; x < width; x++) {
        const index = y * width + x;
        const float = values[index];
        const fixed = fixedValue(x, y, kind);
        assert(Number.isFinite(float) && Number.isFinite(fixed));
        const difference = Math.abs(float - fixed);
        absolute += difference;
        square += difference * difference;
        maximum = Math.max(maximum, difference);
        changedOnePercent += difference > 0.01 * range;
        sumX += float;
        sumY += fixed;
        sumXX += float * float;
        sumYY += fixed * fixed;
        sumXY += float * fixed;
      }
    const covariance = sumXY - (sumX * sumY) / length;
    const denominator = Math.sqrt(
      (sumXX - (sumX * sumX) / length) * (sumYY - (sumY * sumY) / length)
    );
    species.push({
      mae: absolute / length,
      rmse: Math.sqrt(square / length),
      maxAbsolute: maximum,
      fractionAboveOnePercent: changedOnePercent / length,
      correlation: denominator > 0 ? covariance / denominator : null,
      floatMean: sumX / length,
      fixedMean: sumY / length,
    });
  }
  return species;
}

const rows = [];
const width = 120;
const height = Math.floor((width * 228) / 200);
const bytes = api.rd_bytes();
{
  const allocation = api.malloc(bytes);
  assert(allocation, "Wasm allocation failed");
  for (const preset of GRAY_SCOTT_PRESETS)
    for (const seed of seeds) {
      const state = api.rd_init(allocation, bytes, seed);
      assert(state, "Wasm initialization failed");
      const requested = {
        ...requestedBase,
        feed: preset.feed,
        kill: preset.kill,
      };
      // The core folds da and db with the 1/20 of the Laplacian into
      // round(D / 20) / 20 (version 4), and computes with those.
      const fold = (key, value) =>
        key === "da" || key === "db"
          ? (Math.round(q15(value) / 20) * 20) / 32768
          : effective(value);
      const parameters = Object.fromEntries(
        Object.entries(requested).map(([key, value]) => [key, fold(key, value)])
      );
      assert.equal(
        api.rd_params(
          state,
          ...["feed", "kill", "da", "db", "dt"].map((key) =>
            q15(requested[key])
          )
        ),
        0
      );

      const reference = new Simulation(width, height, seed);
      // The legacy constructor scatters seeds in grid coordinates. Replacing
      // those fields removes that unrelated initialization difference.
      for (let y = 0; y < height; y++)
        for (let x = 0; x < width; x++) {
          const index = y * width + x;
          reference.a[index] = api.rd_get(state, x, y, 0) / VALUE_ONE;
          reference.b[index] = api.rd_get(state, x, y, 1) / VALUE_ONE;
        }
      let previous = 0;
      const samples = [];
      for (const step of checkpoints) {
        for (let done = previous; done < step; done++) {
          reference.step(parameters);
          assert.equal(api.rd_step(state, 1), 0);
        }
        const [a, b] = metrics(
          [reference.a, reference.b],
          (x, y, kind) => api.rd_get(state, x, y, kind) / VALUE_ONE,
          1,
          width,
          height
        );
        const sample = { step, a, b };
        if (step === 0) {
          assert.equal(sample.a.mae, 0);
          assert.equal(sample.b.mae, 0);
        }
        samples.push(sample);
        previous = step;
      }
      rows.push({
        model: "gray-scott",
        id: preset.id,
        preset: preset.name,
        seed,
        width,
        height,
        requestedParameters: requested,
        effectiveParameters: parameters,
        samples,
      });
      process.stdout.write(
        `${preset.name} seed ${seed}: ` +
          `B MAE ${samples.at(-1).b.mae.toFixed(6)}, ` +
          `r ${samples.at(-1).b.correlation?.toFixed(4) ?? "n/a"}\n`
      );
    }
  api.free(allocation);
}
// FitzHugh-Nagumo, compared on x = 4 s - 2 of the stored
// fraction s: v is species 0 and u species 1. The spiral keeps moving, so
// its error grows with every displaced front.
const vector = api.malloc(10 * 4);
assert(vector, "Wasm allocation failed");
{
  const allocation = api.malloc(bytes);
  assert(allocation, "Wasm allocation failed");
  for (const id of FHN_PRESETS)
    for (const seed of seeds) {
      const preset = presetById(id);
      const requested = presetParameters(preset);
      new Int32Array(api.memory.buffer, vector, 10).set(
        parameterVector(requested)
      );
      const state = api.rd_init_model(
        allocation,
        bytes,
        1,
        seed,
        vector,
        parameterOrder.fhn.length
      );
      assert(state, "Wasm initialization failed");
      const parameters = coreEffective(requested);
      const x = (gx, gy, kind) =>
        (4 * api.rd_get(state, gx, gy, kind)) / VALUE_ONE - 2;
      const reference = new FhnSimulation(width, height);
      for (let y = 0; y < height; y++)
        for (let gx = 0; gx < width; gx++) {
          reference.v[y * width + gx] = x(gx, y, 0);
          reference.u[y * width + gx] = x(gx, y, 1);
        }
      let previous = 0;
      const samples = [];
      for (const step of checkpoints) {
        for (let done = previous; done < step; done++) {
          reference.step(parameters);
          assert.equal(api.rd_step(state, 1), 0);
        }
        const [v, u] = metrics([reference.v, reference.u], x, 4, width, height);
        const sample = { step, v, u };
        if (step === 0) {
          assert.equal(sample.v.mae, 0);
          assert.equal(sample.u.mae, 0);
        }
        samples.push(sample);
        previous = step;
      }
      rows.push({
        model: "fhn",
        id,
        preset: preset.name,
        seed,
        width,
        height,
        requestedParameters: requested,
        effectiveParameters: parameters,
        samples,
      });
      process.stdout.write(
        `${id} seed ${seed}: ` +
          `u MAE ${samples.at(-1).u.mae.toFixed(6)}, ` +
          `r ${samples.at(-1).u.correlation?.toFixed(4) ?? "n/a"}\n`
      );
    }
  api.free(allocation);
}
api.free(vector);
const report = {
  source:
    "public/wasm/rd.wasm versus lib/simulation.ts (Gray-Scott) and lib/fhn-simulation.ts (FitzHugh-Nagumo)",
  coreVersion: 5,
  checkpoints,
  method:
    "Float32 fields start from the rd_get Q24 values, use the same 120x136 grid and the effective parameters (Q15, with da and db folded with the 1/20 of the Laplacian), and run the existing Float32 Euler step. Each concentration is compared cell by cell. FitzHugh-Nagumo rows start the lib/fhn-simulation.ts fields from x = 4 s - 2 of the rd_get values, use du and dv folded the same way and k and rest rounded to Q13, and compare v (species 0) and u (species 1) in x units, counting cells that differ by more than 0.04, 1% of the range of x.",
  rows,
};
writeFileSync(
  "docs/float-precision.json",
  JSON.stringify(report, null, 2) + "\n"
);
