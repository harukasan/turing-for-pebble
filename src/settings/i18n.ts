/** The settings page's text in English and Japanese. The page starts in
 * the language the phone stored from an earlier save, else Japanese on a
 * Japanese phone and English elsewhere, and a switch on the page changes
 * it. */
import type { PaletteId } from "../../lib/palettes.ts";

export const LANGS = ["en", "ja"] as const;
export type Lang = (typeof LANGS)[number];

export const langOf = (value: unknown): Lang | null =>
  LANGS.find((lang) => lang === value) ?? null;

/** Japanese for a phone whose first language is Japanese, else English. */
export const detectLang = (language: string | undefined): Lang =>
  language?.toLowerCase().startsWith("ja") ? "ja" : "en";

/** The label of the avoidance switch for the analog or digital face, with
 * or without the date. */
type AvoidLabel = (analog: boolean, date: boolean) => string;

const en = {
  name: "English",
  title: "Turing Settings",
  language: "Language",
  previewLabel: "Watchface preview",
  loading: "Loading the preview",
  unavailable: "The preview is not available here",
  loadFailed: "Could not load the preview",
  caption: (speed: number) => `Preview (${speed}× speed)`,
  pattern: "Pattern",
  diffusion: "Diffusion and Time Step",
  colors: "Colors",
  clock: "Clock",
  fileSection: "Export and Import Settings",
  /** The models of src/core/modes.ts, in order. */
  models: ["Gray–Scott", "FitzHugh–Nagumo"],
  /** The presets of lib/presets.ts, by id. */
  presetNames: {
    maze: "Maze",
    coral: "Coral",
    mitosis: "Mitosis",
    spots: "Spots",
    "thin-line": "Thin lines",
    "fhn-stripes": "Stripes",
    "fhn-hex": "Hex spots",
    "fhn-spiral": "Spiral",
  } as Record<string, string>,
  initial: "Initial condition",
  /** The initial conditions of FitzHugh–Nagumo, init 0 and 1. */
  inits: ["Disks", "Cut wave"],
  /** The stripe widths of DIFFUSION_PRESETS, in order. */
  widths: ["Thick", "Medium", "Thin"],
  custom: "Custom",
  palettes: { green: "Lime", blue: "Cyan", mono: "Mono" } as Partial<
    Record<PaletteId, string>
  >,
  stops: (count: number) => `${count} colors`,
  dark: "Dark",
  light: "Light",
  stopLabels: ["Dark color", "Middle 1", "Middle 2", "Light color"],
  value: (label: string) => `${label} value`,
  faceLabel: "Face",
  /** The clock faces, digital then analog. */
  faces: ["Digital", "Analog"],
  fontLabel: "Font",
  showClock: "Show the clock",
  showDate: "Show the date",
  /** The avoidance switch. Japanese names what the face shows, the digits
   * or the hands and date, and "the clock" covers both in English. */
  avoid: (() => "Keep the pattern off the clock") as AvoidLabel,
  export: "Export",
  file: "File",
  jsonLabel: "Settings JSON",
  jsonPlaceholder:
    "Exported settings appear here. You can also paste settings JSON here to import it.",
  importText: "Import text",
  cancel: "Cancel",
  save: "Save",
  pasted: "The pasted text",
  imported: "Settings imported.",
  notSettings: (source: string) => `${source} is not a settings file.`,
  pasteFirst: "Paste the settings JSON first.",
  exported: "Exported.",
  exportFailed: "Could not export.",
  exportedFile: (name: string) => `Exported ${name}.`,
  copiedInApp:
    "The Pebble app cannot save files, so the settings were copied. Paste them into a file or a note to keep them.",
  copyFailedInApp:
    "The Pebble app cannot save files. Copy the text above and paste it into a file or a note to keep it.",
};

export type Strings = typeof en;

const ja: Strings = {
  name: "日本語",
  title: "Turing の設定",
  language: "言語",
  previewLabel: "文字盤のプレビュー",
  loading: "プレビューを読み込んでいます",
  unavailable: "この環境ではプレビューを表示できません",
  loadFailed: "プレビューを読み込めませんでした",
  caption: (speed) => `プレビュー（${speed}倍速）`,
  pattern: "パターン",
  diffusion: "拡散と時間刻み",
  colors: "配色",
  clock: "時計",
  fileSection: "設定の書き出し/読み込み",
  models: ["Gray–Scott", "FitzHugh–Nagumo"],
  presetNames: {
    maze: "迷路",
    coral: "珊瑚",
    mitosis: "細胞分裂",
    spots: "斑点",
    "thin-line": "細線",
    "fhn-stripes": "縞",
    "fhn-hex": "六方斑点",
    "fhn-spiral": "らせん",
  },
  initial: "初期条件",
  inits: ["円板", "断ち切った波面"],
  widths: ["太い", "やや細い", "細い"],
  custom: "カスタム",
  palettes: { green: "ライム", blue: "シアン", mono: "白黒" },
  stops: (count) => `${count} 色`,
  dark: "暗",
  light: "明",
  stopLabels: ["暗い色", "中間 1", "中間 2", "明るい色"],
  value: (label) => `${label}の値`,
  faceLabel: "文字盤",
  faces: ["デジタル", "アナログ"],
  fontLabel: "フォント",
  showClock: "時計を表示",
  showDate: "日付を表示",
  avoid: (analog, date) =>
    !analog ? "数字を避ける" : date ? "針と日付を避ける" : "針を避ける",
  export: "書き出す",
  file: "ファイル",
  jsonLabel: "設定の JSON",
  jsonPlaceholder:
    "書き出した設定がここに出ます。設定の JSON を貼り付けて読み込むこともできます。",
  importText: "テキストを読み込む",
  cancel: "キャンセル",
  save: "保存",
  pasted: "貼り付けた文字列",
  imported: "設定を読み込みました。",
  notSettings: (source) => `${source}は設定として読み込めませんでした。`,
  pasteFirst: "設定の JSON を貼り付けてください。",
  exported: "書き出しました。",
  exportFailed: "書き出せませんでした。",
  exportedFile: (name) => `${name} を書き出しました。`,
  copiedInApp:
    "Pebbleアプリではファイルを保存できないため、設定をコピーしました。ファイルやメモにペーストして保存してください。",
  copyFailedInApp:
    "Pebbleアプリではファイルを保存できません。上の文字列をコピーして、ファイルやメモにペーストして保存してください。",
};

export const STRINGS: Record<Lang, Strings> = { en, ja };
