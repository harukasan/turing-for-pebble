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
  fromFileJson,
  SETTINGS_FILE_NAME,
  toFileJson,
  stopKeys,
  withStops,
  DIFFUSION_PRESETS,
  diffusionPresetOf,
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

/** A slider for a Q15 coefficient with its value in a number field. The
 * field takes any value from 0 to 1, the range the watch accepts, also
 * outside the slider's range, and applies it when the entry is committed. */
function slider(
  container: HTMLElement,
  key: keyof Parameters,
  label: string,
  digits: number,
  onInput: () => void
) {
  const [min, max, step] = parameterBounds[key];
  const row = document.createElement("div");
  row.className = "slider";
  const name = document.createElement("span");
  name.textContent = label;
  const input = document.createElement("input");
  input.type = "range";
  input.min = String(min);
  input.max = String(max);
  input.step = String(step);
  input.setAttribute("aria-label", label);
  const field = document.createElement("input");
  field.type = "number";
  field.inputMode = "decimal";
  field.min = "0";
  field.max = "1";
  field.step = "any";
  field.setAttribute("aria-label", `${label}の値`);
  row.append(name, input, field);
  container.append(row);
  const show = () => {
    const value = settings[key] / 32768;
    input.value = String(value);
    if (document.activeElement !== field) field.value = value.toFixed(digits);
  };
  input.addEventListener("input", () => {
    settings[key] = q15(Number(input.value));
    onInput();
  });
  field.addEventListener("focus", () => field.select());
  field.addEventListener("keydown", (event) => {
    if (event.key === "Enter") field.blur();
  });
  field.addEventListener("change", () => {
    const value = Number(field.value);
    if (field.value.trim() !== "" && Number.isFinite(value)) {
      settings[key] = q15(Math.min(1, Math.max(0, value)));
      onInput();
    }
    field.blur();
    show();
  });
  field.addEventListener("blur", show);
  return show;
}

const STOPS = [
  { key: "low", label: "暗い色" },
  { key: "mid1", label: "中間 1" },
  { key: "mid2", label: "中間 2" },
  { key: "high", label: "明るい色" },
] as const;
type StopKey = (typeof STOPS)[number]["key"];

const hex = (value: number) => `#${value.toString(16).padStart(6, "0")}`;

/** The stops of the custom palette in use, in one row from dark to light,
 * as buttons showing their colors and joined by lines that blend from one
 * color to the next. The ends carry 暗 and 明 on their outer sides, and the
 * middle stops show their color only. A button
 * opens the watch's 64 colors for its stop, and picking a color closes
 * them. */
function customPicker(
  stops: HTMLElement,
  picker: HTMLElement,
  onPick: () => void
) {
  let picking: StopKey | null = null;
  const buttons = STOPS.map(({ key, label }) => {
    const button = document.createElement("button");
    button.type = "button";
    const middle = key === "mid1" || key === "mid2";
    button.className = middle ? "stop stop-middle" : "stop";
    button.setAttribute("aria-label", label);
    const chip = document.createElement("span");
    chip.className = "stop-color";
    button.append(chip);
    // The ends are named by one character on their outer side.
    if (key === "low") button.prepend("暗");
    if (key === "high") button.append("明");
    button.addEventListener("click", () => {
      picking = picking === key ? null : key;
      show();
    });
    return { key, button, chip };
  });
  const colors = PEBBLE_COLORS.map((color) => {
    const button = document.createElement("button");
    button.type = "button";
    button.style.background = css(color);
    button.setAttribute("aria-label", hex(valueOf(color)));
    button.addEventListener("click", () => {
      if (!picking) return;
      settings[picking] = valueOf(color);
      picking = null;
      onPick();
    });
    picker.append(button);
    return button;
  });
  function show() {
    const used: readonly StopKey[] = stopKeys(settings);
    if (picking && !used.includes(picking)) picking = null;
    const row: HTMLElement[] = [];
    used.forEach((key, i) => {
      const { button, chip } = buttons.find((b) => b.key === key)!;
      chip.style.background = hex(settings[key]);
      button.setAttribute("aria-expanded", String(picking === key));
      if (i > 0) {
        const link = document.createElement("span");
        link.className = "stop-link";
        link.style.background = `linear-gradient(to right, ${hex(settings[used[i - 1]])}, ${hex(settings[key])})`;
        row.push(link);
      }
      row.push(button);
    });
    stops.replaceChildren(...row);
    picker.hidden = picking === null;
    const label = STOPS.find((stop) => stop.key === picking)?.label ?? "";
    picker.setAttribute("aria-label", label);
    PEBBLE_COLORS.forEach((color, i) =>
      colors[i].setAttribute(
        "aria-pressed",
        String(picking !== null && valueOf(color) === settings[picking])
      )
    );
  }
  return show;
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
const setDiffusion = chips(
  element("diffusion-presets"),
  "diffusion",
  [...DIFFUSION_PRESETS.map((p) => p.name), "カスタム"],
  (index) => {
    if (index < DIFFUSION_PRESETS.length) {
      settings.da = q15(DIFFUSION_PRESETS[index].da);
      settings.db = q15(DIFFUSION_PRESETS[index].db);
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
const setStops = chips(
  element("custom-count"),
  "stops",
  ["2 色", "3 色", "4 色"],
  (index) => {
    Object.assign(settings, withStops(settings, index + 2));
    changed("palette");
  }
);
const showCustom = customPicker(
  element("custom-stops"),
  element("color-picker"),
  () => changed("palette")
);

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
  const diffusion = diffusionPresetOf(settings);
  setDiffusion(diffusion < 0 ? DIFFUSION_PRESETS.length : diffusion);
  sliders.forEach((show) => show());
  paletteInputs.forEach(
    (input, index) => (input.checked = index === settings.palette)
  );
  customPalette.hidden = settings.palette !== PALETTE_CUSTOM;
  if (customSwatch)
    customSwatch.replaceChildren(
      ...swatch(paletteSwatch(customStops(settings))).children
    );
  setStops(settings.stops - 2);
  showCustom();
  setFont(settings.font);
  clock.checked = settings.clock === 1;
  avoid.checked = settings.avoid === 1;
  avoid.disabled = !clock.checked;
}

const fileStatus = element("file-status");
const fileText = element<HTMLTextAreaElement>("file-text");
/** The page runs as the data URL the phone opens in the Pebble app, whose
 * web view neither saves downloads nor offers a share sheet, and shows a
 * downloaded file in place of the page. */
const inApp = location.protocol === "data:";

/** Show the settings file's text and copy it by selecting it, which works
 * in web views without the Clipboard API. Runs within the click. */
function showAndCopy() {
  fileText.value = toFileJson(settings);
  fileText.focus();
  fileText.setSelectionRange(0, fileText.value.length);
  let copied = false;
  try {
    copied = document.execCommand("copy");
  } catch {
    copied = false;
  }
  fileText.blur();
  return copied;
}

/** Fill the page from a settings file's text. `source` names the text,
 * for example a file name, in the messages. */
function load(text: string, source: string) {
  const loaded = fromFileJson(text);
  if (!loaded) {
    fileStatus.textContent = `${source}は設定として読み込めませんでした。`;
    return;
  }
  Object.assign(settings, loaded);
  changed("field");
  fileStatus.textContent = `${source}から設定を読み込みました。保存を押すと watch に送ります。`;
}

/** The settings file through the share sheet where the web view offers one,
 * as text to copy in the Pebble app, and as a download elsewhere. */
element("export").addEventListener("click", async () => {
  const file = new File([toFileJson(settings)], SETTINGS_FILE_NAME, {
    type: "application/json",
  });
  if (navigator.canShare?.({ files: [file] })) {
    try {
      await navigator.share({ files: [file] });
      fileStatus.textContent = "書き出しました。";
    } catch (error) {
      if ((error as Error).name !== "AbortError")
        fileStatus.textContent = "書き出せませんでした。";
    }
    return;
  }
  if (inApp) {
    fileStatus.textContent = showAndCopy()
      ? "Pebble アプリではファイルを保存できないため、設定をコピーしました。メモなどに貼り付けて残せます。"
      : "Pebble アプリではファイルを保存できません。上の文字列をコピーして残してください。";
    return;
  }
  const url = URL.createObjectURL(file);
  const link = document.createElement("a");
  link.href = url;
  link.download = SETTINGS_FILE_NAME;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
  fileStatus.textContent = `${SETTINGS_FILE_NAME} を書き出しました。`;
});

element("copy").addEventListener("click", () => {
  fileStatus.textContent = showAndCopy()
    ? "設定をコピーしました。"
    : "コピーできませんでした。上の文字列を選んでコピーしてください。";
});

const importInput = element<HTMLInputElement>("import");
importInput.addEventListener("change", async () => {
  const file = importInput.files?.[0];
  importInput.value = "";
  if (file) load(await file.text(), `${file.name} `);
});

element("import-text").addEventListener("click", () => {
  if (fileText.value.trim()) load(fileText.value, "貼り付けた文字列");
  else fileStatus.textContent = "設定の JSON を貼り付けてください。";
});

element("save").addEventListener("click", () => {
  location.href =
    (returnTo ?? "pebblejs://close#") + encodeURIComponent(toJson(settings));
});
element("cancel").addEventListener("click", () => {
  location.href = returnTo ?? "pebblejs://close";
});

/** The page scrolls inside .scroll, sized to the visible area. When the
 * keyboard opens, the Pebble app shrinks its web view and iOS scrolls the
 * fixed document to the focused field, and neither always comes back when
 * the keyboard closes. So .scroll follows the visual viewport's height, and
 * the document returns to the top once no field has the focus. */
function fitViewport() {
  const height = window.visualViewport?.height ?? window.innerHeight;
  document.documentElement.style.setProperty(
    "--viewport-height",
    `${Math.round(height)}px`
  );
  const active = document.activeElement;
  if (!active || active === document.body) window.scrollTo(0, 0);
}
window.visualViewport?.addEventListener("resize", fitViewport);
window.addEventListener("resize", fitViewport);
document.addEventListener("focusout", () => setTimeout(fitViewport, 100));
fitViewport();

update();
view
  ?.load(wasmUrl, fontsUrl)
  .then(() => view?.run({ ...settings }))
  .catch(() => {
    progress.textContent = "プレビューを読み込めませんでした";
  });
