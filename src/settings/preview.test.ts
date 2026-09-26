import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { PALETTE_CUSTOM } from "../../lib/palettes.ts";
import { loadCore } from "../../lib/wasm-simulation.ts";
import { PreviewField, PREVIEW_MODE, PREVIEW_SEED } from "./preview.ts";
import { DEFAULT_SETTINGS, type WatchSettings } from "./settings.ts";

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
const run = (settings: WatchSettings) => {
  const field = new PreviewField(core);
  field.start(settings, date);
  field.step(1000);
  return field;
};

test("the preview runs the watch's field", () => {
  assert.equal(PREVIEW_MODE, 3);
  assert.equal(PREVIEW_SEED, 42);
  assert.equal(run(DEFAULT_SETTINGS).hash(), golden(0));
  assert.equal(run({ ...DEFAULT_SETTINGS, font: 1 }).hash(), golden(1));
  assert.equal(run({ ...DEFAULT_SETTINGS, clock: 0 }).hash(), golden());
  assert.equal(run({ ...DEFAULT_SETTINGS, avoid: 0 }).hash(), golden());
});

test("recoloring keeps the field and custom stops draw like the palette", () => {
  const draw = (field: PreviewField) => {
    const pixels = { data: new Uint8ClampedArray(200 * 228 * 4) } as ImageData;
    field.render(pixels);
    return pixels.data;
  };
  const field = run(DEFAULT_SETTINGS);
  const lime = draw(field);
  field.recolor({ ...DEFAULT_SETTINGS, palette: PALETTE_CUSTOM });
  assert.equal(field.hash(), golden(0));
  assert.deepEqual(draw(field), lime);
  field.recolor({
    ...DEFAULT_SETTINGS,
    palette: PALETTE_CUSTOM,
    low: 0x00002d,
    high: 0x55ffff,
  });
  const custom = draw(field);
  field.recolor({ ...DEFAULT_SETTINGS, palette: 1 });
  assert.deepEqual(custom, draw(field));
  assert.notDeepEqual(custom, lime);
});
