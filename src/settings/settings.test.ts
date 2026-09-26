import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { PALETTE_COUNT } from "../../lib/palettes.ts";
import { presets } from "../../lib/simulation.ts";
import {
  DEFAULT_SETTINGS,
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
  toQuery,
  validate,
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
  for (const key of SETTING_KEYS) {
    const match = config.match(
      new RegExp(`^#define RD_DEFAULT_${key.toUpperCase()} (\\S+)$`, "m")
    );
    assert(match, key);
    assert.equal(Number(match[1]), DEFAULT_SETTINGS[key], key);
  }
  assert.equal(SETTING_MAX.palette, PALETTE_COUNT - 1);
  assert.equal(presetOf(DEFAULT_SETTINGS), 0);
  assert.equal(diffusionPresetOf(DEFAULT_SETTINGS), 2);
});

test("validation accepts integers up to each maximum and nothing else", () => {
  assert.deepEqual(validate(SETTING_MAX), SETTING_MAX);
  assert.deepEqual(validate(DEFAULT_SETTINGS), DEFAULT_SETTINGS);
  for (const key of SETTING_KEYS) {
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
    const missing: Record<string, number> = { ...DEFAULT_SETTINGS };
    delete missing[key];
    assert.equal(validate(missing), null, key);
  }
  assert.equal(validate(null), null);
  assert.equal(validate(1), null);
});

test("query strings and embedded JSON round-trip", () => {
  const settings = { ...SETTING_MAX, feed: 1786, avoid: 0 };
  assert.deepEqual(fromQuery(new URLSearchParams(toQuery(settings))), settings);
  assert.deepEqual(fromEmbedded(toJson(settings)), settings);
  assert.deepEqual(Object.keys(JSON.parse(toJson(settings))), SETTING_KEYS);
  assert.equal(fromQuery(new URLSearchParams("feed=1")), null);
  assert.equal(
    fromQuery(
      new URLSearchParams(toQuery(settings).replace("feed=1786", "feed=1e3"))
    ),
    null
  );
  assert.equal(fromEmbedded("{"), null);
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

test("presets are found by their Q15 feed and kill", () => {
  presets.forEach((p, i) =>
    assert.equal(
      presetOf({ ...DEFAULT_SETTINGS, feed: q15(p.feed), kill: q15(p.kill) }),
      i
    )
  );
  assert.equal(presetOf({ ...DEFAULT_SETTINGS, feed: 951 }), -1);
});

test("stripe widths are found by their Q15 Da and Db", () => {
  DIFFUSION_PRESETS.forEach((p, i) =>
    assert.equal(
      diffusionPresetOf({ ...DEFAULT_SETTINGS, da: q15(p.da), db: q15(p.db) }),
      i
    )
  );
  assert.equal(diffusionPresetOf({ ...DEFAULT_SETTINGS, da: 32767 }), -1);
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
});

test("settings files round-trip and take the page's bare JSON", () => {
  const settings = { ...DEFAULT_SETTINGS, palette: 9, stops: 4 };
  const text = toFileJson(settings);
  assert.equal(JSON.parse(text).format, "turing-pattern-watchface-settings");
  assert.deepEqual(fromFileJson(text), settings);
  assert.deepEqual(fromFileJson(toJson(settings)), settings);
  const file = JSON.parse(text);
  assert.equal(fromFileJson(JSON.stringify({ ...file, version: 2 })), null);
  assert.equal(fromFileJson(JSON.stringify({ ...file, format: "x" })), null);
  file.settings.stops = 5;
  assert.equal(fromFileJson(JSON.stringify(file)), null);
  assert.equal(fromFileJson("not json"), null);
});

test("the color picker offers the 64 watch colors", () => {
  assert.equal(new Set(PEBBLE_COLORS.map((c) => c.join())).size, 64);
  assert(PEBBLE_COLORS.flat().every((c) => [0, 85, 170, 255].includes(c)));
});
