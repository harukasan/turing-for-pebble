import { Simulation, type Parameters } from './simulation.ts';

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

  constructor(width: 100 | 200, seed: number) {
    this.width = width;
    this.height = (width * 228) / 200;
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
    this.field.step(params);
  }

  seedAt(x: number, y: number, radius = 6) {
    this.seedDisplay(
      Math.floor((x * 200) / this.width),
      Math.floor((y * 228) / this.height),
      radius,
    );
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

  render(pixels: ImageData, palette: string, quantize: boolean) {
    const data = pixels.data;
    const paletteIndex = palette === 'green' ? 0 : 1;
    for (let y = 0; y < 228; y++) {
      const gy = Math.floor((y * this.height) / 228);
      for (let x = 0; x < 200; x++) {
        const gx = Math.floor((x * this.width) / 200);
        const intensity = Math.min(1, this.field.b[gy * this.width + gx] * 3);
        const offset = (y * 200 + x) * 4;
        for (let channel = 0; channel < 3; channel++) {
          const color =
            palette === 'mono'
              ? intensity >= 0.45
                ? 255
                : 0
              : Math.round(
                  paletteLow[paletteIndex][channel] +
                    (paletteHigh[paletteIndex][channel] -
                      paletteLow[paletteIndex][channel]) *
                      intensity,
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
