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

/** Largest ru of the core (RD_FHN_RU_MAX of core/rd.h), 0.25 in Q15. */
export const RD_FHN_RU_MAX = 8192;

/** The model of a core model number, or null. */
export const modelOfIndex = (index: number): Model | null =>
  (Object.keys(modelIndex) as Model[]).find(
    (model) => modelIndex[model] === index
  ) ?? null;

/** The range of each entry of a model's Q15 parameter vector, as the core
 * checks it: k and rest are signed, ru is at most RD_FHN_RU_MAX, and init
 * is 0 or 1. */
export const parameterRanges = (model: Model): [number, number][] =>
  parameterOrder[model].map((key) =>
    key === "init"
      ? [0, 1]
      : key === "ru"
        ? [0, RD_FHN_RU_MAX]
        : key === "k" || key === "rest"
          ? [-32768, 32768]
          : [0, 32768]
  );

/** Whether a vector of RD_PARAM_MAX integers is a valid parameter vector
 * of the model: each entry of the model within its range, and zeros after
 * them. */
export function vectorValid(model: Model, vector: readonly number[]) {
  if (vector.length !== RD_PARAM_MAX) return false;
  const ranges = parameterRanges(model);
  return vector.every(
    (value, i) =>
      Number.isInteger(value) &&
      (i < ranges.length
        ? value >= ranges[i][0] && value <= ranges[i][1]
        : value === 0)
  );
}

/** The parameters of a model from its Q15 vector, the inverse of
 * parameterVector. */
export function parametersFromVector(
  model: Model,
  vector: readonly number[]
): Parameters {
  const params: Record<string, number> = {};
  parameterOrder[model].forEach((key, i) => {
    params[key] = INTEGER_PARAMETERS.has(key) ? vector[i] : vector[i] / 32768;
  });
  return { model, ...params } as unknown as Parameters;
}

/** The resting point of FitzHugh–Nagumo, the u of the uniform equilibrium:
 * the root of u − u³ − u / av + k = 0, where v = u / av. For 0 < av ≤ 1 the
 * left side decreases in u, so the root is unique, and bisection over
 * [−2, 2], the stored range, finds it. With av = 0 the v equation forces
 * u = 0. rest is the initial field, the value held under the digits, and
 * the target of the pull toward them, so it is always this root rather
 * than a free parameter. */
export function restingPoint(k: number, av: number) {
  if (av <= 0) return 0;
  const f = (u: number) => u - u * u * u - u / av + k;
  let low = -2,
    high = 2;
  if (f(low) <= 0) return low;
  if (f(high) >= 0) return high;
  for (let i = 0; i < 60; i++) {
    const middle = (low + high) / 2;
    const value = f(middle);
    if (value === 0) return middle;
    if (value > 0) low = middle;
    else high = middle;
  }
  return (low + high) / 2;
}

/** FitzHugh–Nagumo parameters with rest at the resting point of their k
 * and av, and any other parameters as they are. */
export const withRestingPoint = (params: Parameters): Parameters =>
  params.model === "fhn"
    ? { ...params, rest: restingPoint(params.k, params.av) }
    : params;

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
    id: "holes",
    name: "穴",
    model: "gray-scott",
    feed: 0.039,
    kill: 0.058,
    da: 1,
    db: 0.5,
    dt: 1,
  },
  {
    id: "cells",
    name: "区画",
    model: "gray-scott",
    feed: 0.062,
    kill: 0.0609,
    da: 1,
    db: 0.5,
    dt: 1,
  },
  {
    id: "worms",
    name: "ワーム",
    model: "gray-scott",
    feed: 0.046,
    kill: 0.065,
    da: 1,
    db: 0.5,
    dt: 1,
  },
  {
    id: "moving-spots",
    name: "動く斑点",
    model: "gray-scott",
    feed: 0.022,
    kill: 0.059,
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
    ru: 0.015,
    rv: 0.0375,
    av: 0.6,
    k: 0,
    dt: 1,
    rest: restingPoint(0, 0.6),
    init: 0,
  },
  {
    id: "fhn-hex",
    name: "六方斑点",
    model: "fhn",
    du: 0.04,
    dv: 1,
    ru: 0.02625,
    rv: 0.065625,
    av: 0.6,
    k: -0.22,
    dt: 1,
    rest: restingPoint(-0.22, 0.6),
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
    rest: restingPoint(-0.3, 1),
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
      : // + 0 turns a rounded -0 into 0.
        Math.round(values[key] * 32768) + 0
  );
  while (vector.length < RD_PARAM_MAX) vector.push(0);
  return vector;
}
