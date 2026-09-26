/** A preview of the watchface for a set of watch settings: the shared C
 * core as WebAssembly in the watch's mode, seed, and rendering, run for the
 * steps of the watch's startup animation, with the clock drawn from the
 * same glyphs as the watch. */
import { CLOCK_FONTS, drawClock, loadFonts } from "../../lib/clock-fonts.ts";
import {
  buildMask,
  loadCore,
  WasmSimulation,
  type CoreAPI,
} from "../../lib/wasm-simulation.ts";
import { HALO } from "../core/modes.ts";
import { customStops, toParameters, type WatchSettings } from "./settings.ts";

export const PREVIEW_MODE = 3;
export const PREVIEW_SEED = 42;
/** About the steps of the 30 s startup on the watch (docs/behavior.md). */
export const PREVIEW_STEPS = 1650;
/** Compute time per animation frame, the watch's slice budget. */
const SLICE_MS = 30;

/** The watch's field for a set of settings, without a canvas. */
export class PreviewField {
  private sim: WasmSimulation | null = null;
  private settings: WatchSettings | null = null;

  constructor(private readonly api: CoreAPI) {}

  /** A new field from the seed with the coefficients, custom stops, and
   * digit mask of the settings, as the watch starts one. */
  start(settings: WatchSettings, date: Date) {
    this.sim?.dispose();
    const sim = new WasmSimulation(this.api, PREVIEW_MODE, PREVIEW_SEED);
    this.sim = sim;
    this.settings = settings;
    sim.setPalette(customStops(settings));
    sim.setMask(
      settings.clock && settings.avoid
        ? buildMask(this.api, sim.width, sim.height, settings.font, date, HALO)
        : null
    );
  }

  get steps() {
    return this.sim?.steps ?? 0;
  }

  step(count: number) {
    if (this.sim && this.settings)
      this.sim.step(toParameters(this.settings), count);
  }

  /** New palette settings, without touching the field. */
  recolor(settings: WatchSettings) {
    this.settings = settings;
    this.sim?.setPalette(customStops(settings));
  }

  /** The watch's pixels: quantized and interpolated. */
  render(pixels: ImageData) {
    if (this.sim && this.settings)
      this.sim.render(pixels, this.settings.palette, true, true);
  }

  hash() {
    return this.sim?.hash() ?? 0;
  }

  dispose() {
    this.sim?.dispose();
    this.sim = null;
  }
}

/** The preview on a 200 × 228 canvas, stepped within SLICE_MS per frame up
 * to PREVIEW_STEPS. */
export class PreviewView {
  private field: PreviewField | null = null;
  private settings: WatchSettings | null = null;
  private frame = 0;
  private readonly ctx: CanvasRenderingContext2D;
  private readonly pixels: ImageData;

  constructor(
    canvas: HTMLCanvasElement,
    private readonly onProgress: (steps: number) => void
  ) {
    const ctx = canvas.getContext("2d");
    if (!ctx) throw new Error("Canvas 2D is unavailable");
    this.ctx = ctx;
    this.pixels = ctx.createImageData(200, 228);
  }

  async load(wasmUrl: string, fontsUrl: string) {
    const [api] = await Promise.all([loadCore(wasmUrl), loadFonts(fontsUrl)]);
    this.field = new PreviewField(api);
  }

  /** Start the field over for new settings and animate it. */
  run(settings: WatchSettings) {
    if (!this.field) return;
    cancelAnimationFrame(this.frame);
    this.settings = settings;
    this.field.start(settings, new Date());
    this.frame = requestAnimationFrame(this.tick);
  }

  /** Redraw with new palette settings. */
  recolor(settings: WatchSettings) {
    if (!this.field) return;
    this.settings = settings;
    this.field.recolor(settings);
    this.paint();
  }

  private tick = () => {
    const field = this.field;
    if (!field) return;
    const start = performance.now();
    while (field.steps < PREVIEW_STEPS && performance.now() - start < SLICE_MS)
      field.step(1);
    this.paint();
    if (field.steps < PREVIEW_STEPS)
      this.frame = requestAnimationFrame(this.tick);
  };

  private paint() {
    if (!this.field || !this.settings) return;
    this.field.render(this.pixels);
    this.ctx.putImageData(this.pixels, 0, 0);
    if (this.settings.clock)
      drawClock(this.ctx, new Date(), CLOCK_FONTS[this.settings.font]);
    this.onProgress(this.field.steps);
  }
}
