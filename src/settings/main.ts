/** The watch settings page: edits the settings with a live preview and
 * returns them through pebblejs://close, or through the return_to address
 * of `pebble emu-app-config`. */
import "./style.css";
import fontsUrl from "../../public/fonts/clock-fonts.json?url";
import wasmUrl from "../../public/wasm/rd.wasm?url";
import { CLOCK_FONTS } from "../../lib/clock-fonts.ts";
import {
  MONO,
  PALETTE_CUSTOM,
  PALETTES,
  paletteSwatch,
  type Rgb,
} from "../../lib/palettes.ts";
import { presets, type Parameters } from "../../lib/simulation.ts";
import { parameterBounds } from "../core/modes.ts";
import { PREVIEW_STEPS, PreviewView } from "./preview.ts";
import {
  customStops,
  initialSettings,
  PEBBLE_COLORS,
  presetOf,
  q15,
  toJson,
  valueOf,
  type WatchSettings,
} from "./settings.ts";

const element = <T extends HTMLElement>(id: string) =>
  document.getElementById(id) as T;

const settings: WatchSettings = initialSettings(
  element("settings").textContent,
  location.search
);
const returnTo = new URLSearchParams(location.search).get("return_to");

const css = (rgb: Rgb) => `rgb(${rgb.join(",")})`;

function swatch(colors: readonly Rgb[]) {
  const bar = document.createElement("span");
  bar.className = "swatch";
  for (const color of colors) {
    const cell = document.createElement("span");
    cell.style.background = css(color);
    bar.append(cell);
  }
  return bar;
}

/** A group of radio chips. */
function chips(
  container: HTMLElement,
  name: string,
  labels: string[],
  onChange: (index: number) => void
) {
  const inputs = labels.map((label, index) => {
    const wrap = document.createElement("label");
    wrap.className = "chip";
    const input = document.createElement("input");
    input.type = "radio";
    input.name = name;
    input.addEventListener("change", () => onChange(index));
    const text = document.createElement("span");
    text.textContent = label;
    wrap.append(input, text);
    container.append(wrap);
    return input;
  });
  return (index: number) =>
    inputs.forEach((input, i) => (input.checked = i === index));
}

/** A labeled slider for a Q15 coefficient. */
function slider(
  container: HTMLElement,
  key: keyof Parameters,
  label: string,
  digits: number,
  onInput: () => void
) {
  const [min, max, step] = parameterBounds[key];
  const row = document.createElement("label");
  row.className = "slider";
  const name = document.createElement("span");
  name.textContent = label;
  const input = document.createElement("input");
  input.type = "range";
  input.min = String(min);
  input.max = String(max);
  input.step = String(step);
  const output = document.createElement("output");
  row.append(name, input, output);
  container.append(row);
  input.addEventListener("input", () => {
    settings[key] = q15(Number(input.value));
    onInput();
  });
  return () => {
    const value = settings[key] / 32768;
    input.value = String(value);
    output.textContent = value.toFixed(digits);
  };
}

/** An 8 × 8 grid of the watch's 64 colors for one custom stop. */
function colorGrid(
  container: HTMLElement,
  label: string,
  key: "low" | "high",
  onPick: () => void
) {
  const title = document.createElement("span");
  title.textContent = label;
  const grid = document.createElement("div");
  grid.className = "colors";
  grid.setAttribute("role", "group");
  grid.setAttribute("aria-label", label);
  const buttons = PEBBLE_COLORS.map((color) => {
    const button = document.createElement("button");
    button.type = "button";
    button.style.background = css(color);
    button.setAttribute(
      "aria-label",
      `#${valueOf(color).toString(16).padStart(6, "0")}`
    );
    button.addEventListener("click", () => {
      settings[key] = valueOf(color);
      onPick();
    });
    grid.append(button);
    return button;
  });
  container.append(title, grid);
  return () =>
    PEBBLE_COLORS.forEach((color, i) =>
      buttons[i].setAttribute(
        "aria-pressed",
        String(valueOf(color) === settings[key])
      )
    );
}

const progress = element<HTMLParagraphElement>("progress");
let view: PreviewView | null = null;
try {
  view = new PreviewView(element("preview"), (steps) => {
    progress.textContent = `ステップ ${steps} / ${PREVIEW_STEPS}`;
  });
} catch {
  progress.textContent = "この環境ではプレビューを表示できません";
}

let rerun = 0;
/** A change of coefficients, font, or clock runs the preview again after a
 * short pause, a palette change only recolors it. */
function changed(kind: "field" | "palette") {
  update();
  if (kind === "palette") {
    view?.recolor({ ...settings });
    return;
  }
  clearTimeout(rerun);
  rerun = window.setTimeout(() => view?.run({ ...settings }), 300);
}

const setPreset = chips(
  element("presets"),
  "preset",
  [...presets.map((p) => p.name), "カスタム"],
  (index) => {
    if (index < presets.length) {
      settings.feed = q15(presets[index].feed);
      settings.kill = q15(presets[index].kill);
    }
    changed("field");
  }
);
const patternSliders = element("pattern-sliders");
const diffusionSliders = element("diffusion-sliders");
const sliders = [
  slider(patternSliders, "feed", "Feed", 4, () => changed("field")),
  slider(patternSliders, "kill", "Kill", 4, () => changed("field")),
  slider(diffusionSliders, "da", "Da", 2, () => changed("field")),
  slider(diffusionSliders, "db", "Db", 2, () => changed("field")),
  slider(diffusionSliders, "dt", "Dt", 2, () => changed("field")),
];

const paletteInputs: HTMLInputElement[] = [];
let customSwatch: HTMLElement | null = null;
const paletteList = element("palettes");
[...PALETTES, null].forEach((palette, index) => {
  const wrap = document.createElement("label");
  wrap.className = "palette";
  const input = document.createElement("input");
  input.type = "radio";
  input.name = "palette";
  input.addEventListener("change", () => {
    settings.palette = index;
    changed("palette");
  });
  const body = document.createElement("span");
  body.className = "palette-body";
  const name = document.createElement("span");
  name.textContent = palette ? palette.name : "カスタム";
  const bar = swatch(
    !palette
      ? paletteSwatch(customStops(settings))
      : index === MONO
        ? [
            [0, 0, 0],
            [255, 255, 255],
          ]
        : paletteSwatch(palette.stops)
  );
  if (!palette) customSwatch = bar;
  body.append(bar, name);
  wrap.append(input, body);
  paletteList.append(wrap);
  paletteInputs.push(input);
});
const customPalette = element("custom-palette");
const grids = [
  colorGrid(element("custom-low"), "暗い側", "low", () => changed("palette")),
  colorGrid(element("custom-high"), "明るい側", "high", () =>
    changed("palette")
  ),
];

const setFont = chips(
  element("fonts"),
  "font",
  CLOCK_FONTS.map((font) => (font === "leco" ? "LECO" : "Bitham")),
  (index) => {
    settings.font = index;
    changed("field");
  }
);
const clock = element<HTMLInputElement>("clock");
const avoid = element<HTMLInputElement>("avoid");
clock.addEventListener("change", () => {
  settings.clock = Number(clock.checked);
  changed("field");
});
avoid.addEventListener("change", () => {
  settings.avoid = Number(avoid.checked);
  changed("field");
});

/** Show the settings in every control. */
function update() {
  const preset = presetOf(settings);
  setPreset(preset < 0 ? presets.length : preset);
  sliders.forEach((show) => show());
  paletteInputs.forEach(
    (input, index) => (input.checked = index === settings.palette)
  );
  customPalette.hidden = settings.palette !== PALETTE_CUSTOM;
  if (customSwatch)
    customSwatch.replaceChildren(
      ...swatch(paletteSwatch(customStops(settings))).children
    );
  grids.forEach((show) => show());
  setFont(settings.font);
  clock.checked = settings.clock === 1;
  avoid.checked = settings.avoid === 1;
  avoid.disabled = !clock.checked;
}

element("save").addEventListener("click", () => {
  location.href =
    (returnTo ?? "pebblejs://close#") + encodeURIComponent(toJson(settings));
});
element("cancel").addEventListener("click", () => {
  location.href = returnTo ?? "pebblejs://close";
});

update();
view
  ?.load(wasmUrl, fontsUrl)
  .then(() => view?.run({ ...settings }))
  .catch(() => {
    progress.textContent = "プレビューを読み込めませんでした";
  });
