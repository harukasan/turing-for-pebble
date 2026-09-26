import { FloatSimulation } from "../../lib/float-simulation";
import { drawClock, fontIndex, loadFonts } from "../../lib/clock-fonts";
import type { Parameters } from "../../lib/simulation";
import {
  WasmSimulation,
  buildMask,
  loadCore,
  type CoreAPI,
} from "../../lib/wasm-simulation";
import {
  engineMode,
  engineWidth,
  HALO,
  isFloat,
  type Engine,
  type PlayerSettings,
} from "./modes";

type ActiveSimulation = WasmSimulation | FloatSimulation;

export type PlayerStats = {
  steps: number;
  stepMs: number;
  bytes: number | null;
};

export type PlayerEvents = {
  onStats: (stats: PlayerStats) => void;
  onLoading: (loading: boolean) => void;
  onError: (error: unknown, phase: "canvas" | "load" | "render") => void;
};

export class TuringPlayer {
  private readonly ctx: CanvasRenderingContext2D | null;
  private readonly offscreen = document.createElement("canvas");
  private readonly off: CanvasRenderingContext2D | null;
  private readonly pixels: ImageData | null;
  private sim: ActiveSimulation | null = null;
  private core: CoreAPI | null = null;
  private frame = 0;
  private generation = 0;
  private disposed = false;
  private manualPending = 0;
  private stepMs = 0;
  private lastPaint = 0;
  private lastStats = 0;
  private maskSim: ActiveSimulation | null = null;
  private maskKey = "";
  private playing = false;

  constructor(
    private readonly canvas: HTMLCanvasElement,
    private readonly assetBaseUrl: string,
    private settings: PlayerSettings,
    private readonly events: PlayerEvents
  ) {
    this.ctx = canvas.getContext("2d");
    this.offscreen.width = 200;
    this.offscreen.height = 228;
    this.off = this.offscreen.getContext("2d");
    this.pixels = this.off?.createImageData(200, 228) ?? null;
    if (!this.ctx || !this.off || !this.pixels) {
      this.events.onError(new Error("Canvas 2D is unavailable"), "canvas");
    } else {
      this.frame = requestAnimationFrame(this.render);
    }
  }

  setSettings(settings: PlayerSettings) {
    this.settings = settings;
  }

  setParameters(params: Parameters) {
    this.settings = { ...this.settings, params };
  }

  play() {
    this.playing = true;
  }

  pause() {
    this.playing = false;
  }

  async load(engine: Engine, seed: number) {
    const requested = ++this.generation;
    this.sim?.dispose();
    this.sim = null;
    this.manualPending = 0;
    this.maskSim = null;
    this.maskKey = "";
    this.stepMs = 0;
    this.publishStats();
    if (!this.ctx || !this.off || !this.pixels) return;
    this.events.onLoading(true);
    try {
      const base = new URL(
        this.assetBaseUrl.endsWith("/")
          ? this.assetBaseUrl
          : `${this.assetBaseUrl}/`,
        document.baseURI
      );
      const asset = (path: string) => new URL(path, base).href;
      const [api] = await Promise.all([
        loadCore(asset("wasm/rd.wasm")),
        loadFonts(asset("fonts/clock-fonts.json")),
      ]);
      if (this.disposed || requested !== this.generation) return;
      this.core = api;
      this.sim = isFloat(engine)
        ? new FloatSimulation(engineWidth(engine), seed)
        : new WasmSimulation(api, engineMode(engine), seed);
      this.events.onLoading(false);
      this.publishStats();
    } catch (error) {
      if (this.disposed || requested !== this.generation) return;
      this.pause();
      this.events.onLoading(false);
      this.events.onError(error, "load");
      this.publishStats();
    }
  }

  advance(count: number) {
    if (!this.playing && Number.isInteger(count) && count > 0)
      this.manualPending += count;
  }

  /** Seed at normalized canvas coordinates in the range [0, 1). */
  seed(x: number, y: number) {
    if (
      !this.sim ||
      !Number.isFinite(x) ||
      !Number.isFinite(y) ||
      x < 0 ||
      x >= 1 ||
      y < 0 ||
      y >= 1
    )
      return;
    this.sim.seedAt(
      Math.floor(x * this.sim.width),
      Math.floor(y * this.sim.height)
    );
  }

  dispose() {
    if (this.disposed) return;
    this.disposed = true;
    this.generation++;
    cancelAnimationFrame(this.frame);
    this.sim?.dispose();
    this.sim = null;
  }

  private publishStats() {
    this.events.onStats({
      steps: this.sim?.steps ?? 0,
      stepMs: this.stepMs,
      bytes: this.sim?.bytes ?? null,
    });
  }

  private render = (now: number) => {
    if (this.disposed) return;
    this.frame = requestAnimationFrame(this.render);
    if (
      document.hidden ||
      now - this.lastPaint < 1000 / 30 ||
      !this.sim ||
      !this.core ||
      !this.ctx ||
      !this.off ||
      !this.pixels
    )
      return;

    const sim = this.sim;
    const core = this.core;
    const {
      params,
      speed,
      clock,
      avoid,
      font,
      palette,
      quantize,
      interpolate,
    } = this.settings;
    const start = performance.now();
    const minute = Math.floor(Date.now() / 60000);
    const key = clock && avoid ? `${font}|${minute}` : "";
    try {
      if (sim !== this.maskSim || key !== this.maskKey) {
        sim.setMask(
          key
            ? buildMask(
                core,
                sim.width,
                sim.height,
                fontIndex(font),
                new Date(),
                HALO
              )
            : null
        );
        this.maskSim = sim;
        this.maskKey = key;
      }
      const target =
        this.manualPending > 0
          ? Math.min(this.manualPending, 8)
          : this.playing
            ? speed
            : 0;
      let steps = 0;
      while (steps < target) {
        sim.step(params);
        steps++;
        if (performance.now() - start >= 8) break;
      }
      if (this.manualPending > 0) this.manualPending -= steps;
      if (steps) this.stepMs = (performance.now() - start) / steps;
      sim.render(this.pixels, palette, quantize, interpolate);
      this.off.putImageData(this.pixels, 0, 0);
      this.ctx.imageSmoothingEnabled = false;
      this.ctx.drawImage(this.offscreen, 0, 0);
      if (clock) drawClock(this.ctx, new Date(), font);
      this.lastPaint = now;
      if (now - this.lastStats > 400) {
        this.publishStats();
        this.lastStats = now;
      }
    } catch (error) {
      this.pause();
      this.events.onError(error, "render");
      this.publishStats();
    }
  };
}
