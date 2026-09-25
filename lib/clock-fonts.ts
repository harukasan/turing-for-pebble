/** Clock glyphs extracted from the PebbleOS system fonts, drawn as the watch
 * draws them. The same glyphs are compiled into the core's mask builder. */
type Glyph = {
  width: number;
  height: number;
  left_offset: number;
  top_offset: number;
  advance: number;
  bits: string;
};
type Line = { font: string; top: number; box: number };
type ClockFontData = {
  fonts: Record<string, { glyphs: Record<string, Glyph> }>;
  sets: { name: string; time: Line; date: Line }[];
};
/** Font sets, in the order of CM_FONT_* in core/clock_mask.h. */
export const CLOCK_FONTS = ['leco', 'bitham'] as const;
export type ClockFont = (typeof CLOCK_FONTS)[number];
export const fontIndex = (font: ClockFont) => CLOCK_FONTS.indexOf(font);
let data: ClockFontData | undefined;
let loading: Promise<void> | undefined;
export function loadFonts() {
  return (loading ??= fetch('/fonts/clock-fonts.json').then(async (r) => {
    if (!r.ok) throw new Error('Clock font load failed');
    data = await r.json();
  }));
}
const pad = (value: number) => String(value).padStart(2, '0');
/** The time and date strings of the watch: HH:MM and YYYY.MM.DD. */
export const clockText = (date: Date) => [
  `${pad(date.getHours())}:${pad(date.getMinutes())}`,
  `${date.getFullYear()}.${pad(date.getMonth() + 1)}.${pad(date.getDate())}`,
];
/** Visit every display pixel of the clock glyphs of a font set. */
export function clockPixels(
  date: Date,
  font: ClockFont,
  visit: (x: number, y: number) => void,
) {
  if (!data) return false;
  const set = data.sets[fontIndex(font)];
  const [time, day] = clockText(date);
  for (const [text, line] of [
    [time, set.time],
    [day, set.date],
  ] as const) {
    const glyphs = data.fonts[line.font].glyphs;
    let width = 0;
    for (const c of text) width += glyphs[c].advance;
    let x = Math.floor((200 - width) / 2);
    for (const c of text) {
      const g = glyphs[c];
      for (let y = 0; y < g.height; y++)
        for (let col = 0; col < g.width; col++)
          if (g.bits[y * g.width + col] === '1')
            visit(x + g.left_offset + col, line.top + g.top_offset + y);
      x += g.advance;
    }
  }
  return true;
}
export function drawClock(
  ctx: CanvasRenderingContext2D,
  date: Date,
  font: ClockFont,
) {
  ctx.fillStyle = '#fff';
  clockPixels(date, font, (x, y) => ctx.fillRect(x, y, 1, 1));
}
