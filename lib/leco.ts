type Glyph = {
  width: number;
  height: number;
  left_offset: number;
  top_offset: number;
  advance: number;
  bits: string;
};
type Font = { glyphs: Record<string, Glyph> };
let fonts: Record<string, Font>;
let loading: Promise<void> | undefined;
export function loadFonts() {
  return (loading ??= fetch('/fonts/leco.json').then(async (r) => {
    if (!r.ok) throw new Error('LECO font load failed');
    fonts = await r.json();
  }));
}
function line(
  ctx: CanvasRenderingContext2D,
  text: string,
  name: string,
  top: number,
) {
  const f = fonts[name];
  let width = 0;
  for (const c of text) width += f.glyphs[c].advance;
  let x = Math.floor((200 - width) / 2);
  ctx.fillStyle = '#fff';
  for (const c of text) {
    const g = f.glyphs[c];
    for (let y = 0; y < g.height; y++)
      for (let col = 0; col < g.width; col++)
        if (g.bits[y * g.width + col] === '1')
          ctx.fillRect(x + g.left_offset + col, top + g.top_offset + y, 1, 1);
    x += g.advance;
  }
}
export function drawClock(ctx: CanvasRenderingContext2D, date: Date) {
  if (!fonts) return;
  line(
    ctx,
    `${String(date.getHours()).padStart(2, '0')}:${String(date.getMinutes()).padStart(2, '0')}`,
    'LECO_42_NUMBERS',
    78,
  );
  line(
    ctx,
    `${date.getFullYear()}.${String(date.getMonth() + 1).padStart(2, '0')}.${String(date.getDate()).padStart(2, '0')}`,
    'LECO_20_BOLD_NUMBERS',
    128,
  );
}
