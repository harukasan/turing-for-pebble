/*
 * Shared Gray-Scott reaction-diffusion core, numerical definition version 2.
 *
 * Concentrations are computed in Q24 fixed point (RD_VALUE_ONE is 1.0) and
 * coefficients are Q15 (RD_Q15_ONE is 1.0). Modes 0 and 2 pack both species
 * into one 16-bit word per cell: A as a 7-bit linear code and B as a 9-bit
 * square-root companded code. Mode 1 stores each species as a Q15 word.
 * Every mode writes with Floyd-Steinberg error diffusion, so the rounding
 * error of each cell is carried into its unwritten neighbors instead of being
 * discarded. The packed codes also dither the rounding threshold so that
 * slow fronts are not pinned by the coarse codes; the Q15 codes are fine
 * enough to round to the nearest code. docs/core.md is the numerical
 * contract.
 *
 * The step loop is written for an in-order Cortex-M without FPU: one row of
 * Laplacians is computed per pass with the vertical sums shared between
 * columns, the per-step coefficients live in a stack context so stores
 * through the planes cannot force reloads, and both codecs avoid divisions.
 */
#include "rd.h"
#include <string.h>

/* 16-bit planes per cell: one packed word, or one Q15 word per species. */
#define MODE_PLANES(mode) ((mode) == 1 ? 2 : 1)

/* 'R', 'D', '1', 0x02: marks an initialized State. */
#define STATE_MAGIC 0x52443102u
/* The State is placed on the first 4-byte boundary inside the caller block. */
#define STATE_ALIGNMENT 4

#define SPECIES_COUNT 2
/* Decoded rows per species: previous, current, next, and original first row.
 * The previous row also receives the Laplacian of the current row. */
#define SAVED_ROWS 4
#define BYTES_PER_PIXEL 4

/* Coefficients applied by rd_init until rd_params is called (Q15). */
#define DEFAULT_FEED 950
#define DEFAULT_KILL 1868

/* Initial seeding: disks placed by a 32-bit LCG in display coordinates. */
#define INITIAL_DISKS 24
#define MIN_DISK_RADIUS 4
#define DISK_RADIUS_RANGE 6
#define MAX_SEED_RADIUS 100
#define LCG_MULTIPLIER 1664525u
#define LCG_INCREMENT 1013904223u

/* Packed cell: A occupies the high A_BITS of the word, B the low B_BITS. */
#define A_BITS 7
#define B_BITS 9
/* Linear A: value = code * RD_VALUE_ONE / A_CODE_MAX, code A_CODE_MAX is 1. */
#define A_CODE_MAX ((1 << A_BITS) - 1)
/* Companded B: value = code * code << B_SHIFT, so B_CODE_MAX is just below 1
 * and the code spacing grows with the square root of the concentration. */
#define B_CODE_MAX ((1 << B_BITS) - 1)
#define B_SHIFT (RD_VALUE_BITS - 2 * B_BITS)
#define B_VALUE_MAX ((B_CODE_MAX * B_CODE_MAX) << B_SHIFT)
/* The square root argument value >> B_SHIFT is below 2^ROOT_BITS. */
#define ROOT_BITS (2 * B_BITS)
/* The root table is indexed by the top ROOT_INDEX_BITS bits of the argument
 * normalized into [2^(ROOT_BITS - 2), 2^ROOT_BITS). */
#define ROOT_INDEX_BITS 8
#define ROOT_INDEX_SHIFT (ROOT_BITS - ROOT_INDEX_BITS)

/* Q15 storage: value = code << Q15_SHIFT. */
#define Q15_SHIFT (RD_VALUE_BITS - 15)
#define Q15_CODE_MAX RD_Q15_ONE

/* Seed concentrations A = 0.5 and B = 0.25, as codes of each storage. Codes
 * are written directly, without error diffusion. */
#define PACKED_FULL_A A_CODE_MAX
#define PACKED_SEED_A 64  /* nearest 7-bit code to 0.5 */
#define PACKED_SEED_B 256 /* (256 / 512)^2 is exactly 0.25 */
#define Q15_FULL_A RD_Q15_ONE
#define Q15_SEED_A (RD_Q15_ONE / 2)
#define Q15_SEED_B (RD_Q15_ONE / 4)

#define MAX_STEPS_PER_CALL 1000000

/* Nine-point Laplacian: (4 * axial_sum + diagonal_sum - 20 * center) / 20. */
#define LAPLACIAN_AXIAL_WEIGHT 4
#define LAPLACIAN_SCALE 20

/* Floyd-Steinberg error diffusion weights, in sixteenths: right, below left,
 * below, and the remainder below right. */
#define DIFFUSION_RIGHT 7
#define DIFFUSION_BELOW_LEFT 3
#define DIFFUSION_BELOW 5
#define DIFFUSION_DENOMINATOR 16

/* Rounding threshold dither: a cell rounds up to the next code when its
 * fraction of the code step is at least (DITHER_BASE + random) /
 * DITHER_SCALE, with random in [0, 2^DITHER_BITS), so the threshold is
 * spread uniformly over [1/4, 3/4). */
#define DITHER_BITS 15
#define DITHER_BASE (1 << (DITHER_BITS - 1))
#define DITHER_SCALE (1 << (DITHER_BITS + 1))
#define DITHER_MASK ((1u << DITHER_BITS) - 1)
/* The B random value is taken from the upper half of the 32-bit hash. */
#define DITHER_B_SHIFT 16

/* Bits dropped from a Q54 product to reach Q24: dt (Q15) times a Q39 rate.
 * With dt = 1 the product is skipped and the Q39 rate is rounded by 15. */
#define RATE_SHIFT (2 * 15)
#define UNIT_RATE_SHIFT 15

/* FNV-1a parameters for rd_hash. */
#define FNV_OFFSET_BASIS 2166136261u
#define FNV_PRIME 16777619u

/* Rendering: the B concentration times DISPLAY_GAIN, clamped to 1.0, selects
 * a color between the palette endpoints. Monochrome switches on at
 * MONO_THRESHOLD_PERCENT. Quantized output rounds each channel to a multiple
 * of RGB2_STEP, the RGB2 level spacing. */
#define DISPLAY_GAIN 3
#define MONO_THRESHOLD_PERCENT 45
#define RGB2_STEP 85

/* Display lookup table for rd_row_rgb2: one byte per packed B code, or one
 * per LUT_BUCKET Q15 codes plus one for the code RD_Q15_ONE. A zero entry
 * (alpha 0, never a valid pixel) marks a bucket whose codes differ. */
#define LUT_ENTRIES 513
#define LUT_BYTES 516
#define LUT_BUCKET_BITS 6
#define LUT_BUCKET (1 << LUT_BUCKET_BITS)
#define NO_PALETTE (-1)
/* Opaque alpha of an ARGB8 pixel: a 2 bits, r 2, g 2, b 2. */
#define ARGB8_OPAQUE 0xc0
#define ARGB8_CHANNEL_SHIFT 6

/* Palette endpoints, RGB per palette, at intensity 0 and intensity 1.0. */
static const int PALETTE_LOW[3][3] = {{0, 30, 18}, {0, 0, 45}, {0, 0, 0}};
static const int PALETTE_HIGH[3][3] = {
    {210, 255, 85}, {85, 255, 255}, {255, 255, 255}};

/* decode_a: round(code * 2^24 / 127) for every 7-bit code. */
static const int32_t A_TABLE[A_CODE_MAX + 1] = {
    0,        132104,   264208,   396312,   528416,   660520,   792624,
    924728,   1056833,  1188937,  1321041,  1453145,  1585249,  1717353,
    1849457,  1981561,  2113665,  2245769,  2377873,  2509977,  2642081,
    2774185,  2906289,  3038393,  3170498,  3302602,  3434706,  3566810,
    3698914,  3831018,  3963122,  4095226,  4227330,  4359434,  4491538,
    4623642,  4755746,  4887850,  5019954,  5152058,  5284163,  5416267,
    5548371,  5680475,  5812579,  5944683,  6076787,  6208891,  6340995,
    6473099,  6605203,  6737307,  6869411,  7001515,  7133619,  7265723,
    7397828,  7529932,  7662036,  7794140,  7926244,  8058348,  8190452,
    8322556,  8454660,  8586764,  8718868,  8850972,  8983076,  9115180,
    9247284,  9379388,  9511493,  9643597,  9775701,  9907805,  10039909,
    10172013, 10304117, 10436221, 10568325, 10700429, 10832533, 10964637,
    11096741, 11228845, 11360949, 11493053, 11625158, 11757262, 11889366,
    12021470, 12153574, 12285678, 12417782, 12549886, 12681990, 12814094,
    12946198, 13078302, 13210406, 13342510, 13474614, 13606718, 13738823,
    13870927, 14003031, 14135135, 14267239, 14399343, 14531447, 14663551,
    14795655, 14927759, 15059863, 15191967, 15324071, 15456175, 15588279,
    15720383, 15852488, 15984592, 16116696, 16248800, 16380904, 16513008,
    16645112, 16777216};

/* floor(sqrt(index << ROOT_INDEX_SHIFT)): a lower bound of the square root
 * of every normalized argument whose top bits are index. */
static const uint16_t ROOT_TABLE[1 << ROOT_INDEX_BITS] = {
    0,   32,  45,  55,  64,  71,  78,  84,  90,  96,  101, 106, 110, 115, 119,
    123, 128, 131, 135, 139, 143, 146, 150, 153, 156, 160, 163, 166, 169, 172,
    175, 178, 181, 183, 186, 189, 192, 194, 197, 199, 202, 204, 207, 209, 212,
    214, 217, 219, 221, 224, 226, 228, 230, 232, 235, 237, 239, 241, 243, 245,
    247, 249, 251, 253, 256, 257, 259, 261, 263, 265, 267, 269, 271, 273, 275,
    277, 278, 280, 282, 284, 286, 288, 289, 291, 293, 295, 296, 298, 300, 301,
    303, 305, 306, 308, 310, 311, 313, 315, 316, 318, 320, 321, 323, 324, 326,
    327, 329, 331, 332, 334, 335, 337, 338, 340, 341, 343, 344, 346, 347, 349,
    350, 352, 353, 354, 356, 357, 359, 360, 362, 363, 364, 366, 367, 369, 370,
    371, 373, 374, 375, 377, 378, 379, 381, 382, 384, 385, 386, 387, 389, 390,
    391, 393, 394, 395, 397, 398, 399, 400, 402, 403, 404, 406, 407, 408, 409,
    411, 412, 413, 414, 416, 417, 418, 419, 420, 422, 423, 424, 425, 426, 428,
    429, 430, 431, 432, 434, 435, 436, 437, 438, 439, 441, 442, 443, 444, 445,
    446, 448, 449, 450, 451, 452, 453, 454, 455, 457, 458, 459, 460, 461, 462,
    463, 464, 465, 467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477, 478,
    480, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493, 494,
    495, 496, 497, 498, 499, 500, 501, 502, 503, 504, 505, 506, 507, 508, 509,
    510};

/*
 * Control structure at the start of the aligned block. The caller block is
 * laid out as:
 *
 *   [<= 3 bytes padding][State][plane 0][plane 1, mode 1 only]
 *   [saved rows: SAVED_ROWS x SPECIES_COUNT x width int32]
 *   [residual rows: SPECIES_COUNT x width int32]
 *   [output row, RD_ROW_BYTES][display lookup table, LUT_BYTES]
 *
 * rd_memory reports the same bytes as an accounting breakdown by component,
 * which is not the physical order. Row buffers cover the saved rows and the
 * residual rows; the rendering component covers the output row and the
 * lookup table.
 */
typedef struct {
  uint32_t magic, seed, step;
  int width, height, packed, feed, kill, da, db, dt;
  /* Palette the display lookup table was built for, or NO_PALETTE. */
  int lut_palette;
} State;

/* Everything the cell loop reads, copied out of the State once per step so
 * that the stores into the planes and residual rows cannot alias it. */
/* Error diffusion shares held between cells of the row being written: the
 * right share of the previous cell, the below-right share pending for the
 * next column, and the below-left share of column 0 that wraps to the last
 * column. The row loop keeps a copy in registers. */
typedef struct {
  int32_t carry, pending, wrap;
} Shares;

typedef struct {
  int packed, width, unit_dt;
  int32_t feed, decay, da, db, dt;
  /* Hash of seed and step, mixed into every cell's dither. */
  uint32_t step_salt;
  int32_t *restrict residual[SPECIES_COUNT];
  Shares shares[SPECIES_COUNT];
} StepContext;

static int mode_supported(int mode) {
#ifdef RD_MODE
  return mode == RD_MODE;
#else
  return mode >= 0 && mode <= 2;
#endif
}

static int grid_width(int mode) {
  return mode == 0 ? RD_DISPLAY_WIDTH : RD_DISPLAY_WIDTH / 2;
}

static int grid_height(int width) {
  return width * RD_DISPLAY_HEIGHT / RD_DISPLAY_WIDTH;
}

/* Whether both species share one packed word. A Pebble build fixes the mode
 * at compile time, so this folds to a constant there. */
static inline int is_packed(const State *state) {
#ifdef RD_MODE
  (void)state;
  return MODE_PLANES(RD_MODE) == 1;
#else
  return state->packed;
#endif
}

static inline int ctx_packed(const StepContext *ctx) {
#ifdef RD_MODE
  (void)ctx;
  return MODE_PLANES(RD_MODE) == 1;
#else
  return ctx->packed;
#endif
}

static inline int planes(const State *state) {
  return is_packed(state) ? 1 : SPECIES_COUNT;
}

size_t rd_memory(int mode, int component) {
  if (!mode_supported(mode)) {
    return 0;
  }
  int width = grid_width(mode);
  int height = grid_height(width);
  switch (component) {
  case RD_COMPONENT_FIELDS:
    return (size_t)width * height * MODE_PLANES(mode) * sizeof(uint16_t);
  case RD_COMPONENT_ROW_BUFFERS:
    return (size_t)width * SPECIES_COUNT * (SAVED_ROWS + 1) * sizeof(int32_t);
  case RD_COMPONENT_CONTROL:
    return sizeof(State);
  case RD_COMPONENT_OUTPUT_ROW:
    return RD_ROW_BYTES + LUT_BYTES;
  case RD_COMPONENT_ALIGNMENT:
    return STATE_ALIGNMENT - 1;
  default:
    return 0;
  }
}

size_t rd_bytes(int mode) {
  size_t total = 0;
  for (int component = 0; component < RD_COMPONENT_COUNT; component++) {
    total += rd_memory(mode, component);
  }
  return total;
}

/* Stored plane: the packed plane, or plane `species` in mode 1. */
static uint16_t *plane(State *state, int index) {
  return (uint16_t *)(state + 1) + (size_t)index * state->width * state->height;
}

/* Decoded Q24 rows, after the planes: SAVED_ROWS rows per species. */
static int32_t *saved_rows(State *state) {
  return (int32_t *)plane(state, planes(state));
}

/* Error diffusion residual row of one species, after the saved rows. */
static int32_t *residual_row(State *state, int species) {
  return saved_rows(state) +
         (SAVED_ROWS * SPECIES_COUNT + species) * state->width;
}

/* RGBA output row, after the residual rows. */
static uint8_t *output_row(State *state) {
  return (uint8_t *)residual_row(state, SPECIES_COUNT);
}

/* Display lookup table, after the output row. */
static uint8_t *lookup_table(State *state) {
  return output_row(state) + RD_ROW_BYTES;
}

/* Round a value to the nearest multiple of 2^bits, halfway values away from
 * zero, and drop those bits. */
static int64_t round_shift(int64_t value, int bits) {
  int64_t half = (int64_t)1 << (bits - 1);
  return value < 0 ? -((-value + half) >> bits) : (value + half) >> bits;
}

/* The same rounding for a non-negative value, without the sign test. */
static inline uint64_t round_shift_unsigned(uint64_t value, int bits) {
  return (value + ((uint64_t)1 << (bits - 1))) >> bits;
}

/* Integer square root of a value below 2^ROOT_BITS, rounded down: the
 * argument is normalized by an even shift, ROOT_TABLE gives a lower bound
 * from its top bits, and at most two increments reach the floor. */
static uint32_t isqrt(uint32_t value) {
  if (value == 0) {
    return 0;
  }
  /* Bits of value are 32 - clz; shifting by an even amount up to ROOT_BITS
   * bits keeps the root a power of two multiple. */
  int shift = ((int)__builtin_clz(value) - (32 - ROOT_BITS)) & ~1;
  uint32_t root =
      ROOT_TABLE[(value << shift) >> ROOT_INDEX_SHIFT] >> (shift / 2);
  while ((root + 1) * (root + 1) <= value) {
    root++;
  }
  return root;
}

/* 32-bit integer mixing function (lowbias32) for the rounding dither. */
static uint32_t mix32(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  return value ^ (value >> 16);
}

/* Decoding a code of each storage to Q24. */
static inline int32_t decode_a(unsigned code) { return A_TABLE[code]; }

static inline int32_t decode_b(unsigned code) {
  return (int32_t)(code * code) << B_SHIFT;
}

static inline int32_t decode_q15(unsigned code) {
  return (int32_t)code << Q15_SHIFT;
}

/* The largest code whose decoded value is at most the Q24 value, which must
 * be within [0, largest decoded value]. */
static inline unsigned floor_a(int32_t value) {
  unsigned code = ((uint32_t)value * A_CODE_MAX) >> RD_VALUE_BITS;
  /* The product truncates before the decode rounds, so the next code can
   * still decode within value. */
  if (code < A_CODE_MAX && decode_a(code + 1) <= value) {
    code++;
  }
  return code;
}

static inline unsigned floor_b(int32_t value) {
  return isqrt((uint32_t)value >> B_SHIFT);
}

static inline unsigned floor_q15(int32_t value) {
  return (unsigned)value >> Q15_SHIFT;
}

/* Largest code and value of one species. */
static inline unsigned code_limit(int packed, int species) {
  if (!packed) {
    return Q15_CODE_MAX;
  }
  return species == RD_SPECIES_A ? A_CODE_MAX : B_CODE_MAX;
}

static inline int32_t value_limit(int packed, int species) {
  return packed && species == RD_SPECIES_B ? B_VALUE_MAX : RD_VALUE_ONE;
}

static inline int32_t decode(int packed, int species, unsigned code) {
  if (!packed) {
    return decode_q15(code);
  }
  return species == RD_SPECIES_A ? decode_a(code) : decode_b(code);
}

static inline unsigned floor_code(int packed, int species, int32_t value) {
  if (!packed) {
    return floor_q15(value);
  }
  return species == RD_SPECIES_A ? floor_a(value) : floor_b(value);
}

/* Read the codes of one cell. */
static void load_codes(State *state, int cell_index, unsigned *code_a,
                       unsigned *code_b) {
  if (is_packed(state)) {
    unsigned word = plane(state, 0)[cell_index];
    *code_a = word >> B_BITS;
    *code_b = word & B_CODE_MAX;
  } else {
    *code_a = plane(state, RD_SPECIES_A)[cell_index];
    *code_b = plane(state, RD_SPECIES_B)[cell_index];
  }
}

/* Write the codes of one cell. */
static void store_codes(State *state, int cell_index, unsigned code_a,
                        unsigned code_b) {
  if (is_packed(state)) {
    plane(state, 0)[cell_index] = (uint16_t)((code_a << B_BITS) | code_b);
  } else {
    plane(state, RD_SPECIES_A)[cell_index] = (uint16_t)code_a;
    plane(state, RD_SPECIES_B)[cell_index] = (uint16_t)code_b;
  }
}

/* Decode a stored row into Q24 rows of both species. */
static void decode_row(State *state, int y, int32_t *restrict row_a,
                       int32_t *restrict row_b) {
  int width = state->width;
  if (is_packed(state)) {
    const uint16_t *cells = plane(state, 0) + (size_t)y * width;
    for (int x = 0; x < width; x++) {
      unsigned word = cells[x];
      row_a[x] = decode_a(word >> B_BITS);
      row_b[x] = decode_b(word & B_CODE_MAX);
    }
  } else {
    const uint16_t *cells_a = plane(state, RD_SPECIES_A) + (size_t)y * width;
    const uint16_t *cells_b = plane(state, RD_SPECIES_B) + (size_t)y * width;
    for (int x = 0; x < width; x++) {
      row_a[x] = decode_q15(cells_a[x]);
      row_b[x] = decode_q15(cells_b[x]);
    }
  }
}

/* The State behind a public handle, or NULL if it is not initialized. */
static State *checked_state(void *handle) {
  State *state = handle;
  return state && state->magic == STATE_MAGIC ? state : NULL;
}

int rd_params(void *handle, int feed, int kill, int da, int db, int dt) {
  State *state = checked_state(handle);
  if (!state || feed < 0 || feed > RD_Q15_ONE || kill < 0 ||
      kill > RD_Q15_ONE || da < 0 || da > RD_Q15_ONE || db < 0 ||
      db > RD_Q15_ONE || dt < 0 || dt > RD_Q15_ONE) {
    return -1;
  }
  state->feed = feed;
  state->kill = kill;
  state->da = da;
  state->db = db;
  state->dt = dt;
  return 0;
}

/* Shortest signed distance along one periodic display axis. */
static int wrap_delta(int delta, int size) {
  if (delta > size / 2) {
    delta -= size;
  }
  if (delta < -size / 2) {
    delta += size;
  }
  return delta;
}

/* Seed a disk given in display coordinates: A = 0.5 and B = 0.25 inside it.
 * Each grid cell samples the display coordinate it covers. */
int rd_seed(void *handle, int x, int y, int radius) {
  State *state = checked_state(handle);
  if (!state || x < 0 || x >= RD_DISPLAY_WIDTH || y < 0 ||
      y >= RD_DISPLAY_HEIGHT || radius < 1 || radius > MAX_SEED_RADIUS) {
    return -1;
  }
  int packed = is_packed(state);
  for (int grid_y = 0; grid_y < state->height; grid_y++) {
    for (int grid_x = 0; grid_x < state->width; grid_x++) {
      int display_x = grid_x * RD_DISPLAY_WIDTH / state->width;
      int display_y = grid_y * RD_DISPLAY_HEIGHT / state->height;
      int dx = wrap_delta(display_x - x, RD_DISPLAY_WIDTH);
      int dy = wrap_delta(display_y - y, RD_DISPLAY_HEIGHT);
      if (dx * dx + dy * dy <= radius * radius) {
        store_codes(state, grid_y * state->width + grid_x,
                    packed ? PACKED_SEED_A : Q15_SEED_A,
                    packed ? PACKED_SEED_B : Q15_SEED_B);
      }
    }
  }
  return 0;
}

static uint32_t lcg_next(uint32_t *lcg) {
  *lcg = *lcg * LCG_MULTIPLIER + LCG_INCREMENT;
  return *lcg;
}

/* Scale a 32-bit random value into [0, range). */
static int random_below(uint32_t random, int range) {
  return (int)(((uint64_t)random * range) >> 32);
}

/* Initialize the block to the equilibrium A = 1, B = 0, then seed
 * INITIAL_DISKS disks at LCG-chosen display positions and radii. */
void *rd_init(void *memory, size_t bytes, int mode, uint32_t seed) {
  size_t required = rd_bytes(mode);
  if (!memory || required == 0 || bytes < required) {
    return NULL;
  }
  /* Align up: the State starts at most STATE_ALIGNMENT - 1 bytes into the
   * block, and everything after it is exactly the other components. */
  State *state = (State *)(((uintptr_t)memory + STATE_ALIGNMENT - 1) &
                           ~(uintptr_t)(STATE_ALIGNMENT - 1));
  memset(state, 0, required - (STATE_ALIGNMENT - 1));
  state->magic = STATE_MAGIC;
  state->seed = seed;
  state->width = grid_width(mode);
  state->height = grid_height(state->width);
  state->packed = MODE_PLANES(mode) == 1;
  state->lut_palette = NO_PALETTE;
  rd_params(state, DEFAULT_FEED, DEFAULT_KILL, RD_Q15_ONE, RD_Q15_ONE / 2,
            RD_Q15_ONE);
  for (int i = 0; i < state->width * state->height; i++) {
    store_codes(state, i, is_packed(state) ? PACKED_FULL_A : Q15_FULL_A, 0);
  }
  uint32_t lcg = seed;
  for (int i = 0; i < INITIAL_DISKS; i++) {
    int x = random_below(lcg_next(&lcg), RD_DISPLAY_WIDTH);
    int y = random_below(lcg_next(&lcg), RD_DISPLAY_HEIGHT);
    int radius =
        MIN_DISK_RADIUS + random_below(lcg_next(&lcg), DISK_RADIUS_RANGE);
    rd_seed(state, x, y, radius);
  }
  return state;
}

/* Round a Laplacian sum times LAPLACIAN_SCALE to the nearest Laplacian,
 * halfway values away from zero. C division truncates toward zero, so
 * adding half the scale with the sign of the sum is that rounding. */
static inline int32_t round_laplacian(int32_t sum) {
  return (sum + (sum < 0 ? -LAPLACIAN_SCALE / 2 : LAPLACIAN_SCALE / 2)) /
         LAPLACIAN_SCALE;
}

/* Nine-point Laplacian of one species for a whole row, from the decoded
 * rows above, at, and below it, written over the row above, which is not
 * read again before it is decoded anew. With S[x] = up[x] + down[x] the
 * weighted sum is 4 S[x] + S[x-1] + S[x+1] + 4 (cur[x-1] + cur[x+1]) -
 * 20 cur[x], the same integers as the axial and diagonal sums, so the
 * exact rational weights still keep a uniform field exactly uniform. */
static void laplacian_row(int32_t *restrict up, const int32_t *restrict cur,
                          const int32_t *restrict down, int width) {
  int last = width - 1;
  int32_t s_first = up[0] + down[0], c_first = cur[0];
  int32_t s_prev = up[last] + down[last], c_prev = cur[last];
  int32_t s_cur = s_first, c_cur = c_first;
  for (int x = 0; x < last; x++) {
    int32_t s_next = up[x + 1] + down[x + 1], c_next = cur[x + 1];
    up[x] = round_laplacian(LAPLACIAN_AXIAL_WEIGHT * (s_cur + c_prev + c_next) +
                            s_prev + s_next - LAPLACIAN_SCALE * c_cur);
    s_prev = s_cur;
    c_prev = c_cur;
    s_cur = s_next;
    c_cur = c_next;
  }
  up[last] =
      round_laplacian(LAPLACIAN_AXIAL_WEIGHT * (s_cur + c_prev + c_first) +
                      s_prev + s_first - LAPLACIAN_SCALE * c_cur);
}

/* One explicit Euler step of the Gray-Scott update for a single cell, from
 * and to Q24 concentrations, before clamping:
 *   A' = A + dt * (Da * lap(A) - A * B^2 + feed * (1 - A))
 *   B' = B + dt * (Db * lap(B) + A * B^2 - (feed + kill) * B)
 * Rates are accumulated as Q39 (Q15 coefficient times Q24 concentration)
 * and rounded once after the dt product. The concentrations are never
 * negative, so their products round without a sign test. The coefficients
 * are passed by value so the row loop keeps them in registers. */
static inline void react_cell(int32_t a, int32_t b, int32_t lap_a,
                              int32_t lap_b, int32_t feed, int32_t decay,
                              int32_t da, int32_t db, int32_t dt, int unit_dt,
                              int32_t *next_a, int32_t *next_b) {
  uint64_t ab = round_shift_unsigned((uint64_t)a * (uint64_t)b, RD_VALUE_BITS);
  int64_t reaction =
      (int64_t)round_shift_unsigned(ab * (uint64_t)b, RD_VALUE_BITS) << 15;
  int64_t rate_a =
      (int64_t)da * lap_a - reaction + (int64_t)feed * (RD_VALUE_ONE - a);
  int64_t rate_b = (int64_t)db * lap_b + reaction - (int64_t)decay * b;
  if (unit_dt) {
    /* rate * 2^15 rounded by 30 bits is rate rounded by 15 bits. */
    *next_a = a + (int32_t)round_shift(rate_a, UNIT_RATE_SHIFT);
    *next_b = b + (int32_t)round_shift(rate_b, UNIT_RATE_SHIFT);
  } else {
    *next_a = a + (int32_t)round_shift(rate_a * dt, RATE_SHIFT);
    *next_b = b + (int32_t)round_shift(rate_b * dt, RATE_SHIFT);
  }
}

/* Fill the step context and derive the dither salt before a step. The
 * residual rows themselves persist: after the last row they hold the shares
 * for row 0 of the next step. */
static void begin_step(State *state, StepContext *ctx) {
  ctx->packed = is_packed(state);
  ctx->width = state->width;
  ctx->feed = state->feed;
  ctx->decay = state->kill + state->feed;
  ctx->da = state->da;
  ctx->db = state->db;
  ctx->dt = state->dt;
  ctx->unit_dt = state->dt == RD_Q15_ONE;
  ctx->step_salt = mix32(state->seed ^ mix32(state->step));
  for (int species = 0; species < SPECIES_COUNT; species++) {
    ctx->residual[species] = residual_row(state, species);
    ctx->shares[species].carry = ctx->shares[species].pending =
        ctx->shares[species].wrap = 0;
  }
}

/* Dither bits of one cell: the low DITHER_BITS are for A and the bits from
 * DITHER_B_SHIFT for B. */
static inline uint32_t cell_dither(uint32_t step_salt, int cell_index) {
  return mix32((uint32_t)cell_index ^ step_salt);
}

/* Whether the fraction (value - low) / (high - low) of a code step is at
 * least the dithered threshold. The packed A step is the only one whose
 * products need 64 bits. */
static inline int rounds_up(int packed, int species, int32_t value, int32_t low,
                            int32_t high, uint32_t random) {
  if (packed && species == RD_SPECIES_A) {
    return (int64_t)(value - low) * DITHER_SCALE >=
           (int64_t)(high - low) * (int64_t)(DITHER_BASE + random);
  }
  return (uint32_t)(value - low) * DITHER_SCALE >=
         (uint32_t)(high - low) * (DITHER_BASE + random);
}

/* Encode the value of one species at column x of the row being written,
 * with Floyd-Steinberg error diffusion and, for packed codes, a dithered
 * rounding threshold from random, in [0, 2^DITHER_BITS). The residual row
 * holds the shares
 * this row received from the row above; each column is consumed and then
 * reused for the shares given to the row below. Cells are encoded in row
 * order, each row from column 0 up, with finish_row after each row. */
static inline unsigned encode_cell(int packed, int species,
                                   int32_t *restrict residual, Shares *shares,
                                   int x, int32_t value, uint32_t random) {
  value += residual[x] + shares->carry;
  int32_t limit = value_limit(packed, species);
  if (value < 0) {
    value = 0;
  }
  if (value > limit) {
    value = limit;
  }
  unsigned code = floor_code(packed, species, value);
  int32_t low = decode(packed, species, code);
  if (code < code_limit(packed, species)) {
    int32_t high = decode(packed, species, code + 1);
    /* Packed codes use the dithered threshold; Q15 codes round to the
     * nearest code, halfway upward. */
    int up = packed ? rounds_up(packed, species, value, low, high, random)
                    : (value - low) * 2 >= high - low;
    if (up) {
      code++;
      low = high;
    }
  }
  int32_t error = value - low;
  int32_t right = error * DIFFUSION_RIGHT / DIFFUSION_DENOMINATOR;
  int32_t below_left = error * DIFFUSION_BELOW_LEFT / DIFFUSION_DENOMINATOR;
  int32_t below = error * DIFFUSION_BELOW / DIFFUSION_DENOMINATOR;
  shares->carry = right;
  residual[x] = shares->pending + below;
  if (x > 0) {
    residual[x - 1] += below_left;
  } else {
    shares->wrap = below_left;
  }
  shares->pending = error - right - below_left - below;
  return code;
}

/* Deliver the shares that wrapped around the periodic row ends. The right
 * share of the last column carries into column 0 of the next row. */
static void finish_row(StepContext *ctx) {
  for (int species = 0; species < SPECIES_COUNT; species++) {
    int32_t *residual = ctx->residual[species];
    residual[ctx->width - 1] += ctx->shares[species].wrap;
    residual[0] += ctx->shares[species].pending;
    ctx->shares[species].wrap = ctx->shares[species].pending = 0;
  }
}

/* Advance the field in place. Each species keeps four decoded rows so that
 * neighbors still see the old values of rows that were already rewritten. */
int rd_step(void *handle, int count) {
  State *state = checked_state(handle);
  if (!state || count < 0 || count > MAX_STEPS_PER_CALL) {
    return -1;
  }
  int width = state->width, height = state->height;
  size_t row_bytes = (size_t)width * sizeof(int32_t);
  int32_t *rows = saved_rows(state);
  /* The planes and coefficients are hoisted so that the residual stores in
   * the cell loop cannot force them to be reloaded. */
  uint16_t *restrict cells_a = plane(state, 0);
  uint16_t *restrict cells_b = is_packed(state) ? cells_a : plane(state, 1);
  StepContext ctx;
  for (int iteration = 0; iteration < count; iteration++) {
    /* prev, cur, and next hold the old rows above, at, and below the row
     * being updated. first keeps the original row 0, which is the periodic
     * neighbor below the last row after row 0 has been rewritten. */
    int32_t *prev[SPECIES_COUNT], *cur[SPECIES_COUNT], *next[SPECIES_COUNT],
        *first[SPECIES_COUNT];
    for (int species = 0; species < SPECIES_COUNT; species++) {
      prev[species] = rows + (0 * SPECIES_COUNT + species) * width;
      cur[species] = rows + (1 * SPECIES_COUNT + species) * width;
      next[species] = rows + (2 * SPECIES_COUNT + species) * width;
      first[species] = rows + (3 * SPECIES_COUNT + species) * width;
    }
    decode_row(state, height - 1, prev[0], prev[1]);
    decode_row(state, 0, cur[0], cur[1]);
    memcpy(first[0], cur[0], row_bytes);
    memcpy(first[1], cur[1], row_bytes);
    begin_step(state, &ctx);
    for (int y = 0; y < height; y++) {
      if (y == height - 1) {
        memcpy(next[0], first[0], row_bytes);
        memcpy(next[1], first[1], row_bytes);
      } else {
        decode_row(state, y + 1, next[0], next[1]);
      }
      /* The rows above are consumed into the Laplacians in place. */
      laplacian_row(prev[0], cur[0], next[0], width);
      laplacian_row(prev[1], cur[1], next[1], width);
      const int32_t *restrict lap_a = prev[0], *restrict lap_b = prev[1];
      const int32_t *restrict cur_a = cur[0], *restrict cur_b = cur[1];
      int32_t *restrict res_a = ctx.residual[0], *restrict res_b =
                                                     ctx.residual[1];
      Shares shares_a = ctx.shares[0], shares_b = ctx.shares[1];
      const int packed = ctx_packed(&ctx);
      const int32_t feed = ctx.feed, decay = ctx.decay, da = ctx.da,
                    db = ctx.db, dt = ctx.dt;
      const int unit_dt = ctx.unit_dt;
      const uint32_t step_salt = ctx.step_salt;
      int row_index = y * width;
      for (int x = 0; x < width; x++) {
        int32_t next_a, next_b;
        uint32_t dither = packed ? cell_dither(step_salt, row_index + x) : 0;
        react_cell(cur_a[x], cur_b[x], lap_a[x], lap_b[x], feed, decay, da, db,
                   dt, unit_dt, &next_a, &next_b);
        unsigned code_a = encode_cell(packed, RD_SPECIES_A, res_a, &shares_a, x,
                                      next_a, dither & DITHER_MASK);
        unsigned code_b =
            encode_cell(packed, RD_SPECIES_B, res_b, &shares_b, x, next_b,
                        (dither >> DITHER_B_SHIFT) & DITHER_MASK);
        if (packed) {
          cells_a[row_index + x] = (uint16_t)((code_a << B_BITS) | code_b);
        } else {
          cells_a[row_index + x] = (uint16_t)code_a;
          cells_b[row_index + x] = (uint16_t)code_b;
        }
      }
      ctx.shares[0] = shares_a;
      ctx.shares[1] = shares_b;
      finish_row(&ctx);
      /* Rotate: the row just finished becomes the row above, and the buffer
       * that held the old row above is reused for the next row below. */
      for (int species = 0; species < SPECIES_COUNT; species++) {
        int32_t *tmp = prev[species];
        prev[species] = cur[species];
        cur[species] = next[species];
        next[species] = tmp;
      }
    }
    state->step++;
  }
  return 0;
}

int rd_get(void *handle, int x, int y, int species) {
  State *state = checked_state(handle);
  if (!state || x < 0 || x >= state->width || y < 0 || y >= state->height ||
      species < 0 || species >= SPECIES_COUNT) {
    return -1;
  }
  unsigned code_a, code_b;
  load_codes(state, y * state->width + x, &code_a, &code_b);
  return decode(is_packed(state), species,
                species == RD_SPECIES_A ? code_a : code_b);
}

uint32_t rd_steps(void *handle) {
  State *state = checked_state(handle);
  return state ? state->step : 0;
}

/* FNV-1a over the stored words as canonical little-endian 16-bit values, so
 * native and Wasm builds of the same mode produce comparable hashes. The
 * residual rows are not part of the hash. */
uint32_t rd_hash(void *handle) {
  State *state = checked_state(handle);
  if (!state) {
    return 0;
  }
  uint32_t hash = FNV_OFFSET_BASIS;
  for (int index = 0; index < planes(state); index++) {
    for (int i = 0; i < state->width * state->height; i++) {
      unsigned value = plane(state, index)[i];
      hash = (hash ^ (value & 0xff)) * FNV_PRIME;
      hash = (hash ^ (value >> 8)) * FNV_PRIME;
    }
  }
  return hash;
}

/* The three color channels of one B code: intensity is B times
 * DISPLAY_GAIN clamped to 1.0, mapped between the palette endpoints, or a
 * monochrome threshold, and optionally quantized to RGB2 levels. */
static void pixel_rgb(const State *state, int palette, int quantize,
                      unsigned code_b, uint8_t rgb[3]) {
  int intensity = (int)round_shift(
      (int64_t)decode(is_packed(state), RD_SPECIES_B, code_b) * DISPLAY_GAIN,
      Q15_SHIFT);
  if (intensity > RD_Q15_ONE) {
    intensity = RD_Q15_ONE;
  }
  for (int channel = 0; channel < 3; channel++) {
    int color;
    if (palette == RD_PALETTE_MONO) {
      color = intensity * 100 >= MONO_THRESHOLD_PERCENT * RD_Q15_ONE ? 255 : 0;
    } else {
      int low = PALETTE_LOW[palette][channel];
      int high = PALETTE_HIGH[palette][channel];
      color = low + ((high - low) * intensity + RD_Q15_ONE / 2) / RD_Q15_ONE;
    }
    rgb[channel] =
        (uint8_t)(quantize ? (color + RGB2_STEP / 2) / RGB2_STEP * RGB2_STEP
                           : color);
  }
}

/* The quantized color of one B code as an opaque ARGB8 byte. */
static uint8_t pixel_argb8(const State *state, int palette, unsigned code_b) {
  uint8_t rgb[3];
  pixel_rgb(state, palette, 1, code_b, rgb);
  return (uint8_t)(ARGB8_OPAQUE | (rgb[0] >> ARGB8_CHANNEL_SHIFT) << 4 |
                   (rgb[1] >> ARGB8_CHANNEL_SHIFT) << 2 |
                   (rgb[2] >> ARGB8_CHANNEL_SHIFT));
}

/* Build the display lookup table for a palette unless it is current. */
static void ensure_lookup_table(State *state, int palette) {
  if (state->lut_palette == palette) {
    return;
  }
  uint8_t *table = lookup_table(state);
  if (is_packed(state)) {
    for (unsigned code = 0; code <= B_CODE_MAX; code++) {
      table[code] = pixel_argb8(state, palette, code);
    }
  } else {
    for (unsigned bucket = 0; bucket < LUT_ENTRIES; bucket++) {
      unsigned first = bucket << LUT_BUCKET_BITS;
      unsigned last = first + LUT_BUCKET - 1;
      if (last > Q15_CODE_MAX) {
        last = Q15_CODE_MAX;
      }
      uint8_t value = pixel_argb8(state, palette, first);
      for (unsigned code = first + 1; code <= last; code++) {
        if (pixel_argb8(state, palette, code) != value) {
          value = 0;
          break;
        }
      }
      table[bucket] = value;
    }
  }
  state->lut_palette = palette;
}

/* Render display row y into the shared RGBA output row. Each display pixel
 * samples the grid cell that covers it. */
uint8_t *rd_row(void *handle, int y, int palette, int quantize) {
  State *state = checked_state(handle);
  if (!state || y < 0 || y >= RD_DISPLAY_HEIGHT || palette < 0 ||
      palette > RD_PALETTE_MONO || (quantize != 0 && quantize != 1)) {
    return NULL;
  }
  uint8_t *output = output_row(state);
  int grid_y = y * state->height / RD_DISPLAY_HEIGHT;
  for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
    int grid_x = x * state->width / RD_DISPLAY_WIDTH;
    unsigned code_a, code_b;
    load_codes(state, grid_y * state->width + grid_x, &code_a, &code_b);
    pixel_rgb(state, palette, quantize, code_b, output + x * BYTES_PER_PIXEL);
    output[x * BYTES_PER_PIXEL + 3] = 255; /* opaque alpha */
  }
  return output;
}

/* Render display row y as quantized ARGB8 bytes into the shared output row,
 * the value rd_row with quantize = 1 gives after packing each channel's top
 * two bits. Grid cells map to display pixels by a shift, because a grid
 * axis is the display axis or half of it. */
uint8_t *rd_row_rgb2(void *handle, int y, int palette) {
  State *state = checked_state(handle);
  if (!state || y < 0 || y >= RD_DISPLAY_HEIGHT || palette < 0 ||
      palette > RD_PALETTE_MONO) {
    return NULL;
  }
  ensure_lookup_table(state, palette);
  uint8_t *output = output_row(state);
  const uint8_t *table = lookup_table(state);
  int shift = state->width == RD_DISPLAY_WIDTH ? 0 : 1;
  int grid_y = y >> shift;
  if (is_packed(state)) {
    const uint16_t *cells = plane(state, 0) + (size_t)grid_y * state->width;
    for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
      output[x] = table[cells[x >> shift] & B_CODE_MAX];
    }
  } else {
    const uint16_t *cells =
        plane(state, RD_SPECIES_B) + (size_t)grid_y * state->width;
    for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
      unsigned code = cells[x >> shift];
      uint8_t value = table[code >> LUT_BUCKET_BITS];
      output[x] = value ? value : pixel_argb8(state, palette, code);
    }
  }
  return output;
}
