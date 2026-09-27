import { FloatSimulation } from "../../lib/float-simulation";
import { drawClock, fontIndex, loadFonts } from "../../lib/clock-fonts";
import {
  analogPixels,
  dateAtMinutes,
  drawAnalog,
  hourAngle,
  minuteAngle,
  sweepAngle,
} from "../../lib/clock-face";
import type { Parameters } from "../../lib/simulation";
import {
  WasmSimulation,
  buildAnalogMask,
  buildMask,
  loadCore,
  type CoreAPI,
} from "../../lib/wasm-simulation";
import {
  GRID_WIDTH,
  HALO,
  isFloat,
  SWEEP_MS,
  type Engine,
  type PlayerSettings,
} from "./modes";

type ActiveSimulation = WasmSimulation | FloatSimulation;
/** Angles of the analog hands, in 1/1440 of a turn. */
type Hands = { hour: number; minute: number };

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

/** Parameters from a caller written before version 5 carry no model and
 * are Gray–Scott coefficients. */
const withModel = (params: Parameters): Parameters =>
  params.model
    ? params
    : ({ ...(params as object), model: "gray-scott" } as Parameters);

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
  /** Angles of the analog hands shown, null to snap to the next target. */
  private hands: Hands | null = null;
  /** The sweep of the hands in progress, started at a frame time. */
  private sweep: { from: Hands; to: Hands; start: number } | null = null;
  /** The face bitmap drawn over the field and the key it was built for. */
  private overlay: Uint8Array | null = null;
  private overlayKey = "";

  constructor(
    private readonly canvas: HTMLCanvasElement,
    private readonly assetBaseUrl: string,
    private settings: PlayerSettings,
    private readonly events: PlayerEvents
  ) {
    this.settings = { ...settings, params: withModel(settings.params) };
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
    this.settings = { ...settings, params: withModel(settings.params) };
  }

  setParameters(params: Parameters) {
    this.settings = { ...this.settings, params: withModel(params) };
  }

  play() {
    this.playing = true;
  }

  pause() {
    this.playing = false;
  }

  /** Reset the field: a new simulation of the model of params (by default
   * the current settings) with the engine and seed. Parameters of another
   * model set later are not stepped until the next load. */
  async load(
    engine: Engine,
    seed: number,
    params: Parameters = this.settings.params
  ) {
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
        ? new FloatSimulation(GRID_WIDTH, seed, params)
        : new WasmSimulation(api, seed, params);
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
      face,
      time,
      palette,
      quantize,
      interpolate,
    } = this.settings;
    const start = performance.now();
    const shown = time === null ? new Date() : dateAtMinutes(time);
    const day = `${shown.getFullYear()}-${shown.getMonth() + 1}-${shown.getDate()}`;
    try {
      let faceKey: string;
      let hands: Hands | null = null;
      if (face === "analog") {
        hands = this.moveHands(
          core,
          {
            hour: hourAngle(shown.getHours(), shown.getMinutes()),
            minute: minuteAngle(shown.getMinutes()),
          },
          now
        );
        faceKey = `analog|${font}|${day}|${hands.hour}|${hands.minute}`;
      } else {
        this.hands = null;
        this.sweep = null;
        faceKey = `digital|${font}|${day}|${shown.getHours() * 60 + shown.getMinutes()}`;
      }
      const key = clock && avoid ? faceKey : "";
      if (sim !== this.maskSim || key !== this.maskKey) {
        sim.setMask(
          !key
            ? null
            : hands
              ? buildAnalogMask(
                  core,
                  sim.width,
                  sim.height,
                  fontIndex(font),
                  hands.hour,
                  hands.minute,
                  shown,
                  HALO
                )
              : buildMask(
                  core,
                  sim.width,
                  sim.height,
                  fontIndex(font),
                  shown,
                  HALO
                )
        );
        this.maskSim = sim;
        this.maskKey = key;
      }
      // Parameters of another model wait for the reload that the model
      // change requests.
      const target =
        params.model !== sim.model
          ? 0
          : this.manualPending > 0
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
      if (clock && hands) {
        if (faceKey !== this.overlayKey) {
          this.overlay = analogPixels(
            core,
            fontIndex(font),
            hands.hour,
            hands.minute,
            shown
          );
          this.overlayKey = faceKey;
        }
        if (this.overlay) drawAnalog(this.ctx, this.overlay);
      } else if (clock) drawClock(this.ctx, shown, font);
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
  /** Move the analog hands toward a target at frame time now: snap on the
   * first frame, start a sweep from the shown angles when the target
   * changes, and ease along it for SWEEP_MS. The mask key holds the shown
   * angles, so the mask is rebuilt only when an integer angle changes. */
  private moveHands(core: CoreAPI, target: Hands, now: number): Hands {
    if (!this.hands) {
      this.sweep = null;
      this.hands = target;
      return target;
    }
    const goal = this.sweep?.to ?? this.hands;
    if (target.hour !== goal.hour || target.minute !== goal.minute)
      this.sweep = { from: this.hands, to: target, start: now };
    if (this.sweep) {
      const { from, to } = this.sweep;
      const elapsed = now - this.sweep.start;
      if (elapsed >= SWEEP_MS) {
        this.hands = to;
        this.sweep = null;
      } else {
        this.hands = {
          hour: sweepAngle(core, from.hour, to.hour, elapsed, SWEEP_MS),
          minute: sweepAngle(core, from.minute, to.minute, elapsed, SWEEP_MS),
        };
      }
    }
    return this.hands;
  }
}
