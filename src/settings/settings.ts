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
  "font",
  "avoid",
  "clock",
] as const;
export type SettingKey = (typeof SETTING_KEYS)[number];
/** Q15 coefficients, the palette index, the custom stops as 0xRRGGBB, the
 * clock font index, and avoidance and the clock as 0 or 1. */
export type WatchSettings = Record<SettingKey, number>;

const Q15 = 32768;

/** Every setting is an integer from 0 to its maximum. */
export const SETTING_MAX: WatchSettings = {
  feed: Q15,
  kill: Q15,
  da: Q15,
  db: Q15,
  dt: Q15,
  palette: PALETTE_COUNT - 1,
  low: 0xffffff,
  high: 0xffffff,
  font: CLOCK_FONTS.length - 1,
  avoid: 1,
  clock: 1,
};

/** The RD_DEFAULT_* values of pebble/src/c/config.h. */
export const DEFAULT_SETTINGS: WatchSettings = {
  feed: 950,
  kill: 1868,
  da: 32768,
  db: 16384,
  dt: 32768,
  palette: 0,
  low: 0x001e12,
  high: 0xd2ff55,
  font: 0,
  avoid: 1,
  clock: 1,
};

export function validate(value: unknown): WatchSettings | null {
  if (!value || typeof value !== "object") return null;
  const settings = {} as WatchSettings;
  for (const key of SETTING_KEYS) {
    const v = (value as Record<string, unknown>)[key];
    if (
      typeof v !== "number" ||
      !Number.isInteger(v) ||
      v < 0 ||
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

export const q15 = (value: number) => Math.round(value * Q15);

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

/** The stops of the custom palette. */
export const customStops = (settings: WatchSettings): Rgb[] => [
  rgbOf(settings.low),
  rgbOf(settings.high),
];

/** The 64 colors of the watch display, two bits per channel. */
export const PEBBLE_COLORS: Rgb[] = Array.from({ length: 64 }, (_, i) => [
  ((i >> 4) & 3) * 85,
  ((i >> 2) & 3) * 85,
  (i & 3) * 85,
]);
