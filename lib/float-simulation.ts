import { floatColor, paletteIndex } from "./palettes.ts";
import {
  Simulation,
  defaultParametersFor,
  type Model,
  type Parameters,
} from "./simulation.ts";
import { FhnSimulation, FHN_LIMIT } from "./fhn-simulation.ts";
import {
  MASK_KILL,
  MASK_PULL,
  MASK_RAMP,
  maskLevels,
} from "./wasm-simulation.ts";

/** Float32 reference with the same display-coordinate seed layout as the C
 * core, for either model. The model is fixed at construction. */
export class FloatSimulation {
  readonly field: Simulation | FhnSimulation;
  readonly model: Model;
  readonly width: number;
  readonly height: number;
  readonly bytes: number;
  /** FitzHugh-Nagumo: the resting u of the latest parameters, which seeds
   * and masked cells use. */
  private rest = 0;

  constructor(
    width: number,
    seed: number,
    params: Parameters = defaultParametersFor("gray-scott")
  ) {
    this.width = width;
    this.height = Math.floor((width * 228) / 200);
    this.model = params.model;
    this.bytes =
      this.width * this.height * 2 * 2 * Float32Array.BYTES_PER_ELEMENT;
    if (params.model === "fhn") {
      const field = new FhnSimulation(this.width, this.height);
      field.fill(params);
      this.rest = params.rest;
      this.field = field;
      if (params.init === 1) {
        field.seedWave();
        return;
      }
    } else {
      const field = new Simulation(this.width, this.height, seed);
      field.a.fill(1);
      field.b.fill(0);
      this.field = field;
    }

    let state = seed >>> 0;
    const randomBelow = (range: number) => {
      state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
      return Math.floor((state * range) / 4294967296);
    };
    for (let disk = 0; disk < 24; disk++) {
      const x = randomBelow(200);
      const y = randomBelow(228);
      const radius = 4 + randomBelow(6);
      this.seedDisplay(x, y, radius);
    }
  }

  get steps() {
    return this.field.steps;
  }

  dispose() {}

  step(params: Parameters) {
    if (params.model !== this.model)
      throw new Error(`${params.model} parameters for a ${this.model} field`);
    // The field was built for this model, so the casts only name its type.
    if (params.model === "fhn") {
      this.rest = params.rest;
      (this.field as FhnSimulation).step(params, 1, this.pulls());
    } else {
      (this.field as Simulation).step(params, 1, this.kills(params.kill));
    }
    this.applyMask();
  }

  seedAt(x: number, y: number, radius = 6) {
    this.seedDisplay(
      Math.floor((x * 200) / this.width),
      Math.floor((y * 228) / this.height),
      radius
    );
    this.applyMask();
  }

  private levels: Uint8Array | null = null;
  private killMap: Float32Array | null = null;
  private killFor = Number.NaN;
  private pullMap: Float32Array | null = null;
  /** FitzHugh-Nagumo pull toward rest at mask level 0 (RD_MASK_PULL of
   * the core), which scripts/fhn-explore.mjs varies before setMask. */
  maskPull = MASK_PULL;

  /** Hold the displayed species at its resting value (b = 0, or u = rest)
   * in the cells of a mask (or none) and damp the pattern toward it, like
   * rd_mask of the C core: Gray-Scott raises the kill rate, FitzHugh-Nagumo
   * pulls u toward rest. */
  setMask(mask: Uint8Array | null) {
    this.levels = mask ? maskLevels(mask, this.width, this.height) : null;
    this.killFor = Number.NaN;
    this.pullMap = null;
    this.applyMask();
  }

  maskLevel(x: number, y: number) {
    return this.levels ? this.levels[y * this.width + x] : MASK_RAMP;
  }

  /** Per-cell kill rates for the mask levels, rebuilt when kill changes. */
  private kills(kill: number) {
    if (!this.levels) return undefined;
    if (this.killFor !== kill) {
      const levels = this.levels;
      this.killMap = Float32Array.from(
        levels,
        (level) => kill + ((MASK_KILL - kill) * (MASK_RAMP - level)) / MASK_RAMP
      );
      this.killFor = kill;
    }
    return this.killMap ?? undefined;
  }

  /** Per-cell pull toward rest for the mask levels. */
  private pulls() {
    if (!this.levels) return undefined;
    this.pullMap ??= Float32Array.from(
      this.levels,
      (level) => (this.maskPull * (MASK_RAMP - level)) / MASK_RAMP
    );
    return this.pullMap;
  }

  /** The stored fraction of a species, as rd_get of the core: Gray-Scott
   * concentrations as they are, FitzHugh-Nagumo x as (x + 2) / 4 with v as
   * species 0 and u as species 1. */
  get(x: number, y: number, species: number) {
    const i = y * this.width + x;
    if (this.field instanceof FhnSimulation)
      return ((species ? this.field.u : this.field.v)[i] + 2) / 4;
    return (species ? this.field.b : this.field.a)[i];
  }

  private applyMask() {
    if (!this.levels) return;
    const shown =
      this.field instanceof FhnSimulation ? this.field.u : this.field.b;
    const rest = this.field instanceof FhnSimulation ? this.rest : 0;
    for (let i = 0; i < this.levels.length; i++)
      if (this.levels[i] === 0) shown[i] = rest;
  }

  private seedDisplay(x: number, y: number, radius: number) {
    const field = this.field;
    for (let gy = 0; gy < this.height; gy++) {
      const displayY = Math.floor((gy * 228) / this.height);
      const dy = Math.abs(displayY - y);
      const wrappedY = Math.min(dy, 228 - dy);
      for (let gx = 0; gx < this.width; gx++) {
        const displayX = Math.floor((gx * 200) / this.width);
        const dx = Math.abs(displayX - x);
        const wrappedX = Math.min(dx, 200 - dx);
        if (wrappedX * wrappedX + wrappedY * wrappedY > radius * radius)
          continue;
        const index = gy * this.width + gx;
        if (field instanceof FhnSimulation) {
          field.u[index] = Math.min(FHN_LIMIT, this.rest + 0.5);
        } else {
          field.a[index] = 0.5;
          field.b[index] = 0.25;
        }
      }
    }
  }

  /** Draw the display like rd_row: each pixel shows the covering cell, or
   * with interpolate the displayed species interpolated at the pixel
   * center between the four nearest cells, periodic like the field. The
   * intensity is 3 b for Gray-Scott and u for FitzHugh-Nagumo, clamped to
   * [0, 1]. */
  render(
    pixels: ImageData,
    palette: string,
    quantize: boolean,
    interpolate = false
  ) {
    const data = pixels.data;
    const index = paletteIndex(palette);
    const { width, height } = this;
    const fhn = this.field instanceof FhnSimulation;
    const b = this.field instanceof FhnSimulation ? this.field.u : this.field.b;
    const gain = fhn ? 1 : 3;
    const sample = (p: number, n: number, size: number) => {
      const u = ((2 * p + 1) * n - size) / (2 * size);
      const cell = Math.floor(u);
      return [(cell + n) % n, u - cell];
    };
    for (let y = 0; y < 228; y++) {
      const [gy, wy] = interpolate
        ? sample(y, height, 228)
        : [Math.floor((y * height) / 228), 0];
      const gy1 = (gy + 1) % height;
      for (let x = 0; x < 200; x++) {
        const [gx, wx] = interpolate
          ? sample(x, width, 200)
          : [Math.floor((x * width) / 200), 0];
        const gx1 = (gx + 1) % width;
        const value =
          (b[gy * width + gx] * (1 - wx) + b[gy * width + gx1] * wx) *
            (1 - wy) +
          (b[gy1 * width + gx] * (1 - wx) + b[gy1 * width + gx1] * wx) * wy;
        const intensity = Math.max(0, Math.min(1, value * gain));
        const rgb = floatColor(index, intensity);
        const offset = (y * 200 + x) * 4;
        for (let channel = 0; channel < 3; channel++) {
          const color = rgb[channel];
          data[offset + channel] = quantize
            ? Math.round(color / 85) * 85
            : color;
        }
        data[offset + 3] = 255;
      }
    }
  }
}
