/*
 * Shared Gray-Scott reaction-diffusion core, numerical definition version 2.
 *
 * Concentrations are computed in Q24 fixed point (RD_VALUE_ONE is 1.0) and
 * coefficients are Q15 (RD_Q15_ONE is 1.0). Modes 0 and 2 pack both species
 * into one 16-bit word per cell: A as a 7-bit linear code and B as a 9-bit
 * square-root companded code. Mode 1 stores each species as a Q15 word.
 * Every mode writes with Floyd-Steinberg error diffusion, so the rounding
 * error of each cell is carried into its unwritten neighbors instead of being
 * discarded, and a dithered rounding threshold keeps slow fronts from being
 * pinned by the codes. docs/core.md is the numerical contract.
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
/* Decoded rows per species: previous, current, next, and original first row. */
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

/* Bits dropped from a Q54 product to reach Q24: dt (Q15) times a Q39 rate. */
#define RATE_SHIFT (2 * 15)

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

/* Palette endpoints, RGB per palette, at intensity 0 and intensity 1.0. */
static const int PALETTE_LOW[3][3] = {{0, 30, 18}, {0, 0, 45}, {0, 0, 0}};
static const int PALETTE_HIGH[3][3] = {
    {210, 255, 85}, {85, 255, 255}, {255, 255, 255}};

/*
 * Control structure at the start of the aligned block. The caller block is
 * laid out as:
 *
 *   [<= 3 bytes padding][State][plane 0][plane 1, mode 1 only]
 *   [saved rows: SAVED_ROWS x SPECIES_COUNT x width int32]
 *   [residual rows: SPECIES_COUNT x width int32]
 *   [output row, RD_ROW_BYTES]
 *
 * rd_memory reports the same bytes as an accounting breakdown by component,
 * which is not the physical order. Row buffers cover the saved rows and the
 * residual rows.
 */
typedef struct {
  uint32_t magic, seed, step;
  /* Hash of seed and step, mixed into every cell's dither during a step. */
  uint32_t step_salt;
  int width, height, packed, feed, kill, da, db, dt;
  /* Error diffusion shares held between cells of the row being written:
   * the right share of the previous cell, the below-right share pending for
   * the next column, and the below-left share of column 0 that wraps to the
   * last column. */
  int32_t carry[SPECIES_COUNT], pending[SPECIES_COUNT], wrap[SPECIES_COUNT];
} State;

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
    return RD_ROW_BYTES;
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

/* Round a value to the nearest multiple of 2^bits, halfway values away from
 * zero, and drop those bits. */
static int64_t round_shift(int64_t value, int bits) {
  int64_t half = (int64_t)1 << (bits - 1);
  return value < 0 ? -((-value + half) >> bits) : (value + half) >> bits;
}

/* Integer square root of a value below 2^18, rounded down. */
static uint32_t isqrt(uint32_t value) {
  uint32_t result = 0, bit = 1u << 16;
  while (bit > value) {
    bit >>= 2;
  }
  while (bit) {
    if (value >= result + bit) {
      value -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }
  return result;
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
static int32_t decode_a(unsigned code) {
  return (int32_t)((((uint32_t)code << RD_VALUE_BITS) + A_CODE_MAX / 2) /
                   A_CODE_MAX);
}

static int32_t decode_b(unsigned code) {
  return (int32_t)(code * code) << B_SHIFT;
}

static int32_t decode_q15(unsigned code) { return (int32_t)code << Q15_SHIFT; }

/* The largest code whose decoded value is at most the Q24 value, which must
 * be within [0, largest decoded value]. */
static unsigned floor_a(int32_t value) {
  unsigned code = ((uint32_t)value * A_CODE_MAX) >> RD_VALUE_BITS;
  /* The product truncates before the decode rounds, so the next code can
   * still decode within value. */
  if (code < A_CODE_MAX && decode_a(code + 1) <= value) {
    code++;
  }
  return code;
}

static unsigned floor_b(int32_t value) {
  return isqrt((uint32_t)value >> B_SHIFT);
}

static unsigned floor_q15(int32_t value) {
  return (unsigned)value >> Q15_SHIFT;
}

/* Largest code and value of one species. */
static unsigned code_limit(const State *state, int species) {
  if (!is_packed(state)) {
    return Q15_CODE_MAX;
  }
  return species == RD_SPECIES_A ? A_CODE_MAX : B_CODE_MAX;
}

static int32_t value_limit(const State *state, int species) {
  return is_packed(state) && species == RD_SPECIES_B ? B_VALUE_MAX
                                                     : RD_VALUE_ONE;
}

static int32_t decode(const State *state, int species, unsigned code) {
  if (!is_packed(state)) {
    return decode_q15(code);
  }
  return species == RD_SPECIES_A ? decode_a(code) : decode_b(code);
}

static unsigned floor_code(const State *state, int species, int32_t value) {
  if (!is_packed(state)) {
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
static void decode_row(State *state, int y, int32_t *row_a, int32_t *row_b) {
  for (int x = 0; x < state->width; x++) {
    unsigned code_a, code_b;
    load_codes(state, y * state->width + x, &code_a, &code_b);
    row_a[x] = decode(state, RD_SPECIES_A, code_a);
    row_b[x] = decode(state, RD_SPECIES_B, code_b);
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

/* Nine-point Laplacian of one species at column x from the decoded rows
 * above, at, and below the current row. The exact rational weights keep a
 * uniform field (including A = 1) exactly uniform. */
static int32_t laplacian(const int32_t *up, const int32_t *cur,
                         const int32_t *down, int x, int width) {
  int left = (x + width - 1) % width;
  int right = (x + 1) % width;
  int32_t axial_sum = cur[left] + cur[right] + up[x] + down[x];
  int32_t diagonal_sum = up[left] + up[right] + down[left] + down[right];
  int32_t value = LAPLACIAN_AXIAL_WEIGHT * axial_sum + diagonal_sum -
                  LAPLACIAN_SCALE * cur[x];
  return value < 0 ? -((-value + LAPLACIAN_SCALE / 2) / LAPLACIAN_SCALE)
                   : (value + LAPLACIAN_SCALE / 2) / LAPLACIAN_SCALE;
}

/* One explicit Euler step of the Gray-Scott update for a single cell, from
 * and to Q24 concentrations, before clamping:
 *   A' = A + dt * (Da * lap(A) - A * B^2 + feed * (1 - A))
 *   B' = B + dt * (Db * lap(B) + A * B^2 - (feed + kill) * B)
 * Rates are accumulated as Q39 (Q15 coefficient times Q24 concentration)
 * and rounded once after the dt product. */
static void react_cell(const State *state, int32_t a, int32_t b, int32_t lap_a,
                       int32_t lap_b, int32_t *next_a, int32_t *next_b) {
  int64_t ab = round_shift((int64_t)a * b, RD_VALUE_BITS);
  int64_t reaction = round_shift(ab * b, RD_VALUE_BITS) << 15;
  int64_t rate_a = (int64_t)state->da * lap_a - reaction +
                   (int64_t)state->feed * (RD_VALUE_ONE - a);
  int64_t rate_b = (int64_t)state->db * lap_b + reaction -
                   (int64_t)(state->kill + state->feed) * b;
  *next_a = a + (int32_t)round_shift(rate_a * state->dt, RATE_SHIFT);
  *next_b = b + (int32_t)round_shift(rate_b * state->dt, RATE_SHIFT);
}

/* Reset the shares held between cells and derive the dither salt before a
 * step. The residual rows themselves persist: after the last row they hold
 * the shares for row 0 of the next step. */
static void begin_step(State *state) {
  for (int species = 0; species < SPECIES_COUNT; species++) {
    state->carry[species] = state->pending[species] = state->wrap[species] = 0;
  }
  state->step_salt = mix32(state->seed ^ mix32(state->step));
}

/* Dither bits of one cell: the low DITHER_BITS are for A and the bits from
 * DITHER_B_SHIFT for B. */
static uint32_t cell_dither(const State *state, int cell_index) {
  return mix32((uint32_t)cell_index ^ state->step_salt);
}

/* Encode the value of one species at column x of the row being written,
 * with Floyd-Steinberg error diffusion and a dithered rounding threshold
 * from random, in [0, 2^DITHER_BITS). The residual row holds the shares
 * this row received from the row above; each column is consumed and then
 * reused for the shares given to the row below. Cells are encoded in row
 * order, each row from column 0 up, with finish_row after each row. */
static unsigned encode_cell(State *state, int species, int x, int32_t value,
                            uint32_t random) {
  int32_t *residual = residual_row(state, species);
  value += residual[x] + state->carry[species];
  int32_t limit = value_limit(state, species);
  if (value < 0) {
    value = 0;
  }
  if (value > limit) {
    value = limit;
  }
  unsigned code = floor_code(state, species, value);
  if (code < code_limit(state, species)) {
    int32_t low = decode(state, species, code);
    int32_t high = decode(state, species, code + 1);
    if ((int64_t)(value - low) * DITHER_SCALE >=
        (int64_t)(high - low) * (DITHER_BASE + (int32_t)random)) {
      code++;
    }
  }
  int32_t error = value - decode(state, species, code);
  int32_t right = error * DIFFUSION_RIGHT / DIFFUSION_DENOMINATOR;
  int32_t below_left = error * DIFFUSION_BELOW_LEFT / DIFFUSION_DENOMINATOR;
  int32_t below = error * DIFFUSION_BELOW / DIFFUSION_DENOMINATOR;
  state->carry[species] = right;
  residual[x] = state->pending[species] + below;
  if (x > 0) {
    residual[x - 1] += below_left;
  } else {
    state->wrap[species] = below_left;
  }
  state->pending[species] = error - right - below_left - below;
  return code;
}

/* Deliver the shares that wrapped around the periodic row ends. The right
 * share of the last column carries into column 0 of the next row. */
static void finish_row(State *state) {
  for (int species = 0; species < SPECIES_COUNT; species++) {
    int32_t *residual = residual_row(state, species);
    residual[state->width - 1] += state->wrap[species];
    residual[0] += state->pending[species];
    state->wrap[species] = state->pending[species] = 0;
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
    begin_step(state);
    for (int y = 0; y < height; y++) {
      if (y == height - 1) {
        memcpy(next[0], first[0], row_bytes);
        memcpy(next[1], first[1], row_bytes);
      } else {
        decode_row(state, y + 1, next[0], next[1]);
      }
      for (int x = 0; x < width; x++) {
        int32_t next_a, next_b;
        int cell_index = y * width + x;
        uint32_t dither = cell_dither(state, cell_index);
        react_cell(state, cur[0][x], cur[1][x],
                   laplacian(prev[0], cur[0], next[0], x, width),
                   laplacian(prev[1], cur[1], next[1], x, width), &next_a,
                   &next_b);
        store_codes(
            state, cell_index,
            encode_cell(state, RD_SPECIES_A, x, next_a, dither & DITHER_MASK),
            encode_cell(state, RD_SPECIES_B, x, next_b,
                        (dither >> DITHER_B_SHIFT) & DITHER_MASK));
      }
      finish_row(state);
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
  return decode(state, species, species == RD_SPECIES_A ? code_a : code_b);
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
    /* Q15 intensity: B times DISPLAY_GAIN, clamped to 1.0. */
    int intensity = (int)round_shift(
        (int64_t)decode(state, RD_SPECIES_B, code_b) * DISPLAY_GAIN, Q15_SHIFT);
    if (intensity > RD_Q15_ONE) {
      intensity = RD_Q15_ONE;
    }
    for (int channel = 0; channel < 3; channel++) {
      int color;
      if (palette == RD_PALETTE_MONO) {
        color =
            intensity * 100 >= MONO_THRESHOLD_PERCENT * RD_Q15_ONE ? 255 : 0;
      } else {
        int low = PALETTE_LOW[palette][channel];
        int high = PALETTE_HIGH[palette][channel];
        color = low + ((high - low) * intensity + RD_Q15_ONE / 2) / RD_Q15_ONE;
      }
      output[x * BYTES_PER_PIXEL + channel] =
          (uint8_t)(quantize ? (color + RGB2_STEP / 2) / RGB2_STEP * RGB2_STEP
                             : color);
    }
    output[x * BYTES_PER_PIXEL + 3] = 255; /* opaque alpha */
  }
  return output;
}
