import type { Parameters } from './simulation';
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
    t: number,
  ): number;
  rd_seed(p: number, x: number, y: number, r: number): number;
  rd_step(p: number, n: number): number;
  rd_steps(p: number): number;
  rd_row(p: number, y: number, c: number, q: number): number;
};
let loaded: Promise<API> | undefined;
export function loadCore() {
  return (loaded ??= fetch('/wasm/rd.wasm').then(async (r) => {
    if (!r.ok) throw new Error(`Wasm HTTP ${r.status}`);
    const wasmModule = await WebAssembly.compile(await r.arrayBuffer());
    const instance = await WebAssembly.instantiate(wasmModule, {
      wasi_snapshot_preview1: {
        proc_exit() {
          throw new Error('Wasm terminated');
        },
      },
    });
    return instance.exports as API;
  }));
}
export const effective = (p: Parameters) =>
  Object.fromEntries(
    Object.entries(p).map(([k, v]) => [k, Math.round(v * 32768) / 32768]),
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
    seed: number,
  ) {
    this.width = mode === 0 ? 200 : 100;
    this.height = (this.width * 228) / 200;
    this.bytes = api.rd_bytes(mode);
    this.components = Array.from({ length: 5 }, (_, i) =>
      api.rd_memory(mode, i),
    );
    this.allocation = api.malloc(this.bytes);
    if (!this.allocation) throw new Error('Wasm allocation failed');
    this.state = api.rd_init(this.allocation, this.bytes, mode, seed);
    if (!this.state) {
      api.free(this.allocation);
      throw new Error('Invalid simulation configuration');
    }
    this.row = new Uint8Array(
      api.memory.buffer,
      api.rd_row(this.state, 0, 0, 1),
      800,
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
        Math.round(p.dt * 32768),
      )
    )
      throw new Error('Invalid parameters');
    if (this.api.rd_step(this.state, count))
      throw new Error('Invalid step count');
  }
  seedAt(x: number, y: number, radius = 6) {
    this.api.rd_seed(
      this.state,
      Math.floor((x * 200) / this.width),
      Math.floor((y * 228) / this.height),
      radius,
    );
  }
  render(pixels: ImageData, palette: string, quantize: boolean) {
    for (let y = 0; y < 228; y++) {
      this.api.rd_row(
        this.state,
        y,
        palette === 'green' ? 0 : palette === 'blue' ? 1 : 2,
        Number(quantize),
      );
      pixels.data.set(this.row, y * 800);
    }
  }
}
