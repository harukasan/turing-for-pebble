import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import {
  makeHeader,
  makeSettings,
  type SettingsInput,
} from "./serialization.ts";
import {
  type GrayScottParameters,
  presetById,
  presetParameters,
} from "../../lib/simulation.ts";

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
  face: "digital",
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
  assert.equal(settings.face, "digital");
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
      params: { ...(input.params as GrayScottParameters), da: 0.7, db: 0.35 },
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
      params: {
        model: "gray-scott",
        feed: 0.035,
        kill: 0.065,
        da: 0.8,
        db: 0.3,
        dt: 0.9,
      },
      palette: "magma",
      interpolate: false,
      font: "bitham",
      avoid: false,
      clock: false,
      face: "analog",
    })
  );
  assert.equal(header.get("RD_MODE"), "1");
  assert.equal(header.get("RD_RENDER_FLAGS"), "0");
  assert.equal(header.get("RD_DEFAULT_MODEL"), "0");
  assert.equal(
    header.get("RD_DEFAULT_PARAMS"),
    [0.035, 0.065, 0.8, 0.3, 0.9].map((v) => Math.round(v * 32768)).join(", ")
  );
  assert.equal(header.get("RD_DEFAULT_PALETTE"), "4");
  assert.equal(header.get("RD_DEFAULT_LOW"), "0x000004");
  assert.equal(header.get("RD_DEFAULT_HIGH"), "0xfcfdbf");
  assert.equal(header.get("RD_DEFAULT_FONT"), "1");
  assert.equal(header.get("RD_DEFAULT_AVOID"), "0");
  assert.equal(header.get("RD_DEFAULT_CLOCK"), "0");
  assert.equal(header.get("RD_DEFAULT_FACE"), "1");
});

test("the analog face is part of the build identity", () => {
  const analog = { ...input, face: "analog" as const };
  assert.equal(makeSettings(analog).face, "analog");
  assert.equal(defines(makeHeader(analog)).get("RD_DEFAULT_FACE"), "1");
  assert.equal(defines(makeHeader(input)).get("RD_DEFAULT_FACE"), "0");
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
    ru: 860 / 32768,
    rv: 2150 / 32768,
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
  // A build of another mode from this header stops at the mode guard.
  assert.match(
    header,
    /#if RD_DEFAULT_MODEL == 1 && RD_MODE != 1 && RD_MODE != 3\n#error/
  );
  assert.throws(() => makeHeader({ ...fhn("fhn-spiral"), engine: "u8-200" }));
  assert.throws(() =>
    makeHeader({ ...fhn("fhn-spiral"), engine: "float-120" })
  );
});
