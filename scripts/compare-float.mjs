// Compare the released Wasm core with the original Float32 reference at the
// same grid resolution, initial field, and effective parameters (Q15, with
// da and db folded in the Q15 modes).
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { Simulation, presets } from '../lib/simulation.ts';

const wasm = readFileSync('public/wasm/rd.wasm');
const { instance } = await WebAssembly.instantiate(wasm, {
  wasi_snapshot_preview1: {
    proc_exit() {
      throw new Error('Wasm exit');
    },
  },
});
const api = instance.exports;
const seeds = [42, 1234];
const checkpoints = [0, 1, 10, 100, 1000];
const requestedBase = { da: 1, db: 0.5, dt: 1 };
const VALUE_ONE = 2 ** 24;
const q15 = (value) => Math.round(value * 32768);
const effective = (value) => q15(value) / 32768;

function metrics(reference, state, width, height) {
  const length = width * height;
  const species = [];
  for (let kind = 0; kind < 2; kind++) {
    const values = kind === 0 ? reference.a : reference.b;
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
        const fixed = api.rd_get(state, x, y, kind) / VALUE_ONE;
        assert(Number.isFinite(float) && Number.isFinite(fixed));
        const difference = Math.abs(float - fixed);
        absolute += difference;
        square += difference * difference;
        maximum = Math.max(maximum, difference);
        changedOnePercent += difference > 0.01;
        sumX += float;
        sumY += fixed;
        sumXX += float * float;
        sumYY += fixed * fixed;
        sumXY += float * fixed;
      }
    const covariance = sumXY - (sumX * sumY) / length;
    const denominator = Math.sqrt(
      (sumXX - (sumX * sumX) / length) * (sumYY - (sumY * sumY) / length),
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
  return { a: species[0], b: species[1] };
}

const rows = [];
for (const mode of [0, 1, 2, 3]) {
  const width = [200, 100, 100, 120][mode];
  const height = Math.floor((width * 228) / 200);
  const bytes = api.rd_bytes(mode);
  const allocation = api.malloc(bytes);
  assert(allocation, 'Wasm allocation failed');
  for (const preset of presets)
    for (const seed of seeds) {
      const state = api.rd_init(allocation, bytes, mode, seed);
      assert(state, 'Wasm initialization failed');
      const requested = {
        ...requestedBase,
        feed: preset.feed,
        kill: preset.kill,
      };
      // The Q15 modes fold da and db with the 1/20 of the Laplacian into
      // round(D / 20) / 20 (version 4), as the core computes with.
      const fold = (key, value) =>
        (mode === 1 || mode === 3) && (key === 'da' || key === 'db')
          ? (Math.round(q15(value) / 20) * 20) / 32768
          : effective(value);
      const parameters = Object.fromEntries(
        Object.entries(requested).map(([key, value]) => [
          key,
          fold(key, value),
        ]),
      );
      assert.equal(
        api.rd_params(
          state,
          ...['feed', 'kill', 'da', 'db', 'dt'].map((key) =>
            q15(requested[key]),
          ),
        ),
        0,
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
        const sample = { step, ...metrics(reference, state, width, height) };
        if (step === 0) {
          assert.equal(sample.a.mae, 0);
          assert.equal(sample.b.mae, 0);
        }
        samples.push(sample);
        previous = step;
      }
      rows.push({
        mode,
        preset: preset.name,
        seed,
        width,
        height,
        requestedParameters: requested,
        effectiveParameters: parameters,
        samples,
      });
      process.stdout.write(
        `mode ${mode} ${preset.name} seed ${seed}: ` +
          `B MAE ${samples.at(-1).b.mae.toFixed(6)}, ` +
          `r ${samples.at(-1).b.correlation?.toFixed(4) ?? 'n/a'}\n`,
      );
    }
  api.free(allocation);
}
const report = {
  source: 'public/wasm/rd.wasm versus lib/simulation.ts',
  coreVersion: 4,
  checkpoints,
  method:
    'Float32 fields start from the rd_get Q24 values, use the same grid and the effective parameters (Q15, with da and db folded with the 1/20 of the Laplacian in the Q15 modes), and run the existing Float32 Euler step. Each concentration is compared cell by cell. Mode 2 isolates storage precision at 100x114.',
  rows,
};
writeFileSync(
  'docs/float-precision.json',
  JSON.stringify(report, null, 2) + '\n',
);
