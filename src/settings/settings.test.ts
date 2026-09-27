import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { PALETTE_COUNT, PALETTE_IDS, PALETTES } from "../../lib/palettes.ts";
import { presets } from "../../lib/simulation.ts";
import {
  parameterVector,
  presetById,
  presetParameters,
} from "../../lib/presets.ts";
import {
  DEFAULT_SETTINGS,
  FIXED_KEYS,
  fromEmbedded,
  fromQuery,
  initialSettings,
  PEBBLE_COLORS,
  presetOf,
  DIFFUSION_PRESETS,
  diffusionPresetOf,
  q15,
  SETTING_KEYS,
  SETTING_MAX,
  settingMin,
  stopKeys,
  customStops,
  withStops,
  toJson,
  toParameters,
  toQuery,
  validate,
  vectorOf,
  withModel,
  withPreset,
  withVector,
  withRest,
  fromFileJson,
  toFileJson,
} from "./settings.ts";

const read = (path: string) =>
  readFileSync(new URL(`../../${path}`, import.meta.url), "utf8");

test("the settings follow the watch's message keys and defaults", () => {
  const keys = JSON.parse(read("pebble/package.json")).pebble.messageKeys;
  assert.deepEqual(
    keys,
    SETTING_KEYS.map((key) => key.toUpperCase())
  );
  const config = read("pebble/src/c/config.h");
  const define = (name: string) => {
    // The last definition is the default of an unfolded build.
    const match = [
      ...config.matchAll(new RegExp(`^#define ${name} (.+)$`, "gm")),
    ].at(-1);
    assert(match, name);
    return match[1];
  };
  assert.equal(Number(define("RD_DEFAULT_MODEL")), DEFAULT_SETTINGS.model);
  const vector = define("RD_DEFAULT_PARAMS").split(",").map(Number);
  assert.deepEqual(vectorOf(DEFAULT_SETTINGS).slice(0, vector.length), vector);
  assert(
    vectorOf(DEFAULT_SETTINGS)
      .slice(vector.length)
      .every((v) => v === 0)
  );
  for (const key of FIXED_KEYS)
    assert.equal(
      Number(define(`RD_DEFAULT_${key.toUpperCase()}`)),
      DEFAULT_SETTINGS[key],
      key
    );
  assert.equal(SETTING_MAX.palette, PALETTE_COUNT - 1);
  assert.deepEqual(
    PALETTES.map((p) => p.id),
    PALETTE_IDS
  );
  assert.equal(presetOf(DEFAULT_SETTINGS), 0);
  assert.equal(diffusionPresetOf(DEFAULT_SETTINGS), 2);
});

test("validation accepts integers in range and nothing else", () => {
  assert.deepEqual(validate(DEFAULT_SETTINGS), DEFAULT_SETTINGS);
  const maxed = { ...DEFAULT_SETTINGS, ...SETTING_MAX };
  assert.deepEqual(validate(maxed), maxed);
  for (const key of FIXED_KEYS) {
    assert.equal(
      validate({ ...DEFAULT_SETTINGS, [key]: settingMin(key) - 1 }),
      null,
      key
    );
    assert.equal(
      validate({ ...DEFAULT_SETTINGS, [key]: SETTING_MAX[key] + 1 }),
      null,
      key
    );
    assert.equal(validate({ ...DEFAULT_SETTINGS, [key]: 0.5 }), null, key);
    assert.equal(validate({ ...DEFAULT_SETTINGS, [key]: "1" }), null, key);
  }
  for (const key of SETTING_KEYS) {
    const missing: Record<string, number> = { ...DEFAULT_SETTINGS };
    delete missing[key];
    assert.equal(validate(missing), null, key);
  }
  assert.equal(validate(null), null);
  assert.equal(validate(1), null);
});

test("the vector is checked by the model's ranges", () => {
  const spiral = withPreset(DEFAULT_SETTINGS, presetById("fhn-spiral")!);
  assert.equal(spiral.model, 1);
  assert.deepEqual(validate(spiral), spiral);
  assert.equal(validate({ ...DEFAULT_SETTINGS, model: 2 }), null);
  assert.equal(validate({ ...DEFAULT_SETTINGS, model: -1 }), null);
  // Gray-Scott takes 0 to 32768 and zeros after its five entries.
  assert.equal(validate({ ...DEFAULT_SETTINGS, p0: -1 }), null);
  assert.equal(validate({ ...DEFAULT_SETTINGS, p4: 32769 }), null);
  assert.equal(validate({ ...DEFAULT_SETTINGS, p5: 1 }), null);
  // FitzHugh-Nagumo: ru at most 8192, k and rest signed, init 0 or 1.
  assert.equal(validate({ ...spiral, p2: 8193 }), null);
  assert.notEqual(validate({ ...spiral, p5: -32768 }), null);
  assert.equal(validate({ ...spiral, p5: -32769 }), null);
  assert.notEqual(validate({ ...spiral, p7: 32768 }), null);
  assert.equal(validate({ ...spiral, p8: 2 }), null);
  assert.equal(validate({ ...spiral, p9: 1 }), null);
});

test("query strings and embedded JSON round-trip, with negative entries", () => {
  const settings = {
    ...withPreset(DEFAULT_SETTINGS, presetById("fhn-hex")!),
    palette: SETTING_MAX.palette,
    avoid: 0,
  };
  assert(settings.p5 < 0);
  assert.deepEqual(fromQuery(new URLSearchParams(toQuery(settings))), settings);
  assert.deepEqual(fromEmbedded(toJson(settings)), settings);
  assert.deepEqual(Object.keys(JSON.parse(toJson(settings))), SETTING_KEYS);
  assert.equal(fromQuery(new URLSearchParams("p0=1")), null);
  assert.equal(
    fromQuery(
      new URLSearchParams(toQuery(settings).replace("p0=1311", "p0=1e3"))
    ),
    null
  );
  assert.equal(fromEmbedded("{"), null);
  // A query string with the Gray-Scott names from before the model.
  const legacy = toQuery(DEFAULT_SETTINGS)
    .replace(/model=\d+&/, "")
    .replace(/p(\d)=(-?\d+)&?/g, (_, i, v) =>
      Number(i) < 5 ? `${["feed", "kill", "da", "db", "dt"][i]}=${v}&` : ""
    );
  assert.deepEqual(fromQuery(new URLSearchParams(legacy)), DEFAULT_SETTINGS);
});

test("embedded settings come first, then the query string, then the defaults", () => {
  const embedded = { ...DEFAULT_SETTINGS, palette: 4 };
  const query = { ...DEFAULT_SETTINGS, palette: 5 };
  const search = `?${toQuery(query)}&return_to=x`;
  assert.deepEqual(initialSettings(toJson(embedded), search), embedded);
  assert.deepEqual(initialSettings("__SETTINGS__", search), query);
  assert.deepEqual(initialSettings("{", search), query);
  assert.deepEqual(initialSettings(null, "?return_to=x"), DEFAULT_SETTINGS);
});

test("presets are found by their model and entries", () => {
  presets.forEach((preset, i) => {
    const settings = withPreset(DEFAULT_SETTINGS, preset);
    assert.equal(presetOf(settings), i, preset.id);
    assert.deepEqual(toParameters(settings).model, preset.model);
  });
  // A Gray-Scott preset keeps the diffusion and time step of the settings.
  const coral = withPreset(DEFAULT_SETTINGS, presetById("coral")!);
  assert.equal(coral.p2, DEFAULT_SETTINGS.p2);
  assert.equal(presetOf({ ...DEFAULT_SETTINGS, p0: 951 }), -1);
  // A FitzHugh-Nagumo preset sets every entry, and another model starts
  // from its first preset.
  const stripes = withModel(DEFAULT_SETTINGS, "fhn");
  assert.deepEqual(
    vectorOf(stripes),
    parameterVector(presetParameters(presetById("fhn-stripes")!))
  );
  assert.equal(presetOf(stripes), presets.indexOf(presetById("fhn-stripes")!));
  assert.equal(presetOf({ ...stripes, p8: 1 }), -1);
  // The parameters of the settings round-trip through the Q15 vector.
  const params = toParameters(
    withVector(DEFAULT_SETTINGS, "fhn", vectorOf(stripes))
  );
  assert.equal(params.model, "fhn");
  assert.deepEqual(parameterVector(params), vectorOf(stripes));
});

test("stripe widths are found by their Q15 Da and Db of Gray-Scott", () => {
  DIFFUSION_PRESETS.forEach((p, i) =>
    assert.equal(
      diffusionPresetOf({ ...DEFAULT_SETTINGS, p2: q15(p.da), p3: q15(p.db) }),
      i
    )
  );
  assert.equal(diffusionPresetOf({ ...DEFAULT_SETTINGS, p2: 32767 }), -1);
  assert.equal(diffusionPresetOf(withModel(DEFAULT_SETTINGS, "fhn")), -1);
  // Back to Gray-Scott from another model: the watch's thin stripes.
  const back = withModel(withModel(DEFAULT_SETTINGS, "fhn"), "gray-scott");
  assert.deepEqual(vectorOf(back), vectorOf(DEFAULT_SETTINGS));
  assert.equal(diffusionPresetOf(back), 2);
});

test("the custom palette uses 2 to 4 stops, dark to light", () => {
  const s = {
    ...DEFAULT_SETTINGS,
    low: 0x000000,
    mid1: 0x0000ff,
    mid2: 0x00ff00,
    high: 0xffffff,
  };
  assert.deepEqual(stopKeys(s), ["low", "high"]);
  assert.deepEqual(customStops({ ...s, stops: 3 }), [
    [0, 0, 0],
    [0, 0, 255],
    [255, 255, 255],
  ]);
  assert.deepEqual(stopKeys({ ...s, stops: 4 }), [
    "low",
    "mid1",
    "mid2",
    "high",
  ]);
  const gray = withStops({ ...s, stops: 2 }, 4);
  assert.equal(gray.stops, 4);
  assert.equal(gray.mid1, 0x555555);
  assert.equal(gray.mid2, 0xaaaaaa);
  assert.equal(withStops(s, 3).mid1, 0x808080);
  assert.equal(withStops({ ...s, stops: 4 }, 2).mid1, s.mid1);
  // Middle colors already picked stay when stops are added or removed.
  const red = { ...s, stops: 3, mid1: 0xff0000 };
  const four = withStops(red, 4);
  assert.equal(four.mid1, 0xff0000);
  assert.equal(four.mid2, 0xff8080);
  assert.equal(withStops(four, 3).mid1, 0xff0000);
  assert.equal(withStops(withStops(four, 3), 4).mid2, 0xff8080);
});

test("settings files round-trip, take the page's bare JSON, and read version 1", () => {
  const settings = { ...DEFAULT_SETTINGS, palette: 9, stops: 4 };
  const text = toFileJson(settings);
  assert.equal(JSON.parse(text).format, "turing-pattern-watchface-settings");
  assert.equal(JSON.parse(text).version, 2);
  assert.deepEqual(fromFileJson(text), settings);
  assert.deepEqual(fromFileJson(toJson(settings)), settings);
  const file = JSON.parse(text);
  assert.equal(fromFileJson(JSON.stringify({ ...file, version: 3 })), null);
  assert.equal(fromFileJson(JSON.stringify({ ...file, format: "x" })), null);
  file.settings.stops = 5;
  assert.equal(fromFileJson(JSON.stringify(file)), null);
  assert.equal(fromFileJson("not json"), null);
  // A version 1 file names the Gray-Scott coefficients.
  const { model, p0, p1, p2, p3, p4, p5, p6, p7, p8, p9, ...fixed } = settings;
  void model;
  void [p5, p6, p7, p8, p9];
  const legacy = {
    format: "turing-pattern-watchface-settings",
    version: 1,
    settings: { feed: p0, kill: p1, da: p2, db: p3, dt: p4, ...fixed },
  };
  assert.deepEqual(fromFileJson(JSON.stringify(legacy)), settings);
});

test("the color picker offers the 64 watch colors", () => {
  assert.equal(new Set(PEBBLE_COLORS.map((c) => c.join())).size, 64);
  assert(PEBBLE_COLORS.flat().every((c) => [0, 85, 170, 255].includes(c)));
});

test("FitzHugh–Nagumo settings carry the resting point of their k and av", () => {
  const hex = withPreset(DEFAULT_SETTINGS, presetById("fhn-hex")!);
  // A different k moves rest with it, here the rest entry p7 from p5.
  const moved = withRest({ ...hex, p5: q15(-0.1) });
  assert.notEqual(moved.p7, hex.p7);
  assert.deepEqual(withRest(moved), moved);
  // Imported settings get the resting point, whatever rest they carried.
  assert.deepEqual(validate({ ...hex, p7: 1000 }), hex);
  // Gray–Scott settings are left as they are.
  assert.equal(withRest(DEFAULT_SETTINGS), DEFAULT_SETTINGS);
});
