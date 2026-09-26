import assert from "node:assert/strict";
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

test("config.h uses the watch mode, fixed-point parameters and clock options", () => {
  const header = makeHeader(input);
  assert.match(header, /#define RD_MODE 3\n/);
  assert.match(header, /#define RD_SEED 42u\n/);
  assert.match(header, /#define RD_FEED 950\n/);
  assert.match(header, /#define RD_KILL 1868\n/);
  assert.match(header, /#define RD_RENDER_FLAGS 2\n/);
  assert.match(header, /#define RD_STARTUP_MS 30000\n/);
  assert.match(header, /#define RD_STARTUP_STEPS_MAX 2500\n/);
  assert.match(header, /#define RD_MINUTE_STEPS \(RD_AVOID \? 300 : 16\)\n/);
  assert.throws(() => makeHeader({ ...input, engine: "float-120" }));
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
