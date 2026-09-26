import { paletteIndex, type Rgb } from "./palettes.ts";
import type { Parameters } from "./simulation";
type API = {
  memory: WebAssembly.Memory;
  malloc(n: number): number;
  free(p: number): void;
  rd_bytes(m: number): number;
  rd_memory(m: number, c: number): number;
  rd_init(p: number, n: number, m: number, s: number): number;
  rd_params(
    p: number,
    f: number,
    k: number,
    a: number,
    b: number,
    t: number
  ): number;
  rd_palette(p: number, rgb: number, count: number): number;
  rd_seed(p: number, x: number, y: number, r: number): number;
  rd_step(p: number, n: number): number;
  rd_steps(p: number): number;
  rd_row(p: number, y: number, c: number, flags: number): number;
  rd_width(p: number): number;
  rd_height(p: number): number;
  rd_get(p: number, x: number, y: number, s: number): number;
  rd_mask(p: number, m: number): number;
  rd_mask_level(p: number, x: number, y: number): number;
  cm_bytes(w: number, h: number): number;
  cm_build(
    m: number,
    w: number,
    h: number,
    font: number,
    hour: number,
    minute: number,
    year: number,
    month: number,
    day: number,
    halo: number
  ): number;
};
export type CoreAPI = API;
/** The clock mask of a grid (cm_build in core/clock_mask.c), one bit per
 * cell in rows of (width + 7) / 8 bytes. */
export function buildMask(
  api: API,
  width: number,
  height: number,
  font: number,
  date: Date,
  halo: number
) {
  const bytes = api.cm_bytes(width, height);
  const pointer = api.malloc(bytes);
  if (!pointer) throw new Error("Wasm allocation failed");
  try {
    if (
      api.cm_build(
        pointer,
        width,
        height,
        font,
        date.getHours(),
        date.getMinutes(),
        date.getFullYear(),
        date.getMonth() + 1,
        date.getDate(),
        halo
      )
    )
      throw new Error("Invalid clock mask");
    return new Uint8Array(api.memory.buffer, pointer, bytes).slice();
  } finally {
    api.free(pointer);
  }
}
/** Ramp of the mask levels and the kill rate at the mask (RD_MASK_RAMP and
 * RD_MASK_KILL of core/rd.h). */
export const MASK_RAMP = 5;
export const MASK_KILL = 2458 / 32768;
/** Mask levels as rd_mask derives them: 0 in a masked cell, else the
 * chessboard distance to the nearest masked cell without wrapping, capped
 * at MASK_RAMP. */
export function maskLevels(mask: Uint8Array, width: number, height: number) {
  const levels = new Uint8Array(width * height).fill(MASK_RAMP);
  for (let y = 0; y < height; y++)
    for (let x = 0; x < width; x++) {
      if (!maskBit(mask, width, x, y)) continue;
      for (
        let yy = Math.max(0, y - MASK_RAMP);
        yy <= Math.min(height - 1, y + MASK_RAMP);
        yy++
      )
        for (
          let xx = Math.max(0, x - MASK_RAMP);
          xx <= Math.min(width - 1, x + MASK_RAMP);
          xx++
        ) {
          const d = Math.max(Math.abs(xx - x), Math.abs(yy - y));
          if (d < levels[yy * width + xx]) levels[yy * width + xx] = d;
        }
    }
  return levels;
}
/** Whether cell (x, y) is set in a mask of a grid of the given width. */
export const maskBit = (
  mask: Uint8Array,
  width: number,
  x: number,
  y: number
) => (mask[y * ((width + 7) >> 3) + (x >> 3)] >> (x & 7)) & 1;
const loaded = new Map<string, Promise<API>>();
export function loadCore(url = "/wasm/rd.wasm") {
  const existing = loaded.get(url);
  if (existing) return existing;
  const request = fetch(url).then(async (r) => {
    if (!r.ok) throw new Error(`Wasm HTTP ${r.status}`);
    const wasmModule = await WebAssembly.compile(await r.arrayBuffer());
    const instance = await WebAssembly.instantiate(wasmModule, {
      wasi_snapshot_preview1: {
        proc_exit() {
          throw new Error("Wasm terminated");
        },
      },
    });
    return instance.exports as API;
  });
  loaded.set(url, request);
  request.catch(() => loaded.delete(url));
  return request;
}
/** The parameters the core computes with: each rounded to Q15, and in the
 * Q15 modes (1 and 3) the diffusion coefficients folded with the 1/20 of
 * the Laplacian into round(D / 20) / 20 (numerical definition version 4). */
export const effective = (p: Parameters, mode = 0) =>
  Object.fromEntries(
    Object.entries(p).map(([k, v]) => {
      const q = Math.round(v * 32768);
      const folded =
        (mode === 1 || mode === 3) && (k === "da" || k === "db")
          ? Math.round(q / 20) * 20
          : q;
      return [k, folded / 32768];
    })
  );
export class WasmSimulation {
  private allocation: number;
  private state: number;
  private row: Uint8Array;
  readonly width: number;
  readonly height: number;
  readonly bytes: number;
  readonly components: number[];
  constructor(
    private api: API,
    public mode: number,
    seed: number
  ) {
    this.bytes = api.rd_bytes(mode);
    this.components = Array.from({ length: 6 }, (_, i) =>
      api.rd_memory(mode, i)
    );
    this.allocation = api.malloc(this.bytes);
    if (!this.allocation) throw new Error("Wasm allocation failed");
    this.state = api.rd_init(this.allocation, this.bytes, mode, seed);
    if (!this.state) {
      api.free(this.allocation);
      throw new Error("Invalid simulation configuration");
    }
    this.width = api.rd_width(this.state);
    this.height = api.rd_height(this.state);
    this.row = new Uint8Array(
      api.memory.buffer,
      api.rd_row(this.state, 0, 0, 1),
      800
    );
  }
  dispose() {
    if (this.allocation) this.api.free(this.allocation);
    this.allocation = 0;
    this.state = 0;
  }
  get steps() {
    return this.api.rd_steps(this.state);
  }
  get linearBytes() {
    return this.api.memory.buffer.byteLength;
  }
  step(p: Parameters, count = 1) {
    if (
      this.api.rd_params(
        this.state,
        Math.round(p.feed * 32768),
        Math.round(p.kill * 32768),
        Math.round(p.da * 32768),
        Math.round(p.db * 32768),
        Math.round(p.dt * 32768)
      )
    )
      throw new Error("Invalid parameters");
    if (this.api.rd_step(this.state, count))
      throw new Error("Invalid step count");
  }
  seedAt(x: number, y: number, radius = 6) {
    this.api.rd_seed(
      this.state,
      Math.floor((x * 200) / this.width),
      Math.floor((y * 228) / this.height),
      radius
    );
  }
  /** Hold B at 0 in the cells of a mask (or none) and raise the kill rate
   * toward it, as rd_mask of the core. */
  setMask(mask: Uint8Array | null) {
    if (!mask) {
      this.api.rd_mask(this.state, 0);
      return;
    }
    const pointer = this.api.malloc(mask.length);
    if (!pointer) throw new Error("Wasm allocation failed");
    new Uint8Array(this.api.memory.buffer, pointer, mask.length).set(mask);
    this.api.rd_mask(this.state, pointer);
    this.api.free(pointer);
  }
  /** Stops of the custom palette (PALETTE_CUSTOM), as rd_palette. */
  setPalette(stops: readonly Rgb[]) {
    const pointer = this.api.malloc(stops.length * 3);
    if (!pointer) throw new Error("Wasm allocation failed");
    new Uint8Array(this.api.memory.buffer, pointer, stops.length * 3).set(
      stops.flat()
    );
    const result = this.api.rd_palette(this.state, pointer, stops.length);
    this.api.free(pointer);
    if (result) throw new Error("Invalid palette stops");
  }
  maskLevel(x: number, y: number) {
    return this.api.rd_mask_level(this.state, x, y);
  }
  get(x: number, y: number, species: number) {
    return this.api.rd_get(this.state, x, y, species) / 2 ** 24;
  }
  /** Draw the display; interpolate samples B between cells at each pixel
   * center (RD_ROW_BILINEAR) instead of showing the covering cell. */
  render(
    pixels: ImageData,
    palette: string | number,
    quantize: boolean,
    interpolate = false
  ) {
    const flags = Number(quantize) | (interpolate ? 2 : 0);
    const index = typeof palette === "number" ? palette : paletteIndex(palette);
    for (let y = 0; y < 228; y++) {
      if (!this.api.rd_row(this.state, y, index, flags))
        throw new Error("Invalid palette");
      pixels.data.set(this.row, y * 800);
    }
  }
}
