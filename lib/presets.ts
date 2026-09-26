import type { Model, Parameters } from "./simulation.ts";

/** A named parameter set. The id is stable across the Web, the report
 * scripts, and the C harnesses, the name is the Japanese button label. */
export type Preset = { id: string; name: string } & Parameters;

/** Length of the parameter vector of the C core (RD_PARAM_MAX of
 * core/rd.h). */
export const RD_PARAM_MAX = 10;

/** Model number of the C core (RD_MODEL_* of core/rd.h). */
export const modelIndex: Record<Model, number> = { "gray-scott": 0 };

/** Order of each model's parameters in the C parameter vector. */
export const parameterOrder = {
  "gray-scott": ["feed", "kill", "da", "db", "dt"],
} as const satisfies Record<Model, readonly string[]>;

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

/** The parameter vector the C core takes: each value in the model's order
 * rounded to Q15, padded with zeros to RD_PARAM_MAX entries. */
export function parameterVector(params: Parameters): number[] {
  const values = params as unknown as Record<string, number>;
  const vector: number[] = parameterOrder[params.model].map((key) =>
    Math.round(values[key] * 32768)
  );
  while (vector.length < RD_PARAM_MAX) vector.push(0);
  return vector;
}
