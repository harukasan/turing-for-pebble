import {
  defaultParametersFor,
  type Model,
  type Parameters,
} from "../../lib/simulation.ts";
import type { ClockFont } from "../../lib/clock-fonts";
import type { PaletteId } from "../../lib/palettes";

/** The Q15 C core in WebAssembly, the watch's computation, or the Float32
 * reference on the same 120 × 136 grid. */
export type Engine = "q15-120" | "float-120";

export const GRID_WIDTH = 120;
export const GRID_HEIGHT = 136;
export const isFloat = (engine: Engine) => engine.startsWith("float");
export const HALO = 1;
/** Duration of the sweep of the analog hands to a new minute (RD_SWEEP_MS
 * on the watch). */
export const SWEEP_MS = 1000;

/** Clock face: the digital HH:MM and date, or hands and the date. */
export type ClockFace = "digital" | "analog";

/** The models and their display names. */
export const models: [Model, string][] = [
  ["gray-scott", "Gray–Scott"],
  ["fhn", "FitzHugh–Nagumo"],
];

export { defaultParametersFor };

type KeysOf<P> = P extends Parameters ? Exclude<keyof P, "model"> : never;
/** The numeric parameters of a model (of every model by default), without
 * the model name. */
export type ParameterKey<M extends Model = Model> = KeysOf<
  Extract<Parameters, { model: M }>
>;

/** The parameters shown as sliders: every one but the initial condition. */
/** The parameters shown as sliders: all but the initial condition and
 * rest, the resting point that k and av determine. */
export type SliderKey<M extends Model> = Exclude<
  ParameterKey<M>,
  "init" | "rest"
>;

/** Slider range and step of each coefficient of each model, shared by the
 * demo and the watch settings page. */
export const parameterBounds: {
  [M in Model]: Record<SliderKey<M>, [number, number, number]>;
} = {
  "gray-scott": {
    feed: [0.01, 0.1, 0.0001],
    kill: [0.03, 0.075, 0.0001],
    da: [0.1, 1, 0.01],
    db: [0.01, 0.5, 0.01],
    dt: [0.1, 1, 0.1],
  },
  fhn: {
    du: [0.01, 1, 0.01],
    dv: [0, 1, 0.01],
    ru: [0.01, 0.25, 0.001],
    rv: [0.001, 1, 0.001],
    av: [0, 1, 0.01],
    k: [-1, 1, 0.01],
    dt: [0.1, 1, 0.1],
  },
};

export const defaultParameters = defaultParametersFor("gray-scott");

export type PlayerSettings = {
  params: Parameters;
  speed: number;
  palette: PaletteId;
  quantize: boolean;
  interpolate: boolean;
  clock: boolean;
  font: ClockFont;
  face: ClockFace;
  /** Minute of the day shown, or null for the current time. */
  time: number | null;
  avoid: boolean;
};

export const defaultPlayerSettings: PlayerSettings = {
  params: { ...defaultParameters },
  speed: 16,
  palette: "green",
  quantize: true,
  interpolate: true,
  clock: true,
  font: "leco",
  face: "digital",
  time: null,
  avoid: true,
};
