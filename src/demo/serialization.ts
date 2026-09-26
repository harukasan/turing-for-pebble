import { parameterValues, type Parameters } from "../../lib/simulation.ts";
import { effective } from "../../lib/wasm-simulation.ts";
import { fontIndex, type ClockFont } from "../../lib/clock-fonts.ts";
import {
  engineMode,
  engineWidth,
  gridHeight,
  isFloat,
  HALO,
  type Engine,
} from "../core/modes.ts";

export const DEVICE_MINUTE_STEPS = 300;
export const DEVICE_MINUTE_STEPS_PLAIN = 16;

export type SettingsInput = {
  params: Parameters;
  engine: Engine;
  seed: number;
  palette: string;
  quantize: boolean;
  interpolate: boolean;
  clock: boolean;
  font: ClockFont;
  avoid: boolean;
  steps: number;
};

export function makeSettings(input: SettingsInput) {
  const {
    params,
    engine,
    seed,
    palette,
    quantize,
    interpolate,
    clock,
    font,
    avoid,
    steps,
  } = input;
  const width = engineWidth(engine);
  const floatMode = isFloat(engine);
  const values = parameterValues(params);
  return {
    model: "Gray-Scott",
    version: 4,
    method: engine,
    rounding: floatMode
      ? "float32-storage"
      : engine === "u8-200"
        ? "floyd-steinberg-dithered"
        : "floyd-steinberg-nearest",
    storage: floatMode
      ? "float32"
      : engine === "u8-200"
        ? "packed-a7-linear-b9-sqrt"
        : "q15",
    effective: floatMode
      ? values
      : parameterValues(effective(params, engineMode(engine))),
    ...values,
    width,
    height: gridHeight(width),
    seed,
    boundary: "periodic",
    laplacian: { center: -1, axial: 0.2, diagonal: 0.05 },
    initialization: "24 display-coordinate disks, radius 4–9, A=0.5 B=0.25",
    palette,
    quantize,
    interpolate,
    clock,
    font,
    avoidDigits: clock && avoid,
    halo: HALO,
    minuteSteps:
      clock && avoid ? DEVICE_MINUTE_STEPS : DEVICE_MINUTE_STEPS_PLAIN,
    steps,
  };
}

export function makeHeader(input: SettingsInput) {
  if (isFloat(input.engine))
    throw new Error("Float32 cannot produce Pebble config.h");
  const q = parameterValues(effective(input.params));
  const { engine, seed, palette, interpolate, clock, font, avoid } = input;
  return `/* Generated configuration, core v4 */\n#ifndef RD_MODE\n#define RD_MODE ${engineMode(engine)}\n#endif\n#define RD_SEED ${seed}u\n#define RD_FEED ${Math.round(Number(q.feed) * 32768)}\n#define RD_KILL ${Math.round(Number(q.kill) * 32768)}\n#define RD_DA ${Math.round(Number(q.da) * 32768)}\n#define RD_DB ${Math.round(Number(q.db) * 32768)}\n#define RD_DT ${Math.round(Number(q.dt) * 32768)}\n#define RD_PALETTE ${palette === "green" ? 0 : palette === "blue" ? 1 : 2}\n#define RD_CLOCK ${Number(clock)}\n#ifndef RD_STARTUP_MS\n#define RD_STARTUP_MS 30000\n#endif\n#define RD_STARTUP_STEPS_MAX 2500\n#ifndef RD_RENDER_FLAGS\n#define RD_RENDER_FLAGS ${interpolate ? 2 : 0}\n#endif\n#ifndef RD_FONT\n#define RD_FONT ${fontIndex(font)}\n#endif\n#ifndef RD_AVOID\n#define RD_AVOID ${Number(avoid)}\n#endif\n#define RD_HALO ${HALO}\n#define RD_MINUTE_STEPS (RD_AVOID ? ${DEVICE_MINUTE_STEPS} : ${DEVICE_MINUTE_STEPS_PLAIN})\n`;
}
