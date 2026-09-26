import type { Model, Parameters } from "./simulation.ts";

/** A named parameter set. The id is stable across the Web, the report
 * scripts, and the C harnesses, the name is the Japanese button label. */
export type Preset = { id: string; name: string } & Parameters;

/** Length of the parameter vector of the C core (RD_PARAM_MAX of
 * core/rd.h). */
export const RD_PARAM_MAX = 10;

/** Model number of the C core (RD_MODEL_* of core/rd.h). */
export const modelIndex: Record<Model, number> = { "gray-scott": 0, fhn: 1 };

/** Order of each model's parameters in the C parameter vector. */
export const parameterOrder = {
  "gray-scott": ["feed", "kill", "da", "db", "dt"],
  fhn: ["du", "dv", "ru", "rv", "av", "k", "dt", "rest", "init"],
} as const satisfies Record<Model, readonly string[]>;

/** Parameters passed to the core as integers rather than Q15. */
const INTEGER_PARAMETERS = new Set(["init"]);

export const presets: Preset[] = [
  {
    id: "maze",
    name: "迷路",
    model: "gray-scott",
    feed: 0.029,
    kill: 0.057,
    da: 1,
    db: 0.5,
    dt: 1,
  },
  {
    id: "coral",
    name: "珊瑚",
    model: "gray-scott",
    feed: 0.0545,
    kill: 0.062,
    da: 1,
    db: 0.5,
    dt: 1,
  },
  {
    id: "mitosis",
    name: "細胞分裂",
    model: "gray-scott",
    feed: 0.0367,
    kill: 0.0649,
    da: 1,
    db: 0.5,
    dt: 1,
  },
  {
    id: "spots",
    name: "斑点",
    model: "gray-scott",
    feed: 0.035,
    kill: 0.065,
    da: 1,
    db: 0.5,
    dt: 1,
  },
  {
    id: "thin-line",
    name: "細線",
    model: "gray-scott",
    feed: 0.023,
    kill: 0.052,
    da: 1,
    db: 0.5,
    dt: 1,
  },
  {
    id: "fhn-stripes",
    name: "縞",
    model: "fhn",
    du: 0.05,
    dv: 1,
    ru: 0.01,
    rv: 0.025,
    av: 0.6,
    k: 0,
    dt: 1,
    rest: 0,
    init: 0,
  },
  {
    id: "fhn-hex",
    name: "六方斑点",
    model: "fhn",
    du: 0.04,
    dv: 1,
    ru: 0.0175,
    rv: 0.04375,
    av: 0.6,
    k: -0.22,
    dt: 1,
    rest: -0.2925,
    init: 0,
  },
  {
    id: "fhn-spiral",
    name: "らせん",
    model: "fhn",
    du: 0.2,
    dv: 0,
    ru: 0.25,
    rv: 0.0125,
    av: 1,
    k: -0.3,
    dt: 1,
    rest: -0.6694,
    init: 1,
  },
];

export const presetById = (id: string) =>
  presets.find((preset) => preset.id === id);

/** The parameters of a preset without its id and name. */
export const presetParameters = (preset: Preset) =>
  Object.fromEntries(
    Object.entries(preset).filter(([key]) => key !== "id" && key !== "name")
  ) as Parameters;

/** The numeric parameters without the model name. */
export const parameterValues = (params: Parameters) =>
  Object.fromEntries(
    Object.entries(params).filter(([key]) => key !== "model")
  ) as Record<string, number>;

/** The default parameters of a model, those of its first preset. */
export const defaultParametersFor = <M extends Model>(model: M) =>
  presetParameters(
    presets.find((preset) => preset.model === model)!
  ) as Extract<Parameters, { model: M }>;

/** The parameter vector the C core takes: each value in the model's order
 * rounded to Q15 (init as it is), padded with zeros to RD_PARAM_MAX
 * entries. */
export function parameterVector(params: Parameters): number[] {
  const values = params as unknown as Record<string, number>;
  const vector: number[] = parameterOrder[params.model].map((key) =>
    INTEGER_PARAMETERS.has(key)
      ? Math.round(values[key])
      : Math.round(values[key] * 32768)
  );
  while (vector.length < RD_PARAM_MAX) vector.push(0);
  return vector;
}
