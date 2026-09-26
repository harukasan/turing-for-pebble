import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import {
  makeHeader,
  makeSettings,
  type SettingsInput,
} from "./serialization.ts";

const input: SettingsInput = {
  params: { feed: 0.029, kill: 0.057, da: 1, db: 0.5, dt: 1 },
  engine: "q15-120",
  seed: 42,
  palette: "green",
  quantize: true,
  interpolate: true,
  clock: true,
  font: "leco",
  avoid: true,
  steps: 123,
};

test("settings keep the watch build identity and are not a field snapshot", () => {
  const settings = makeSettings(input);
  assert.equal(settings.method, "q15-120");
  assert.equal(settings.width, 120);
  assert.equal(settings.height, 136);
  assert.equal(settings.seed, 42);
  assert.equal(settings.version, 4);
  assert.equal(settings.steps, 123);
  assert.equal(settings.minuteSteps, 300);
  assert.equal(settings.avoidDigits, true);
  assert.equal("field" in settings, false);
  assert.deepEqual(settings.effective, {
    feed: Math.round(0.029 * 32768) / 32768,
    kill: Math.round(0.057 * 32768) / 32768,
    da: (Math.round(32768 / 20) * 20) / 32768,
    db: (Math.round(16384 / 20) * 20) / 32768,
    dt: 1,
  });
});

/** The #define names of a header and the last value of each. */
const defines = (text: string) =>
  new Map(
    [...text.matchAll(/^#define (\w+) (.*)$/gm)].map((m) => [m[1], m[2]])
  );

test("config.h defines what pebble/src/c/config.h defines, with its defaults", () => {
  const header = defines(
    makeHeader({
      ...input,
      params: { ...input.params, da: 0.7, db: 0.35 },
    })
  );
  const watch = defines(
    readFileSync(
      new URL("../../pebble/src/c/config.h", import.meta.url),
      "utf8"
    )
  );
  assert.deepEqual([...header.keys()].sort(), [...watch.keys()].sort());
  for (const [name, value] of watch)
    if (name !== "RD_RENDER_FLAGS") assert.equal(header.get(name), value, name);
  assert.equal(header.get("RD_RENDER_FLAGS"), "2");
  assert.throws(() => makeHeader({ ...input, engine: "float-120" }));
});

test("config.h carries the demo's settings as the watch defaults", () => {
  const header = defines(
    makeHeader({
      ...input,
      engine: "q15-100",
      params: { feed: 0.035, kill: 0.065, da: 0.8, db: 0.3, dt: 0.9 },
      palette: "magma",
      interpolate: false,
      font: "bitham",
      avoid: false,
      clock: false,
    })
  );
  assert.equal(header.get("RD_MODE"), "1");
  assert.equal(header.get("RD_RENDER_FLAGS"), "0");
  assert.equal(
    header.get("RD_DEFAULT_FEED"),
    String(Math.round(0.035 * 32768))
  );
  assert.equal(
    header.get("RD_DEFAULT_KILL"),
    String(Math.round(0.065 * 32768))
  );
  assert.equal(header.get("RD_DEFAULT_DA"), String(Math.round(0.8 * 32768)));
  assert.equal(header.get("RD_DEFAULT_DB"), String(Math.round(0.3 * 32768)));
  assert.equal(header.get("RD_DEFAULT_DT"), String(Math.round(0.9 * 32768)));
  assert.equal(header.get("RD_DEFAULT_PALETTE"), "4");
  assert.equal(header.get("RD_DEFAULT_LOW"), "0x000004");
  assert.equal(header.get("RD_DEFAULT_HIGH"), "0xfcfdbf");
  assert.equal(header.get("RD_DEFAULT_FONT"), "1");
  assert.equal(header.get("RD_DEFAULT_AVOID"), "0");
  assert.equal(header.get("RD_DEFAULT_CLOCK"), "0");
});

test("Float32 settings use requested coefficients without Q15 rounding", () => {
  const settings = makeSettings({
    ...input,
    engine: "float-100",
    clock: false,
  });
  assert.equal(settings.width, 100);
  assert.equal(settings.height, 114);
  assert.equal(settings.rounding, "float32-storage");
  assert.deepEqual(settings.effective, input.params);
  assert.equal(settings.avoidDigits, false);
  assert.equal(settings.minuteSteps, 16);
});
