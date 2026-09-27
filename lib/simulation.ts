export type GrayScottParameters = {
  model: "gray-scott";
  feed: number;
  kill: number;
  da: number;
  db: number;
  dt: number;
};
/** FitzHugh-Nagumo in the form of numerical definition version 5: u is the
 * displayed species, rest its resting value, and init selects the initial
 * condition (0 disks, 1 a broken wave). */
export type FhnParameters = {
  model: "fhn";
  du: number;
  dv: number;
  ru: number;
  rv: number;
  av: number;
  k: number;
  dt: number;
  rest: number;
  init: number;
};
export type Parameters = GrayScottParameters | FhnParameters;
export type Model = Parameters["model"];
export {
  defaultParametersFor,
  modelIndex,
  presets,
  presetById,
  presetParameters,
  parameterOrder,
  parameterValues,
  parameterVector,
  type Preset,
} from "./presets.ts";
export class Simulation {
  a: Float32Array;
  b: Float32Array;
  nextA: Float32Array;
  nextB: Float32Array;
  steps = 0;
  constructor(
    public width: number,
    public height: number,
    seed = 42
  ) {
    const n = width * height;
    this.a = new Float32Array(n).fill(1);
    this.b = new Float32Array(n);
    this.nextA = new Float32Array(n);
    this.nextB = new Float32Array(n);
    let state = seed >>> 0;
    const random = () => {
      state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
      return state / 4294967296;
    };
    for (let spot = 0; spot < 24; spot++) {
      const x = Math.floor(random() * width),
        y = Math.floor(random() * height);
      this.seedAt(x, y, 2 + Math.floor(random() * 3));
    }
  }
  seedAt(x: number, y: number, radius = 3) {
    for (let dy = -radius; dy <= radius; dy++)
      for (let dx = -radius; dx <= radius; dx++) {
        if (dx * dx + dy * dy > radius * radius) continue;
        const i =
          ((y + dy + this.height) % this.height) * this.width +
          ((x + dx + this.width) % this.width);
        this.a[i] = 0.5;
        this.b[i] = 0.25;
      }
  }
  /** Advance count steps. kills optionally gives a per-cell kill rate.
   * The model field of the parameters is not read. */
  step(p: Omit<GrayScottParameters, "model">, count = 1, kills?: Float32Array) {
    const w = this.width,
      h = this.height;
    for (let t = 0; t < count; t++) {
      for (let y = 0; y < h; y++) {
        const row = y * w,
          up = ((y + h - 1) % h) * w,
          down = ((y + 1) % h) * w;
        for (let x = 0; x < w; x++) {
          const l = (x + w - 1) % w,
            r = (x + 1) % w,
            i = row + x;
          const a = this.a[i],
            b = this.b[i];
          const lapA =
            -a +
            0.2 *
              (this.a[row + l] +
                this.a[row + r] +
                this.a[up + x] +
                this.a[down + x]) +
            0.05 *
              (this.a[up + l] +
                this.a[up + r] +
                this.a[down + l] +
                this.a[down + r]);
          const lapB =
            -b +
            0.2 *
              (this.b[row + l] +
                this.b[row + r] +
                this.b[up + x] +
                this.b[down + x]) +
            0.05 *
              (this.b[up + l] +
                this.b[up + r] +
                this.b[down + l] +
                this.b[down + r]);
          const reaction = a * b * b;
          this.nextA[i] = Math.max(
            0,
            Math.min(1, a + (p.da * lapA - reaction + p.feed * (1 - a)) * p.dt)
          );
          this.nextB[i] = Math.max(
            0,
            Math.min(
              1,
              b +
                (p.db * lapB +
                  reaction -
                  ((kills?.[i] ?? p.kill) + p.feed) * b) *
                  p.dt
            )
          );
        }
      }
      [this.a, this.nextA] = [this.nextA, this.a];
      [this.b, this.nextB] = [this.nextB, this.b];
      this.steps++;
    }
  }
}
