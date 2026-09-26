import {
  modelIndex,
  parameterOrder,
  parameterValues,
  parameterVector,
  type Parameters,
} from "../../lib/simulation.ts";
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
import {
  WAVE_END,
  WAVE_EXCITED,
  WAVE_REFRACTORY,
  WAVE_TOP,
} from "../../lib/fhn-simulation.ts";

export const DEVICE_MINUTE_STEPS = 300;

const MODEL_NAMES = {
  "gray-scott": "Gray-Scott",
  fhn: "FitzHugh-Nagumo",
} as const;

/** The initial field of a parameter set, as rd_init_model builds it. */
function initialization(params: Parameters) {
  if (params.model === "gray-scott")
    return "24 display-coordinate disks, radius 4–9, A=0.5 B=0.25 over A=1 B=0";
  const rest = "u=rest, v=rest/av";
  return params.init === 1
    ? `broken wave: u=1 at display rows ${WAVE_TOP}–${WAVE_TOP + WAVE_EXCITED - 1} and v=1 at rows ${WAVE_TOP - WAVE_REFRACTORY}–${WAVE_TOP - 1}, columns 0–${WAVE_END - 1}, over ${rest}`
    : `24 display-coordinate disks, radius 4–9, u=rest+0.5 over ${rest}`;
}
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
    model: MODEL_NAMES[params.model],
    version: 5,
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
        : params.model === "fhn"
          ? "q15-fraction-(x+2)/4"
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
    initialization: initialization(params),
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

/** The watch configuration of a Wasm engine: the mode, the seed, the model
 * and its Q15 parameter vector (the core applies its own folding), and the
 * clock and rendering options, in the form of pebble/src/c/config.h. */
export function makeHeader(input: SettingsInput) {
  if (isFloat(input.engine))
    throw new Error("Float32 cannot produce Pebble config.h");
  const { params, engine, seed, palette, interpolate, clock, font, avoid } =
    input;
  const mode = engineMode(engine);
  if (params.model === "fhn" && mode !== 1 && mode !== 3)
    throw new Error("FitzHugh-Nagumo runs only in the Q15 modes");
  const vector = parameterVector(params).slice(
    0,
    parameterOrder[params.model].length
  );
  return `/* Generated configuration, core v5 */\n#ifndef RD_MODE\n#define RD_MODE ${mode}\n#endif\n#define RD_SEED ${seed}u\n#ifndef RD_DEFAULT_MODEL\n#define RD_DEFAULT_MODEL ${modelIndex[params.model]}\n#endif\n#ifndef RD_DEFAULT_PARAMS\n#define RD_DEFAULT_PARAMS ${vector.join(", ")}\n#endif\n#define RD_PALETTE ${palette === "green" ? 0 : palette === "blue" ? 1 : 2}\n#define RD_CLOCK ${Number(clock)}\n#ifndef RD_STARTUP_MS\n#define RD_STARTUP_MS 30000\n#endif\n#define RD_STARTUP_STEPS_MAX 2500\n#ifndef RD_RENDER_FLAGS\n#define RD_RENDER_FLAGS ${interpolate ? 2 : 0}\n#endif\n#ifndef RD_FONT\n#define RD_FONT ${fontIndex(font)}\n#endif\n#ifndef RD_AVOID\n#define RD_AVOID ${Number(avoid)}\n#endif\n#define RD_HALO ${HALO}\n#define RD_MINUTE_STEPS (RD_AVOID ? ${DEVICE_MINUTE_STEPS} : ${DEVICE_MINUTE_STEPS_PLAIN})\n`;
}
