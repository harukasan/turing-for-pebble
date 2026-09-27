/** The watch settings the phone sends, as the settings page edits them:
 * integers under the names of the message keys of pebble/package.json, in
 * the order of the SETTING_* fields of pebble/src/c/settings.h. */
import { CLOCK_FONTS } from "../../lib/clock-fonts.ts";
import { PALETTE_COUNT, type Rgb } from "../../lib/palettes.ts";
import {
  presets,
  type Model,
  type Parameters,
  type Preset,
} from "../../lib/simulation.ts";
import {
  RD_PARAM_MAX,
  defaultParametersFor,
  modelIndex,
  modelOfIndex,
  parameterOrder,
  parameterVector,
  parametersFromVector,
  presetParameters,
  vectorValid,
} from "../../lib/presets.ts";

/** The entries of the parameter vector, p0 to p9. */
export const PARAM_KEYS = Array.from(
  { length: RD_PARAM_MAX },
  (_, i) => `p${i}` as const
) as unknown as readonly [
  "p0",
  "p1",
  "p2",
  "p3",
  "p4",
  "p5",
  "p6",
  "p7",
  "p8",
  "p9",
];
/** The settings that every model shares, with a fixed range each. */
export const FIXED_KEYS = [
  "palette",
  "low",
  "high",
  "stops",
  "mid1",
  "mid2",
  "font",
  "avoid",
  "clock",
  "date",
  "face",
] as const;
export const SETTING_KEYS = ["model", ...PARAM_KEYS, ...FIXED_KEYS] as const;
export type SettingKey = (typeof SETTING_KEYS)[number];
export type FixedKey = (typeof FIXED_KEYS)[number];
export type ParamKey = (typeof PARAM_KEYS)[number];
/** The model number of the core, its Q15 parameter vector (init as an
 * integer, unused entries 0), the palette index, the dark and light custom
 * stops as 0xRRGGBB, the number of custom stops (2 to 4) and the middle
 * stops, the clock font index, avoidance, the clock, and the date as 0 or
 * 1, and the clock face, 0 digital or 1 analog. */
export type WatchSettings = Record<SettingKey, number>;

const Q15 = 32768;

/** Every fixed setting is an integer from its minimum, 0 except for the
 * number of stops, to its maximum. The model and the vector are checked
 * by vectorValid. */
export const settingMin = (key: FixedKey) => (key === "stops" ? 2 : 0);

export const SETTING_MAX: Record<FixedKey, number> = {
  palette: PALETTE_COUNT - 1,
  low: 0xffffff,
  high: 0xffffff,
  stops: 4,
  mid1: 0xffffff,
  mid2: 0xffffff,
  font: CLOCK_FONTS.length - 1,
  avoid: 1,
  clock: 1,
  date: 1,
  face: 1,
};

/** The RD_DEFAULT_* values of pebble/src/c/config.h. */
export const DEFAULT_SETTINGS: WatchSettings = {
  model: 0,
  p0: 950,
  p1: 1868,
  p2: 22938,
  p3: 11469,
  p4: 32768,
  p5: 0,
  p6: 0,
  p7: 0,
  p8: 0,
  p9: 0,
  palette: 0,
  low: 0x001e12,
  high: 0xd2ff55,
  stops: 2,
  mid1: 0x466928,
  mid2: 0x8cb43f,
  font: 0,
  avoid: 1,
  clock: 1,
  date: 1,
  face: 0,
};

/** The model of the settings, or null for an unknown model number. */
export const modelOf = (settings: WatchSettings) =>
  modelOfIndex(settings.model);

/** The Q15 parameter vector of the settings. */
export const vectorOf = (settings: WatchSettings) =>
  PARAM_KEYS.map((key) => settings[key]);

/** Settings with a model and its parameter vector. */
export function withVector(
  settings: WatchSettings,
  model: Model,
  vector: readonly number[]
): WatchSettings {
  const next = { ...settings, model: modelIndex[model] };
  PARAM_KEYS.forEach((key, i) => (next[key] = vector[i] ?? 0));
  return next;
}

/** Settings stored or exported before version 4 carried the five
 * Gray-Scott coefficients under their own names. */
function upgrade(value: Record<string, unknown>): Record<string, unknown> {
  if ("model" in value || !("feed" in value)) return value;
  const { feed, kill, da, db, dt, ...rest } = value;
  return {
    ...rest,
    model: 0,
    p0: feed,
    p1: kill,
    p2: da,
    p3: db,
    p4: dt,
    p5: 0,
    p6: 0,
    p7: 0,
    p8: 0,
    p9: 0,
  };
}

export function validate(value: unknown): WatchSettings | null {
  if (!value || typeof value !== "object") return null;
  const raw = upgrade(value as Record<string, unknown>);
  const settings = {} as WatchSettings;
  for (const key of SETTING_KEYS) {
    const v = raw[key];
    if (typeof v !== "number" || !Number.isInteger(v)) return null;
    settings[key] = v;
  }
  const model = modelOf(settings);
  if (!model || !vectorValid(model, vectorOf(settings))) return null;
  for (const key of FIXED_KEYS)
    if (settings[key] < settingMin(key) || settings[key] > SETTING_MAX[key])
      return null;
  return settings;
}

/** The names of the Gray-Scott coefficients before the model, which a
 * hosted page's query string may still carry. */
const LEGACY_KEYS = ["feed", "kill", "da", "db", "dt"];

export function fromQuery(query: URLSearchParams) {
  const value: Record<string, number> = {};
  for (const key of [...SETTING_KEYS, ...LEGACY_KEYS]) {
    const text = query.get(key);
    if (text === null) continue;
    if (!/^-?\d+$/.test(text)) return null;
    value[key] = Number(text);
  }
  return validate(value);
}

export function fromEmbedded(text: string) {
  try {
    return validate(JSON.parse(text));
  } catch {
    return null;
  }
}

/** The settings the phone embedded in the page, else those of the query
 * string of a hosted page, else the defaults. The page's placeholder is not
 * JSON, so a page without embedded settings falls through. The page keeps
 * the placeholder text in its settings element only, where the phone
 * replaces the first occurrence. */
export function initialSettings(embedded: string | null, search: string) {
  return (
    (embedded ? fromEmbedded(embedded) : null) ??
    fromQuery(new URLSearchParams(search)) ?? { ...DEFAULT_SETTINGS }
  );
}

export const toQuery = (settings: WatchSettings) =>
  SETTING_KEYS.map((key) => `${key}=${settings[key]}`).join("&");

export const toJson = (settings: WatchSettings) =>
  JSON.stringify(
    Object.fromEntries(SETTING_KEYS.map((key) => [key, settings[key]]))
  );

/** The settings file: a named, versioned wrapper around the settings.
 * Version 1 carried the Gray-Scott coefficients by name, version 2 the
 * model and its vector. */
export const SETTINGS_FILE_FORMAT = "turing-pattern-watchface-settings";
export const SETTINGS_FILE_VERSION = 2;
export const SETTINGS_FILE_NAME = "turing-settings.json";

export const toFileJson = (settings: WatchSettings) =>
  JSON.stringify(
    {
      format: SETTINGS_FILE_FORMAT,
      version: SETTINGS_FILE_VERSION,
      settings: JSON.parse(toJson(settings)),
    },
    null,
    2
  ) + "\n";

/** The settings of a settings file, or of the bare JSON the page returns,
 * or null. */
export function fromFileJson(text: string) {
  let value: unknown;
  try {
    value = JSON.parse(text);
  } catch {
    return null;
  }
  if (!value || typeof value !== "object") return null;
  const file = value as Record<string, unknown>;
  if (!("format" in file)) return validate(value);
  return file.format === SETTINGS_FILE_FORMAT &&
    (file.version === 1 || file.version === SETTINGS_FILE_VERSION)
    ? validate(file.settings)
    : null;
}

export const q15 = (value: number) => Math.round(value * Q15);

/** The setting key of a parameter of a model, p0 to p9. */
export const paramKey = (model: Model, key: string): ParamKey =>
  PARAM_KEYS[(parameterOrder[model] as readonly string[]).indexOf(key)];

/** Stripe widths, thick, medium, and thin, named by Strings.widths: Da and
 * Db in the ratio 2:1. Smaller rates draw thinner stripes and spread more
 * slowly. At thin the presets cover about as much of the face within the
 * watch's 30 s startup as at thick, and below about 0.6 the coral preset
 * leaves empty areas. Gray-Scott only. */
export const DIFFUSION_PRESETS = [
  { da: 1, db: 0.5 },
  { da: 0.8, db: 0.4 },
  { da: 0.7, db: 0.35 },
];

/** The index of the stripe width with the settings' Da and Db, or -1, also
 * for another model. */
export const diffusionPresetOf = (settings: WatchSettings) =>
  modelOf(settings) === "gray-scott"
    ? DIFFUSION_PRESETS.findIndex(
        (p) => q15(p.da) === settings.p2 && q15(p.db) === settings.p3
      )
    : -1;

/** The entries of the vector a preset sets: Gray-Scott presets set feed
 * and kill and leave the diffusion and the time step, FitzHugh-Nagumo
 * presets set every parameter. */
const PRESET_ENTRIES: Record<Model, number> = { "gray-scott": 2, fhn: 9 };

/** The presets of a model, in the order of lib/presets.ts. */
export const presetsOf = (model: Model) =>
  presets.filter((preset) => preset.model === model);

/** The index in lib/presets.ts of the preset whose model and entries match
 * the settings, or -1. */
export function presetOf(settings: WatchSettings) {
  const model = modelOf(settings);
  if (!model) return -1;
  const vector = vectorOf(settings);
  return presets.findIndex(
    (preset) =>
      preset.model === model &&
      parameterVector(presetParameters(preset))
        .slice(0, PRESET_ENTRIES[model])
        .every((value, i) => value === vector[i])
  );
}

/** Settings with a preset's model and the entries it sets. */
export function withPreset(
  settings: WatchSettings,
  preset: Preset
): WatchSettings {
  const vector = parameterVector(presetParameters(preset));
  const kept = vectorOf(settings);
  const model = preset.model;
  return withVector(
    settings,
    model,
    vector.map((value, i) =>
      settings.model === modelIndex[model] && i >= PRESET_ENTRIES[model]
        ? kept[i]
        : value
    )
  );
}

/** Settings with another model: Gray-Scott with the watch's defaults, the
 * maze preset with thin stripes, and another model with its first preset. */
export const withModel = (settings: WatchSettings, model: Model) =>
  withVector(
    settings,
    model,
    model === modelOf(DEFAULT_SETTINGS)
      ? vectorOf(DEFAULT_SETTINGS)
      : parameterVector(defaultParametersFor(model))
  );

export const toParameters = (settings: WatchSettings): Parameters =>
  parametersFromVector(modelOf(settings) ?? "gray-scott", vectorOf(settings));

export const rgbOf = (value: number): Rgb => [
  (value >> 16) & 255,
  (value >> 8) & 255,
  value & 255,
];

export const valueOf = (rgb: Rgb) => (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];

/** The keys of the custom stops in use, dark to light. */
export const stopKeys = (settings: WatchSettings) =>
  (
    [
      ["low", "high"],
      ["low", "mid1", "high"],
      ["low", "mid1", "mid2", "high"],
    ] as const
  )[settings.stops - 2];

/** The stops of the custom palette. */
export const customStops = (settings: WatchSettings): Rgb[] =>
  stopKeys(settings).map((key) => rgbOf(settings[key]));

/** Settings with a new number of custom stops. The middle stops already
 * in use keep their colors, and a middle stop that comes into use starts
 * halfway between its neighbors, or on the line from the dark to the light
 * stop when both middles come into use at once. */
export function withStops(
  settings: WatchSettings,
  stops: number
): WatchSettings {
  const between = (a: number, b: number, t: number) => {
    const from = rgbOf(a);
    const to = rgbOf(b);
    return valueOf(
      from.map((c, i) => Math.round(c + (to[i] - c) * t)) as unknown as Rgb
    );
  };
  const { low, high, mid1 } = settings;
  if (stops <= settings.stops) return { ...settings, stops };
  if (stops === 4 && settings.stops === 2)
    return {
      ...settings,
      stops,
      mid1: between(low, high, 1 / 3),
      mid2: between(low, high, 2 / 3),
    };
  if (stops === 4)
    return { ...settings, stops, mid2: between(mid1, high, 0.5) };
  return { ...settings, stops, mid1: between(low, high, 0.5) };
}

/** The 64 colors of the watch display, two bits per channel. */
export const PEBBLE_COLORS: Rgb[] = Array.from({ length: 64 }, (_, i) => [
  ((i >> 4) & 3) * 85,
  ((i >> 2) & 3) * 85,
  (i & 3) * 85,
]);
