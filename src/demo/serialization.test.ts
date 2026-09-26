import assert from "node:assert/strict";
import { test } from "node:test";
import {
  makeHeader,
  makeSettings,
  type SettingsInput,
} from "./serialization.ts";
import { presetById, presetParameters } from "../../lib/simulation.ts";

const input: SettingsInput = {
  params: {
    model: "gray-scott",
    feed: 0.029,
    kill: 0.057,
    da: 1,
    db: 0.5,
    dt: 1,
  },
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
  assert.equal(settings.version, 5);
  assert.equal(settings.model, "Gray-Scott");
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
  assert.match(header, /#define RD_DEFAULT_MODEL 0\n/);
  assert.match(
    header,
    /#define RD_DEFAULT_PARAMS 950, 1868, 32768, 16384, 32768\n/
  );
  assert.doesNotMatch(header, /RD_FEED/);
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
  const { model, ...values } = input.params;
  assert.equal(model, "gray-scott");
  assert.equal(settings.model, "Gray-Scott");
  assert.deepEqual(settings.effective, values);
  assert.equal(settings.avoidDigits, false);
  assert.equal(settings.minuteSteps, 16);
});

const fhn = (id: string) => ({
  ...input,
  params: presetParameters(presetById(id)!),
});

test("FitzHugh-Nagumo settings record the model and the folded values", () => {
  const settings = makeSettings(fhn("fhn-hex"));
  assert.equal(settings.model, "FitzHugh-Nagumo");
  assert.equal(settings.version, 5);
  assert.equal(settings.storage, "q15-fraction-(x+2)/4");
  assert.match(settings.initialization, /u=rest\+0\.5 over u=rest/);
  assert.equal("feed" in settings, false);
  assert.equal((settings as Record<string, unknown>).k, -0.22);
  assert.deepEqual(settings.effective, {
    du: (Math.round(1311 / 20) * 20) / 32768,
    dv: (Math.round(32768 / 20) * 20) / 32768,
    ru: 573 / 32768,
    rv: 1434 / 32768,
    av: 19661 / 32768,
    k: (Math.floor((-7209 + 2) / 4) * 4) / 32768,
    dt: 1,
    rest: (Math.floor((-9585 + 2) / 4) * 4) / 32768,
    init: 0,
  });
  assert.match(
    makeSettings(fhn("fhn-spiral")).initialization,
    /^broken wave: u=1 at display rows 190–197 and v=1 at rows 174–189/
  );
});

test("config.h carries the FitzHugh-Nagumo model and its nine parameters", () => {
  const header = makeHeader(fhn("fhn-spiral"));
  assert.match(header, /#define RD_MODE 3\n/);
  assert.match(header, /#define RD_DEFAULT_MODEL 1\n/);
  assert.match(
    header,
    /#define RD_DEFAULT_PARAMS 6554, 0, 8192, 410, 32768, -9830, 32768, -21935, 1\n/
  );
  assert.throws(() => makeHeader({ ...fhn("fhn-spiral"), engine: "u8-200" }));
  assert.throws(() =>
    makeHeader({ ...fhn("fhn-spiral"), engine: "float-120" })
  );
});
