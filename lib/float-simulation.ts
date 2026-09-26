import { Simulation, type Parameters } from "./simulation.ts";
import { MASK_KILL, MASK_RAMP, maskLevels } from "./wasm-simulation.ts";

const paletteLow = [
  [0, 30, 18],
  [0, 0, 45],
];
const paletteHigh = [
  [210, 255, 85],
  [85, 255, 255],
];

/** Float32 reference with the same display-coordinate seed layout as the C core. */
export class FloatSimulation {
  readonly field: Simulation;
  readonly width: number;
  readonly height: number;
  readonly bytes: number;

  constructor(width: number, seed: number) {
    this.width = width;
    this.height = Math.floor((width * 228) / 200);
    this.field = new Simulation(this.width, this.height, seed);
    this.bytes =
      this.width * this.height * 2 * 2 * Float32Array.BYTES_PER_ELEMENT;
    this.field.a.fill(1);
    this.field.b.fill(0);

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
    this.field.step(params, 1, this.kills(params.kill));
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

  /** Hold b at 0 in the cells of a mask (or none) and raise the kill rate
   * toward it, like rd_mask of the C core. */
  setMask(mask: Uint8Array | null) {
    this.levels = mask ? maskLevels(mask, this.width, this.height) : null;
    this.killFor = Number.NaN;
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

  get(x: number, y: number, species: number) {
    return (species ? this.field.b : this.field.a)[y * this.width + x];
  }

  private applyMask() {
    if (!this.levels) return;
    for (let i = 0; i < this.levels.length; i++)
      if (this.levels[i] === 0) this.field.b[i] = 0;
  }

  private seedDisplay(x: number, y: number, radius: number) {
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
        this.field.a[index] = 0.5;
        this.field.b[index] = 0.25;
      }
    }
  }

  /** Draw the display like rd_row: each pixel shows the covering cell, or
   * with interpolate the B value interpolated at the pixel center between
   * the four nearest cells, periodic like the field. */
  render(
    pixels: ImageData,
    palette: string,
    quantize: boolean,
    interpolate = false
  ) {
    const data = pixels.data;
    const paletteIndex = palette === "green" ? 0 : 1;
    const { width, height } = this;
    const b = this.field.b;
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
        const intensity = Math.min(1, value * 3);
        const offset = (y * 200 + x) * 4;
        for (let channel = 0; channel < 3; channel++) {
          const color =
            palette === "mono"
              ? intensity >= 0.45
                ? 255
                : 0
              : Math.round(
                  paletteLow[paletteIndex][channel] +
                    (paletteHigh[paletteIndex][channel] -
                      paletteLow[paletteIndex][channel]) *
                      intensity
                );
          data[offset + channel] = quantize
            ? Math.round(color / 85) * 85
            : color;
        }
        data[offset + 3] = 255;
      }
    }
  }
}
