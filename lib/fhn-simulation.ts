import type { FhnParameters } from "./simulation.ts";

/** Range of both FitzHugh-Nagumo fields: the codes of the C core span
 * x = -2 to 2. */
export const FHN_LIMIT = 2;

/** Initial broken wave of init 1 (numerical definition version 5), in
 * display coordinates: an excited band from the left edge to the display
 * center, and above it a refractory band, so the wave only runs down and
 * curls up at its free ends. Returns 1 for an excited pixel, 2 for a
 * refractory one, else 0. */
export const WAVE_TOP = 190;
export const WAVE_EXCITED = 8;
export const WAVE_REFRACTORY = 16;
export const WAVE_END = 100;
export function wavePixel(x: number, y: number) {
  if (x >= WAVE_END) return 0;
  if (y >= WAVE_TOP && y < WAVE_TOP + WAVE_EXCITED) return 1;
  if (y >= WAVE_TOP - WAVE_REFRACTORY && y < WAVE_TOP) return 2;
  return 0;
}

/** Float32 FitzHugh-Nagumo reference with the same nine-point Laplacian
 * and explicit Euler step as Simulation:
 *   u' = u + dt (du lap(u) + ru (u - u^3 - v + k) - pull (u - rest))
 *   v' = v + dt (dv lap(v) + rv (u - av v))
 * with both fields clamped to [-2, 2]. pull is 0 away from a mask. */
export class FhnSimulation {
  u: Float32Array;
  v: Float32Array;
  nextU: Float32Array;
  nextV: Float32Array;
  steps = 0;
  constructor(
    public width: number,
    public height: number
  ) {
    const n = width * height;
    this.u = new Float32Array(n);
    this.v = new Float32Array(n);
    this.nextU = new Float32Array(n);
    this.nextV = new Float32Array(n);
  }

  /** The uniform resting field: u = rest and v = rest / av (0 if av = 0). */
  fill(p: Pick<FhnParameters, "rest" | "av">) {
    this.u.fill(p.rest);
    this.v.fill(restV(p));
  }

  /** Excite a grid disk: u = rest + 0.5, v unchanged. */
  seedAt(x: number, y: number, radius: number, rest: number) {
    for (let dy = -radius; dy <= radius; dy++)
      for (let dx = -radius; dx <= radius; dx++) {
        if (dx * dx + dy * dy > radius * radius) continue;
        const i =
          ((y + dy + this.height) % this.height) * this.width +
          ((x + dx + this.width) % this.width);
        this.u[i] = Math.min(FHN_LIMIT, rest + 0.5);
      }
  }

  /** Write the broken wave of init 1 over the field, each grid cell
   * sampling the display pixel it covers. */
  seedWave() {
    for (let gy = 0; gy < this.height; gy++)
      for (let gx = 0; gx < this.width; gx++) {
        const kind = wavePixel(
          Math.floor((gx * 200) / this.width),
          Math.floor((gy * 228) / this.height)
        );
        const i = gy * this.width + gx;
        if (kind === 1) this.u[i] = 1;
        else if (kind === 2) this.v[i] = 1;
      }
  }

  /** Advance count steps. pulls optionally gives a per-cell pull toward
   * rest. The model field of the parameters is not read. */
  step(p: Omit<FhnParameters, "model">, count = 1, pulls?: Float32Array) {
    const w = this.width,
      h = this.height;
    const { du, dv, ru, rv, av, k, dt, rest } = p;
    for (let t = 0; t < count; t++) {
      const u = this.u,
        v = this.v;
      for (let y = 0; y < h; y++) {
        const row = y * w,
          up = ((y + h - 1) % h) * w,
          down = ((y + 1) % h) * w;
        for (let x = 0; x < w; x++) {
          const l = (x + w - 1) % w,
            r = (x + 1) % w,
            i = row + x;
          const a = u[i],
            b = v[i];
          const lapU =
            -a +
            0.2 * (u[row + l] + u[row + r] + u[up + x] + u[down + x]) +
            0.05 * (u[up + l] + u[up + r] + u[down + l] + u[down + r]);
          const lapV =
            -b +
            0.2 * (v[row + l] + v[row + r] + v[up + x] + v[down + x]) +
            0.05 * (v[up + l] + v[up + r] + v[down + l] + v[down + r]);
          const pull = pulls ? pulls[i] : 0;
          const rateU =
            du * lapU + ru * (a - a * a * a - b + k) - pull * (a - rest);
          const rateV = dv * lapV + rv * (a - av * b);
          this.nextU[i] = clamp(a + rateU * dt);
          this.nextV[i] = clamp(b + rateV * dt);
        }
      }
      [this.u, this.nextU] = [this.nextU, this.u];
      [this.v, this.nextV] = [this.nextV, this.v];
      this.steps++;
    }
  }
}

/** The resting v of a parameter set: rest / av, or 0 if av is 0. */
export const restV = (p: Pick<FhnParameters, "rest" | "av">) =>
  p.av ? clamp(p.rest / p.av) : 0;

const clamp = (value: number) =>
  Math.max(-FHN_LIMIT, Math.min(FHN_LIMIT, value));
