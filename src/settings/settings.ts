/** The watch settings the phone sends, as the settings page edits them:
 * integers under the names of the message keys of pebble/package.json, in
 * the order of the SETTING_* fields of pebble/src/c/settings.h. */
import { CLOCK_FONTS } from "../../lib/clock-fonts.ts";
import { PALETTE_COUNT, type Rgb } from "../../lib/palettes.ts";
import { presets, type Parameters } from "../../lib/simulation.ts";

export const SETTING_KEYS = [
  "feed",
  "kill",
  "da",
  "db",
  "dt",
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
] as const;
export type SettingKey = (typeof SETTING_KEYS)[number];
/** Q15 coefficients, the palette index, the dark and light custom stops as
 * 0xRRGGBB, the number of custom stops (2 to 4) and the middle stops, the
 * clock font index, and avoidance, the clock, and the date as 0 or 1. */
export type WatchSettings = Record<SettingKey, number>;

const Q15 = 32768;

/** Every setting is an integer from its minimum, 0 except for the number
 * of stops, to its maximum. */
export const settingMin = (key: SettingKey) => (key === "stops" ? 2 : 0);

export const SETTING_MAX: WatchSettings = {
  feed: Q15,
  kill: Q15,
  da: Q15,
  db: Q15,
  dt: Q15,
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
};

/** The RD_DEFAULT_* values of pebble/src/c/config.h. */
export const DEFAULT_SETTINGS: WatchSettings = {
  feed: 950,
  kill: 1868,
  da: 22938,
  db: 11469,
  dt: 32768,
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
};

export function validate(value: unknown): WatchSettings | null {
  if (!value || typeof value !== "object") return null;
  const settings = {} as WatchSettings;
  for (const key of SETTING_KEYS) {
    const v = (value as Record<string, unknown>)[key];
    if (
      typeof v !== "number" ||
      !Number.isInteger(v) ||
      v < settingMin(key) ||
      v > SETTING_MAX[key]
    )
      return null;
    settings[key] = v;
  }
  return settings;
}

export function fromQuery(query: URLSearchParams) {
  const value: Record<string, number> = {};
  for (const key of SETTING_KEYS) {
    const text = query.get(key);
    if (text === null || !/^\d+$/.test(text)) return null;
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

/** The settings file: a named, versioned wrapper around the settings. */
export const SETTINGS_FILE_FORMAT = "turing-pattern-watchface-settings";
export const SETTINGS_FILE_NAME = "turing-settings.json";

export const toFileJson = (settings: WatchSettings) =>
  JSON.stringify(
    {
      format: SETTINGS_FILE_FORMAT,
      version: 1,
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
  return file.format === SETTINGS_FILE_FORMAT && file.version === 1
    ? validate(file.settings)
    : null;
}

export const q15 = (value: number) => Math.round(value * Q15);

/** Stripe widths, thick, medium, and thin, named by Strings.widths: Da and
 * Db in the ratio 2:1. Smaller rates draw thinner stripes and spread more
 * slowly. At thin the presets cover about as much of the face within the
 * watch's 30 s startup as at thick, and below about 0.6 the coral preset
 * leaves empty areas. */
export const DIFFUSION_PRESETS = [
  { da: 1, db: 0.5 },
  { da: 0.8, db: 0.4 },
  { da: 0.7, db: 0.35 },
];

/** The index of the stripe width with the settings' Da and Db, or -1. */
export const diffusionPresetOf = (settings: WatchSettings) =>
  DIFFUSION_PRESETS.findIndex(
    (p) => q15(p.da) === settings.da && q15(p.db) === settings.db
  );

/** The index of the preset with the settings' feed and kill, or -1. */
export const presetOf = (settings: WatchSettings) =>
  presets.findIndex(
    (p) => q15(p.feed) === settings.feed && q15(p.kill) === settings.kill
  );

export const toParameters = (settings: WatchSettings): Parameters => ({
  feed: settings.feed / Q15,
  kill: settings.kill / Q15,
  da: settings.da / Q15,
  db: settings.db / Q15,
  dt: settings.dt / Q15,
});

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
