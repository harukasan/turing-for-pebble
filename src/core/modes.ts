import type { Parameters } from "../../lib/simulation";
import type { ClockFont } from "../../lib/clock-fonts";
import type { PaletteId } from "../../lib/palettes";

export type Engine =
  "u8-200" | "q15-100" | "q15-120" | "float-200" | "float-100" | "float-120";

export const engineWidth = (engine: Engine) => Number(engine.split("-")[1]);
export const gridHeight = (width: number) => Math.floor((width * 228) / 200);
export const engineMode = (engine: Engine) =>
  engine.endsWith("200") ? 0 : engine.endsWith("120") ? 3 : 1;
export const isFloat = (engine: Engine) => engine.startsWith("float");
export const HALO = 1;
/** Duration of the sweep of the analog hands to a new minute (RD_SWEEP_MS
 * on the watch). */
export const SWEEP_MS = 1000;

/** Clock face: the digital HH:MM and date, or hands and the date. */
export type ClockFace = "digital" | "analog";

/** Slider range and step of each coefficient, shared by the demo and the
 * watch settings page. */
export const parameterBounds: Record<
  keyof Parameters,
  [number, number, number]
> = {
  feed: [0.01, 0.1, 0.0001],
  kill: [0.03, 0.075, 0.0001],
  da: [0.1, 1, 0.01],
  db: [0.01, 0.5, 0.01],
  dt: [0.1, 1, 0.1],
};

export const defaultParameters: Parameters = {
  feed: 0.029,
  kill: 0.057,
  da: 1,
  db: 0.5,
  dt: 1,
};

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
