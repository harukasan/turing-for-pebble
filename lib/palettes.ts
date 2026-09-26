/** Display palettes of the core, in the order of RD_PALETTE_* in core/rd.h.
 * A palette maps the display intensity, B times 3 clamped to 1.0, through
 * equally spaced RGB stops, except the monochrome threshold palette. The
 * core's table core/palettes.h is generated from this list by
 * scripts/gen-palettes.mjs, and the custom palette after the built-in ones
 * takes its stops from rd_palette.
 *
 * The colormap stops are the entries round(i * 255 / 7), i = 0 to 7, of the
 * 256-entry tables in matplotlib's lib/matplotlib/_cm_listed.py at commit
 * 2d0352ae25bd3be72b1585f70e36b5e4cd07bc9c, times 255 and rounded. Their
 * origins and licenses are in licenses/palettes.txt. */
export type Rgb = readonly [number, number, number];

export type Palette = {
  id: string;
  name: string;
  /** Stops from intensity 0 to 1.0, none for the threshold palette. */
  stops: readonly Rgb[];
};

export const PALETTE_MAX_STOPS = 8;
/** The monochrome palette is white from this intensity, in percent. */
export const MONO_THRESHOLD_PERCENT = 45;
/** B times DISPLAY_GAIN is the display intensity. */
export const DISPLAY_GAIN = 3;

export const PALETTES: readonly Palette[] = [
  {
    id: "green",
    name: "ライム",
    stops: [
      [0, 30, 18],
      [210, 255, 85],
    ],
  },
  {
    id: "blue",
    name: "シアン",
    stops: [
      [0, 0, 45],
      [85, 255, 255],
    ],
  },
  { id: "mono", name: "白黒", stops: [] },
  {
    id: "viridis",
    name: "Viridis",
    stops: [
      [68, 1, 84],
      [70, 50, 126],
      [54, 92, 141],
      [39, 127, 142],
      [31, 161, 135],
      [74, 193, 109],
      [160, 218, 57],
      [253, 231, 37],
    ],
  },
  {
    id: "magma",
    name: "Magma",
    stops: [
      [0, 0, 4],
      [34, 17, 80],
      [95, 24, 127],
      [152, 45, 128],
      [211, 67, 110],
      [248, 118, 92],
      [254, 187, 129],
      [252, 253, 191],
    ],
  },
  {
    id: "plasma",
    name: "Plasma",
    stops: [
      [13, 8, 135],
      [83, 2, 163],
      [139, 10, 165],
      [184, 50, 137],
      [219, 92, 104],
      [244, 136, 73],
      [254, 189, 42],
      [240, 249, 33],
    ],
  },
  {
    id: "inferno",
    name: "Inferno",
    stops: [
      [0, 0, 4],
      [40, 11, 83],
      [101, 21, 110],
      [159, 42, 99],
      [212, 72, 66],
      [245, 125, 21],
      [250, 194, 40],
      [252, 255, 164],
    ],
  },
  {
    id: "cividis",
    name: "Cividis",
    stops: [
      [0, 34, 78],
      [33, 59, 110],
      [76, 85, 108],
      [108, 110, 114],
      [142, 137, 120],
      [177, 165, 112],
      [217, 197, 92],
      [254, 232, 56],
    ],
  },
  {
    id: "turbo",
    name: "Turbo",
    stops: [
      [48, 18, 59],
      [71, 118, 238],
      [27, 208, 213],
      [97, 252, 108],
      [210, 233, 53],
      [254, 155, 45],
      [218, 57, 7],
      [122, 4, 3],
    ],
  },
];

/** The palette index of the custom stops set by rd_palette. */
export const PALETTE_CUSTOM = PALETTES.length;
export const PALETTE_COUNT = PALETTE_CUSTOM + 1;
export const MONO = PALETTES.findIndex((p) => p.id === "mono");

export function paletteIndex(id: string) {
  const index = PALETTES.findIndex((p) => p.id === id);
  if (index < 0) throw new Error(`Unknown palette ${id}`);
  return index;
}

/** Display intensity of a Q24 B value in Q15, as value_rgb computes it:
 * B times DISPLAY_GAIN rounded to Q15, half away from zero, clamped to
 * 1.0. */
export function intensityQ15(value: number) {
  const scaled = value * DISPLAY_GAIN;
  const q =
    scaled < 0
      ? -Math.floor((-scaled + 256) / 512)
      : Math.floor((scaled + 256) / 512);
  return Math.min(q, 32768);
}

/** Color at a Q15 intensity between equally spaced stops, the integer
 * interpolation of value_rgb: channel low + floor(((high - low) f + 2^14)
 * / 2^15) with f the Q15 position inside the segment. */
export function stopColor(stops: readonly Rgb[], intensity: number): Rgb {
  const position = intensity * (stops.length - 1);
  let segment = Math.floor(position / 32768);
  let fraction = position % 32768;
  if (segment === stops.length - 1) {
    segment--;
    fraction = 32768;
  }
  const channel = (c: number) => {
    const low = stops[segment][c];
    const high = stops[segment + 1][c];
    return low + Math.floor(((high - low) * fraction + 16384) / 32768);
  };
  return [channel(0), channel(1), channel(2)];
}

/** A channel rounded to the nearest of the four RGB2 levels. */
export const quantizeChannel = (value: number) =>
  Math.floor((value + 42) / 85) * 85;

/** The color rd_row draws for a Q24 B value with a palette index, and the
 * custom stops for PALETTE_CUSTOM. */
export function paletteColor(
  palette: number,
  value: number,
  quantize: boolean,
  custom: readonly Rgb[] = PALETTES[0].stops
): Rgb {
  const intensity = intensityQ15(value);
  const color: Rgb =
    palette === MONO
      ? intensity * 100 >= MONO_THRESHOLD_PERCENT * 32768
        ? [255, 255, 255]
        : [0, 0, 0]
      : stopColor(
          palette === PALETTE_CUSTOM ? custom : PALETTES[palette].stops,
          intensity
        );
  return quantize
    ? [
        quantizeChannel(color[0]),
        quantizeChannel(color[1]),
        quantizeChannel(color[2]),
      ]
    : color;
}

/** The same mapping for a Float32 intensity in [0, 1], without Q15
 * rounding, for the Float32 reference renderer. */
export function floatColor(palette: number, intensity: number): Rgb {
  if (palette === MONO)
    return intensity >= MONO_THRESHOLD_PERCENT / 100
      ? [255, 255, 255]
      : [0, 0, 0];
  const stops = PALETTES[palette].stops;
  const position = intensity * (stops.length - 1);
  const segment = Math.min(Math.floor(position), stops.length - 2);
  const fraction = position - segment;
  const channel = (c: number) =>
    Math.round(
      stops[segment][c] + (stops[segment + 1][c] - stops[segment][c]) * fraction
    );
  return [channel(0), channel(1), channel(2)];
}

/** The distinct quantized colors a palette shows as the intensity rises,
 * in order, for swatches. */
export function paletteSwatch(stops: readonly Rgb[]): Rgb[] {
  const colors: Rgb[] = [];
  for (let step = 0; step <= 256; step++) {
    const c = stopColor(stops, step * 128);
    const q: Rgb = [
      quantizeChannel(c[0]),
      quantizeChannel(c[1]),
      quantizeChannel(c[2]),
    ];
    const last = colors[colors.length - 1];
    if (!last || last.some((v, i) => v !== q[i])) colors.push(q);
  }
  return colors;
}
