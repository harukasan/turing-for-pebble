import type { Model, Parameters } from "../../lib/simulation";
import type { ClockFont } from "../../lib/clock-fonts";

export type Engine =
  "u8-200" | "q15-100" | "q15-120" | "float-200" | "float-100" | "float-120";

export const engineWidth = (engine: Engine) => Number(engine.split("-")[1]);
export const gridHeight = (width: number) => Math.floor((width * 228) / 200);
export const engineMode = (engine: Engine) =>
  engine.endsWith("200") ? 0 : engine.endsWith("120") ? 3 : 1;
export const isFloat = (engine: Engine) => engine.startsWith("float");
export const HALO = 1;

type KeysOf<P> = P extends Parameters ? Exclude<keyof P, "model"> : never;
/** The numeric parameters of a model (of every model by default), without
 * the model name. */
export type ParameterKey<M extends Model = Model> = KeysOf<
  Extract<Parameters, { model: M }>
>;

export const defaultParameters: Parameters = {
  model: "gray-scott",
  feed: 0.029,
  kill: 0.057,
  da: 1,
  db: 0.5,
  dt: 1,
};

export type PlayerSettings = {
  params: Parameters;
  speed: number;
  palette: string;
  quantize: boolean;
  interpolate: boolean;
  clock: boolean;
  font: ClockFont;
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
  avoid: true,
};
