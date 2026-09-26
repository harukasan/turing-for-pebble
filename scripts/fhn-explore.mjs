// Explore FitzHugh-Nagumo parameters on the Float32 reference
// (lib/float-simulation.ts), optionally under the LECO clock mask of the
// core, and print one JSON line per checkpoint:
//
//   fill       fraction of the open blocks of about a twelfth of the width
//              that hold a cell with |u - rest| > 0.25
//   minU, maxU extremes of u over the open cells
//   meanU      mean u of the open cells
//   feature    mean length in cells of the lit runs along the rows, lit
//              meaning u above rest + (maxU - rest) / 2
//   change     mean |u' - u| of one step at the checkpoint (a pattern that
//              keeps moving, such as a spiral, stays above zero)
//   checker    mean |u - mean of the four axial neighbors| (about 2 for a
//              chessboard, well below 0.5 for a smooth pattern)
//   rampBright fraction of the cells within 1 or 2 cells of the digits
//              with u > 0.5 (a bright band along the digits raises it)
//   openBright fraction of the open cells with u > 0.5
//   litParts   connected regions (4-neighbor, periodic) of open cells with
//   darkParts  u above and at most the lit threshold of feature: a
//              labyrinth has few of both, spots many lit parts, holes many
//              dark parts
//
// "Open" cells are those at mask level 5, all cells without a mask. Each
// checkpoint also writes build/fhn/<label>-<step>.ppm, the interpolated
// lime render with the clock digits drawn in white.
//
//   node --experimental-transform-types scripts/fhn-explore.mjs \
//     [--preset fhn-stripes] [--du 0.1 --k=-0.1 ...] [--width 120] \
//     [--seed 42] [--steps 500,1500,3000] [--no-mask] [--pull 0.125] \
//     [--label name]
//
// --pull replaces the pull toward rest at the digits (RD_MASK_PULL).
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { parseArgs } from "node:util";
import { presetById, presetParameters } from "../lib/presets.ts";
import { FloatSimulation } from "../lib/float-simulation.ts";
import { buildMask, loadCore } from "../lib/wasm-simulation.ts";
import { clockPixels, loadFonts } from "../lib/clock-fonts.ts";

const keys = ["du", "dv", "ru", "rv", "av", "k", "dt", "rest", "init"];
const { values: args } = parseArgs({
  options: {
    preset: { type: "string", default: "fhn-stripes" },
    width: { type: "string", default: "120" },
    seed: { type: "string", default: "42" },
    steps: { type: "string", default: "500,1500,3000" },
    "no-mask": { type: "boolean", default: false },
    label: { type: "string" },
    pull: { type: "string" },
    ...Object.fromEntries(keys.map((key) => [key, { type: "string" }])),
  },
});

globalThis.fetch = async (url) =>
  new Response(
    readFileSync(
      String(url).endsWith(".json")
        ? "public/fonts/clock-fonts.json"
        : "public/wasm/rd.wasm"
    )
  );

const preset = presetById(args.preset);
if (!preset || preset.model !== "fhn")
  throw new Error(`not a FitzHugh-Nagumo preset: ${args.preset}`);
const params = presetParameters(preset);
for (const key of keys)
  if (args[key] !== undefined) params[key] = Number(args[key]);
const width = Number(args.width);
const sim = new FloatSimulation(width, Number(args.seed), params);
const { height } = sim;
const date = new Date(2046, 7, 29, 13, 57);
const mask = !args["no-mask"];
if (mask) {
  if (args.pull !== undefined) sim.maskPull = Number(args.pull);
  const api = await loadCore();
  sim.setMask(buildMask(api, width, height, 0, date, 1));
}
await loadFonts();
const digits = new Uint8Array(200 * 228);
clockPixels(date, "leco", (x, y) => (digits[y * 200 + x] = 1));

const label =
  args.label ??
  `${args.preset}-${width}${keys
    .filter((key) => args[key] !== undefined)
    .map((key) => `-${key}${args[key]}`)
    .join("")}`;
mkdirSync("build/fhn", { recursive: true });
const pixels = { data: new Uint8ClampedArray(200 * 228 * 4) };

function metrics(before) {
  const u = sim.field.u;
  const block = Math.max(4, Math.floor(width / 12));
  const blocksX = Math.ceil(width / block);
  const seen = new Uint8Array(blocksX * Math.ceil(height / block));
  let open = 0,
    sum = 0,
    minU = Infinity,
    maxU = -Infinity,
    bright = 0,
    ramp = 0,
    rampBright = 0,
    checker = 0,
    change = 0;
  for (let y = 0; y < height; y++)
    for (let x = 0; x < width; x++) {
      const i = y * width + x;
      const level = sim.maskLevel(x, y);
      const value = u[i];
      if (level === 1 || level === 2) {
        ramp++;
        rampBright += value > 0.5;
      }
      if (level < 5) continue;
      open++;
      sum += value;
      minU = Math.min(minU, value);
      maxU = Math.max(maxU, value);
      bright += value > 0.5;
      change += Math.abs(value - before[i]);
      const neighbors =
        (u[y * width + ((x + 1) % width)] +
          u[y * width + ((x + width - 1) % width)] +
          u[((y + 1) % height) * width + x] +
          u[((y + height - 1) % height) * width + x]) /
        4;
      checker += Math.abs(value - neighbors);
      const b = Math.floor(y / block) * blocksX + Math.floor(x / block);
      seen[b] = Math.max(seen[b], Math.abs(value - params.rest) > 0.25 ? 2 : 1);
    }
  const lit = params.rest + (maxU - params.rest) / 2;
  let runs = 0,
    runCells = 0;
  for (let y = 0; y < height; y++) {
    let run = 0;
    for (let x = 0; x < width; x++) {
      if (sim.maskLevel(x, y) === 5 && u[y * width + x] > lit) {
        run++;
        continue;
      }
      if (run) {
        runs++;
        runCells += run;
      }
      run = 0;
    }
  }
  const parts = (inside) => {
    const done = new Uint8Array(width * height);
    let count = 0;
    for (let start = 0; start < done.length; start++) {
      if (done[start] || !inside(start)) continue;
      count++;
      const stack = [start];
      done[start] = 1;
      while (stack.length) {
        const i = stack.pop();
        const x = i % width,
          y = (i - x) / width;
        for (const j of [
          y * width + ((x + 1) % width),
          y * width + ((x + width - 1) % width),
          ((y + 1) % height) * width + x,
          ((y + height - 1) % height) * width + x,
        ])
          if (!done[j] && inside(j)) {
            done[j] = 1;
            stack.push(j);
          }
      }
    }
    return count;
  };
  const openCell = (i) => sim.maskLevel(i % width, Math.floor(i / width)) === 5;
  const blocks = seen.filter((value) => value > 0).length;
  return {
    fill: seen.filter((value) => value === 2).length / blocks,
    minU,
    maxU,
    meanU: sum / open,
    feature: runs ? runCells / runs : 0,
    change: change / open,
    checker: checker / open,
    rampBright: ramp ? rampBright / ramp : 0,
    openBright: bright / open,
    litParts: parts((i) => openCell(i) && u[i] > lit),
    darkParts: parts((i) => openCell(i) && u[i] <= lit),
  };
}

for (const checkpoint of args.steps.split(",").map(Number)) {
  while (sim.steps < checkpoint - 1) sim.step(params);
  const before = sim.field.u.slice();
  sim.step(params);
  const row = { label, step: sim.steps, ...metrics(before) };
  for (const key of Object.keys(row))
    if (typeof row[key] === "number" && !Number.isInteger(row[key]))
      row[key] = Number(row[key].toFixed(4));
  process.stdout.write(JSON.stringify(row) + "\n");
  sim.render(pixels, "green", true, true);
  const bytes = Buffer.alloc(200 * 228 * 3);
  for (let p = 0; p < 200 * 228; p++)
    for (let c = 0; c < 3; c++)
      bytes[p * 3 + c] = mask && digits[p] ? 255 : pixels.data[p * 4 + c];
  writeFileSync(
    `build/fhn/${label}-${sim.steps}.ppm`,
    Buffer.concat([Buffer.from("P6\n200 228\n255\n"), bytes])
  );
}
