import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { PALETTE_CUSTOM } from "../../lib/palettes.ts";
import {
  buildAnalogMask,
  loadCore,
  WasmSimulation,
} from "../../lib/wasm-simulation.ts";
import { hourAngle, minuteAngle } from "../../lib/clock-face.ts";
import { HALO } from "../core/modes.ts";
import { PreviewField, PREVIEW_MODE, PREVIEW_SEED } from "./preview.ts";
import {
  DEFAULT_SETTINGS,
  toParameters,
  type WatchSettings,
} from "./settings.ts";

const root = new URL("../../", import.meta.url);
// Node's fetch reads no files: serve the core from the repository.
globalThis.fetch = (async () =>
  new Response(
    readFileSync(new URL("public/wasm/rd.wasm", root))
  )) as typeof fetch;
const core = await loadCore("rd.wasm");

/** The mode 3 field hash after 1,000 steps in tests/golden-hashes.txt,
 * without a mask or with the mask of a font at 13:57 2046.08.29. */
function golden(font?: number) {
  const lines = readFileSync(new URL("tests/golden-hashes.txt", root), "utf8")
    .split("\n")
    .filter((line) => !line.startsWith("#"))
    .map((line) => line.trim().split(/\s+/));
  const mask = font === undefined ? [] : [font, 13, 57, 2046, 8, 29, 1];
  const line = lines.find(
    (l) =>
      l[0] === "3" &&
      l[1] === "1000" &&
      l.slice(3).join() === mask.map(String).join()
  );
  assert(line, `golden ${font}`);
  return Number(line[2]);
}

const date = new Date(2046, 7, 29, 13, 57);
/** The golden hashes use the core's default coefficients, Da 1 and Db 0.5. */
const base = { ...DEFAULT_SETTINGS, da: 32768, db: 16384 };
const run = (settings: WatchSettings) => {
  const field = new PreviewField(core);
  field.start(settings, date);
  field.step(1000);
  return field;
};

test("the preview runs the watch's field", () => {
  assert.equal(PREVIEW_MODE, 3);
  assert.equal(PREVIEW_SEED, 42);
  assert.equal(run(base).hash(), golden(0));
  assert.equal(run({ ...base, font: 1 }).hash(), golden(1));
  assert.equal(run({ ...base, clock: 0 }).hash(), golden());
  assert.equal(run({ ...base, avoid: 0 }).hash(), golden());
});

test("the analog face masks the hands of the time and the date", () => {
  const analog = (showDate: boolean) => {
    const sim = new WasmSimulation(core, PREVIEW_MODE, PREVIEW_SEED);
    sim.setMask(
      buildAnalogMask(
        core,
        sim.width,
        sim.height,
        0,
        hourAngle(13, 57),
        minuteAngle(57),
        date,
        HALO,
        showDate
      )
    );
    sim.step(toParameters(base), 1000);
    const hash = sim.hash();
    sim.dispose();
    return hash;
  };
  const face = { ...base, face: 1 };
  assert.equal(run(face).hash(), analog(true));
  assert.equal(run({ ...face, date: 0 }).hash(), analog(false));
  assert.notEqual(analog(true), analog(false));
  assert.notEqual(run(face).hash(), golden(0));
  assert.equal(run({ ...face, avoid: 0 }).hash(), golden());
  const pixels = new PreviewField(core).analog(face, date);
  assert.equal(pixels.length, 25 * 228);
});

test("recoloring keeps the field and custom stops draw like the palette", () => {
  const draw = (field: PreviewField) => {
    const pixels = { data: new Uint8ClampedArray(200 * 228 * 4) } as ImageData;
    field.render(pixels);
    return pixels.data;
  };
  const field = run(base);
  const lime = draw(field);
  field.recolor({ ...base, palette: PALETTE_CUSTOM });
  assert.equal(field.hash(), golden(0));
  assert.deepEqual(draw(field), lime);
  field.recolor({
    ...base,
    palette: PALETTE_CUSTOM,
    low: 0x00002d,
    high: 0x55ffff,
  });
  const custom = draw(field);
  field.recolor({ ...base, palette: 1 });
  assert.deepEqual(custom, draw(field));
  assert.notDeepEqual(custom, lime);
});
