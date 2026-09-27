/*
 * Shared reaction-diffusion core, numerical definition version 5: the
 * Gray-Scott and FitzHugh-Nagumo models on the 120 x 136 grid.
 *
 * Concentrations are computed in Q24 fixed point (RD_VALUE_ONE is 1.0) and
 * coefficients are Q15 (RD_Q15_ONE is 1.0). Each species is stored as a Q15
 * word per cell, and a step works on the codes with 32-bit arithmetic
 * (react_codes, and react_fhn_codes on FitzHugh-Nagumo values x in [-2, 2]
 * stored as the fraction (x + 2) / 4). Every cell is written with
 * Floyd-Steinberg error diffusion, so its rounding error is carried into its
 * unwritten neighbors instead of being discarded, and rounds to the nearest
 * code. An optional mask holds the displayed
 * species at rest in its cells and damps the pattern toward the mask (a
 * higher Gray-Scott kill rate, a FitzHugh-Nagumo pull toward rest), so the
 * pattern fades out around it. docs/core.md is the numerical contract.
 *
 * The step loop is written for an in-order Cortex-M without FPU: one row of
 * Laplacians is computed per pass with the vertical sums shared between
 * columns, the per-step coefficients live in a stack context so stores
 * through the planes cannot force reloads, and both codecs avoid divisions.
 */
#include "rd.h"
#include <string.h>

#include "palettes.h"

/* A build that defines RD_MODEL compiles only that model. */
#ifdef RD_MODEL
#if RD_MODEL < 0 || RD_MODEL >= RD_MODEL_COUNT
#error "RD_MODEL must be RD_MODEL_GRAY_SCOTT or RD_MODEL_FHN"
#endif
#endif

/* Indices of the parameter vector of each model. */
enum { GS_FEED, GS_KILL, GS_DA, GS_DB, GS_DT };

enum {
  FHN_DU,
  FHN_DV,
  FHN_RU,
  FHN_RV,
  FHN_AV,
  FHN_K,
  FHN_DT,
  FHN_REST,
  FHN_INIT
};

static const uint8_t PARAM_COUNT[RD_MODEL_COUNT] = {RD_GRAY_SCOTT_PARAMS,
                                                    RD_FHN_PARAMS};

/* 'R', 'D', '1', 0x02: marks an initialized State. */
#define STATE_MAGIC 0x52443102u
/* The State is placed on the first 4-byte boundary inside the caller block. */
#define STATE_ALIGNMENT 4

#define SPECIES_COUNT 2
#define CELLS (RD_GRID_WIDTH * RD_GRID_HEIGHT)
/* Step scratch in 32-bit words per grid column: a row of Laplacian pairs
 * (2), a row of next-value pairs (2), and six rows of 16-bit codes (3). */
#define SCRATCH_WORDS 7
#define BYTES_PER_PIXEL 4

/* Parameters of rd_init_model without a vector: the maze and fhn-stripes
 * presets of lib/presets.ts, which tests/adapter.mjs compares. */
static const int DEFAULT_PARAMS[RD_MODEL_COUNT][RD_PARAM_MAX] = {
    {950, 1868, RD_Q15_ONE, RD_Q15_ONE / 2, RD_Q15_ONE},
    {1638, RD_Q15_ONE, 492, 1229, 19661, 0, RD_Q15_ONE, 0, 0}};

/* Initial seeding: disks placed by a 32-bit LCG in display coordinates. */
#define INITIAL_DISKS 24
#define MIN_DISK_RADIUS 4
#define DISK_RADIUS_RANGE 6
#define MAX_SEED_RADIUS 100
#define LCG_MULTIPLIER 1664525u
#define LCG_INCREMENT 1013904223u

/* Q15 storage: value = code << Q15_SHIFT. */
#define Q15_SHIFT (RD_VALUE_BITS - 15)
#define Q15_CODE_MAX RD_Q15_ONE

/* Seed concentrations A = 0.5 and B = 0.25 as Q15 codes. Codes are written
 * directly, without error diffusion. */
#define Q15_FULL_A RD_Q15_ONE
#define Q15_SEED_A (RD_Q15_ONE / 2)
#define Q15_SEED_B (RD_Q15_ONE / 4)

/* FitzHugh-Nagumo storage: the Q15 code c holds x = 4 c / 2^15 - 2, so a
 * code minus FHN_ZERO is x in Q13. A seed adds 0.5 to the resting u. */
#define FHN_ZERO (RD_Q15_ONE / 2)
#define FHN_Q13_BITS 13
#define FHN_Q13_ONE (1 << FHN_Q13_BITS)
#define FHN_SEED_OFFSET (FHN_Q13_ONE / 2)
/* The broken wave of init 1, in display coordinates: u = 1 in the rows
 * [WAVE_TOP, WAVE_TOP + WAVE_EXCITED) and v = 1 in the WAVE_REFRACTORY rows
 * above them, both left of column WAVE_END, so the wave runs down and
 * curls up at its free ends (lib/fhn-simulation.ts has the same). */
#define WAVE_TOP 190
#define WAVE_EXCITED 8
#define WAVE_REFRACTORY 16
#define WAVE_END 100

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

/* FNV-1a parameters for rd_hash. */
#define FNV_OFFSET_BASIS 2166136261u
#define FNV_PRIME 16777619u

/* Mask levels: 0 in a masked cell, else the chessboard distance in cells to
 * the nearest masked cell, capped at RD_MASK_RAMP. Rows within RD_MASK_RAMP - 1
 * of a cell can give it a level below the cap, so the ring holds that many rows
 * on either side. */
#define RING_REACH (RD_MASK_RAMP - 1)
#define RING_ROWS (2 * RING_REACH + 1)

#if defined(__GNUC__)
#define ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define ALWAYS_INLINE inline
#endif

/* Rendering: the Q24 value of the displayed species minus DISPLAY_OFFSET,
 * times DISPLAY_GAIN and clamped to [0, 1.0], selects a color between the
 * palette endpoints: 3 B for Gray-Scott, and u = 4 (s - 1/2) of the stored
 * fraction s for FitzHugh-Nagumo. Monochrome switches on at
 * MONO_THRESHOLD_PERCENT. Quantized output rounds each channel to a multiple
 * of RGB2_STEP, the RGB2 level spacing. */
static const int32_t DISPLAY_OFFSET[RD_MODEL_COUNT] = {0, RD_VALUE_ONE / 2};
static const int32_t DISPLAY_GAIN[RD_MODEL_COUNT] = {3, 4};
#define MONO_THRESHOLD_PERCENT 45
#define RGB2_STEP 85

/* Display lookup table for rd_row_rgb2: one byte per LUT_BUCKET Q15 codes
 * plus one for the code RD_Q15_ONE. A zero entry
 * (alpha 0, never a valid pixel) marks a bucket whose codes differ. */
#define LUT_ENTRIES 513
#define LUT_BYTES 516
#define LUT_BUCKET_BITS 6
#define LUT_BUCKET (1 << LUT_BUCKET_BITS)
#define NO_PALETTE (-1)
/* Opaque alpha of an ARGB8 pixel: a 2 bits, r 2, g 2, b 2. */
#define ARGB8_OPAQUE 0xc0
#define ARGB8_CHANNEL_SHIFT 6

/* Channel differences between palette stops are at least -255, so adding
 * PALETTE_FLOOR_BIAS << 15 keeps the interpolation numerator non-negative
 * and its shift a floor. */
#define PALETTE_FLOOR_BIAS 255

/*
 * Control structure at the start of the aligned block. The caller block is
 * laid out as:
 *
 *   [<= 3 bytes padding][State][plane 0][plane 1]
 *   [step scratch: SCRATCH_WORDS x width int32]
 *   [residual rows: SPECIES_COUNT x width int32]
 *   [output row, RD_ROW_BYTES][display lookup table, LUT_BYTES]
 *   [column index, weight, and right index tables, 3 x RD_DISPLAY_WIDTH]
 *   [interpolation scratch row: width uint16]
 *   [mask levels, one byte per cell]
 *
 * rd_memory reports the same bytes as an accounting breakdown by component,
 * which is not the physical order. Row buffers cover the step scratch and
 * the residual rows; the rendering component covers the output row, the
 * lookup tables, and the interpolation tables and scratch row.
 */
typedef struct {
  uint32_t magic, seed, step;
  int model;
  /* The parameter vector of the model, zero beyond its length. */
  int params[RD_PARAM_MAX];
  /* Palette the display lookup table was built for, or NO_PALETTE. */
  int lut_palette;
  /* Stops of RD_PALETTE_CUSTOM, set by rd_palette. */
  uint8_t custom_stops[RD_PALETTE_MAX_STOPS][3];
  uint8_t custom_count;
  /* Rows [mask_first, mask_end) hold every cell whose mask level is below
   * RD_MASK_RAMP; empty if equal. */
  int mask_first, mask_end;
} State;

/* Everything the cell loop reads, copied out of the State once per step so
 * that the stores into the planes and residual rows cannot alias it. */
/* Error diffusion shares held between cells of the row being written: the
 * right share of the previous cell, the below-right share pending for the
 * next column, and the below-left share of column 0 that wraps to the last
 * column. The row loop keeps a copy in registers. */
typedef struct {
  int32_t carry, pending, wrap;
  /* The value of residual[x - 1] so far (its below share plus the pending
   * share it received), stored once the below-left share of cell x is
   * known, instead of a read-modify-write per cell. */
  int32_t held;
} Shares;

typedef struct {
  int model, unit_dt;
  int32_t feed, decay, dt;
  /* The diffusion coefficient of plane 0 (Gray-Scott da,
   * FitzHugh-Nagumo dv) and of plane 1 (db, du), each divided by 20 and
   * rounded, so coefficient times the 20-fold Laplacian sum is the Q30
   * rate term. */
  int32_t fold_da, fold_db;
  /* feed + kill for each mask level: the kill rises linearly from the
   * State's kill at level RD_MASK_RAMP to RD_MASK_KILL at level 0. */
  int32_t level_decay[RD_MASK_RAMP + 1];
  /* FitzHugh-Nagumo: the reaction coefficients, k and rest in Q13, and the
   * pull toward rest of each mask level, rising linearly from 0 at level
   * RD_MASK_RAMP to RD_MASK_PULL at level 0. */
  int32_t ru, rv, av, k13, rest13;
  int32_t level_pull[RD_MASK_RAMP + 1];
  /* The code the displayed species is held at in a masked cell. */
  int32_t masked_code;
  int32_t *restrict residual[SPECIES_COUNT];
  Shares shares[SPECIES_COUNT];
} StepContext;

/* Whether the build runs a model: the one it is folded to (RD_MODEL), or
 * any. */
static int model_supported(int model) {
#ifdef RD_MODEL
  return model == RD_MODEL;
#else
  return model >= 0 && model < RD_MODEL_COUNT;
#endif
}

/* The model of a state or step. A build that defines RD_MODEL folds it to
 * a constant. */
static inline int state_model(const State *state) {
#if defined(RD_MODEL)
  (void)state;
  return RD_MODEL;
#else
  return state->model;
#endif
}

static inline int ctx_model(const StepContext *ctx) {
#if defined(RD_MODEL)
  (void)ctx;
  return RD_MODEL;
#else
  return ctx->model;
#endif
}

/* A Q15 FitzHugh-Nagumo coefficient (k or rest) in Q13, rounded half up
 * with an arithmetic shift. */
static inline int32_t fhn_q13(int value) { return (value + 2) >> 2; }

/* The code of the displayed species at rest, which masked cells hold:
 * B = 0, or u = rest. */
static unsigned rest_code(const State *state) {
  return state_model(state) == RD_MODEL_FHN
             ? (unsigned)(FHN_ZERO + fhn_q13(state->params[FHN_REST]))
             : 0;
}

size_t rd_memory(int component) {
  switch (component) {
  case RD_COMPONENT_FIELDS:
    return (size_t)CELLS * SPECIES_COUNT * sizeof(uint16_t);
  case RD_COMPONENT_ROW_BUFFERS:
    return (size_t)RD_GRID_WIDTH * (SCRATCH_WORDS + SPECIES_COUNT) *
           sizeof(int32_t);
  case RD_COMPONENT_CONTROL:
    return sizeof(State);
  case RD_COMPONENT_OUTPUT_ROW:
    return RD_ROW_BYTES + LUT_BYTES + 3 * RD_DISPLAY_WIDTH +
           (size_t)RD_GRID_WIDTH * sizeof(uint16_t);
  case RD_COMPONENT_ALIGNMENT:
    return STATE_ALIGNMENT - 1;
  case RD_COMPONENT_MASK:
    return CELLS;
  default:
    return 0;
  }
}

size_t rd_bytes(void) {
  size_t total = 0;
  for (int component = 0; component < RD_COMPONENT_COUNT; component++) {
    total += rd_memory(component);
  }
  return total;
}

/* Stored Q15 plane of one species. */
static uint16_t *plane(State *state, int species) {
  return (uint16_t *)(state + 1) + (size_t)species * CELLS;
}

/* Step scratch, after the planes: SCRATCH_WORDS words per column. Free
 * between steps, when rd_mask uses it. */
static int32_t *scratch(State *state) {
  return (int32_t *)plane(state, SPECIES_COUNT);
}

/* Error diffusion residual row of one species, after the step scratch. */
static int32_t *residual_row(State *state, int species) {
  return scratch(state) + (SCRATCH_WORDS + species) * RD_GRID_WIDTH;
}

/* RGBA output row, after the residual rows. */
static uint8_t *output_row(State *state) {
  return (uint8_t *)residual_row(state, SPECIES_COUNT);
}

/* Display lookup table of Q15 values of the displayed species in
 * LUT_BUCKET buckets, after the output row. */
static uint8_t *lookup_table(State *state) {
  return output_row(state) + RD_ROW_BYTES;
}

/* Left grid column and weight (of the right column, in 256ths) of every
 * display column, for interpolated rendering. */
static uint8_t *column_index(State *state) {
  return lookup_table(state) + LUT_BYTES;
}

static uint8_t *column_weight(State *state) {
  return column_index(state) + RD_DISPLAY_WIDTH;
}

/* The right grid column of every display column, periodic. */
static uint8_t *column_right(State *state) {
  return column_weight(state) + RD_DISPLAY_WIDTH;
}

/* One grid row of interpolated Q15 values. */
static uint16_t *interpolation_row(State *state) {
  return (uint16_t *)(column_right(state) + RD_DISPLAY_WIDTH);
}

/* Mask levels, after the rendering area, one byte per cell in row order.
 * Only the rows [mask_first, mask_end) are written and read. */
static uint8_t *mask_levels(State *state) {
  return (uint8_t *)(interpolation_row(state) + RD_GRID_WIDTH);
}

/* Code that runs only at initialization, on new parameters, and on clock
 * changes, outside the step loop: optimizing GCC builds, the watch builds
 * among them, compile it for size, and the parameter check is kept as one
 * copy instead of inlined into each caller. -O0 builds keep it unoptimized
 * as the reference of scripts/check-optimization.sh, and clang ignores the
 * GCC attributes. */
#if defined(__OPTIMIZE__) && defined(__GNUC__) && !defined(__clang__)
#define SIZE_OPT __attribute__((optimize("Os")))
#define NOINLINE __attribute__((noinline))
#else
#define SIZE_OPT
#define NOINLINE
#endif

/* Mask updates run at launch and on clock changes, outside the step loop,
 * so they are compiled for size (SIZE_OPT). */
#define MASK_SIZE_OPT SIZE_OPT

/* Horizontal distance of every cell of grid row r to the nearest masked
 * cell of that row, capped at RD_MASK_RAMP; RD_MASK_RAMP outside the grid.
 * Distances do not wrap. The two bytes after the row receive the first and
 * last column below the cap (first > last when there is none). */
static MASK_SIZE_OPT void distance_row(const uint8_t *bitmap, int width,
                                       int height, int r, uint8_t *out) {
  int stride = (width + 7) / 8, distance = RD_MASK_RAMP;
  int first = width, last = -1;
  for (int x = 0; x < width; x++) {
    int set =
        r >= 0 && r < height && bitmap[r * stride + (x >> 3)] >> (x & 7) & 1;
    distance = set ? 0 : distance < RD_MASK_RAMP ? distance + 1 : RD_MASK_RAMP;
    out[x] = (uint8_t)distance;
  }
  distance = RD_MASK_RAMP;
  for (int x = width - 1; x >= 0; x--) {
    distance = out[x] == 0               ? 0
               : distance < RD_MASK_RAMP ? distance + 1
                                         : RD_MASK_RAMP;
    if (distance < out[x]) {
      out[x] = (uint8_t)distance;
    }
    if (out[x] < RD_MASK_RAMP) {
      first = x;
      if (last < 0) {
        last = x;
      }
    }
  }
  out[width] = (uint8_t)(first > last ? 1 : first);
  out[width + 1] = (uint8_t)(first > last ? 0 : last);
}

static inline int ring_slot(int r) {
  return (r % RING_ROWS + RING_ROWS) % RING_ROWS;
}

/* Bring the ring to row y: all rows y - RING_REACH to y + RING_REACH when
 * start is set, else only the new row y + RING_REACH. */
static MASK_SIZE_OPT void advance_ring(const uint8_t *bitmap, uint8_t *ring,
                                       int width, int height, int y,
                                       int start) {
  for (int r = start ? y - RING_REACH : y + RING_REACH; r <= y + RING_REACH;
       r++) {
    distance_row(bitmap, width, height, r, ring + ring_slot(r) * (width + 2));
  }
}

/* Levels of row y from the ring: the chessboard distance is the minimum
 * over the rows of max(row distance, horizontal distance), taken only over
 * the columns where a row is below the cap. */
static MASK_SIZE_OPT void ring_levels(const uint8_t *ring, int width, int y,
                                      uint8_t *out) {
  memset(out, RD_MASK_RAMP, (size_t)width);
  for (int dy = -RING_REACH; dy <= RING_REACH; dy++) {
    const uint8_t *row = ring + ring_slot(y + dy) * (width + 2);
    int vertical = dy < 0 ? -dy : dy;
    for (int x = row[width]; x <= row[width + 1]; x++) {
      int level = row[x] > vertical ? row[x] : vertical;
      if (level < out[x]) {
        out[x] = (uint8_t)level;
      }
    }
  }
}

/* Mask level of one cell. */
static int cell_level(State *state, int x, int y) {
  return y >= state->mask_first && y < state->mask_end
             ? mask_levels(state)[(size_t)y * RD_GRID_WIDTH + x]
             : RD_MASK_RAMP;
}

/* A masked cell: level 0. Without a mask no cell is masked. */
static int cell_masked(State *state, int x, int y) {
  return cell_level(state, x, y) == 0;
}

/* Round a value to the nearest multiple of 2^bits, halfway values away from
 * zero, and drop those bits. */
static inline int64_t round_shift(int64_t value, int bits) {
  /* A branch-free form measured slower on the watch than this one. */
  int64_t half = (int64_t)1 << (bits - 1);
  return value < 0 ? -((-value + half) >> bits) : (value + half) >> bits;
}

/* Decoding a Q15 code to Q24. */
static inline int32_t decode_q15(unsigned code) {
  return (int32_t)code << Q15_SHIFT;
}

/* Write the codes of one cell. */
static void store_codes(State *state, int cell_index, unsigned code_a,
                        unsigned code_b) {
  plane(state, RD_SPECIES_A)[cell_index] = (uint16_t)code_a;
  plane(state, RD_SPECIES_B)[cell_index] = (uint16_t)code_b;
}

/* The State behind a public handle, or NULL if it is not initialized. */
static State *checked_state(void *handle) {
  State *state = handle;
  return state && state->magic == STATE_MAGIC ? state : NULL;
}

/* Whether values is a valid parameter vector of the model: count entries,
 * each within the range of its parameter (core/rd.h). */
static SIZE_OPT NOINLINE int params_valid(int model, const int *values,
                                          int count) {
  if (!values || !model_supported(model) || count != PARAM_COUNT[model]) {
    return 0;
  }
  for (int i = 0; i < count; i++) {
    int low = 0, high = RD_Q15_ONE;
    if (model == RD_MODEL_FHN) {
      if (i == FHN_K || i == FHN_REST) {
        low = -RD_Q15_ONE;
      } else if (i == FHN_RU) {
        high = RD_FHN_RU_MAX;
      } else if (i == FHN_INIT) {
        high = 1;
      }
    }
    if (values[i] < low || values[i] > high) {
      return 0;
    }
  }
  return 1;
}

SIZE_OPT int rd_set_params(void *handle, const int *values, int count) {
  State *state = checked_state(handle);
  if (!state || !params_valid(state_model(state), values, count)) {
    return -1;
  }
  memcpy(state->params, values, (size_t)count * sizeof(int));
  return 0;
}

SIZE_OPT int rd_params(void *handle, int feed, int kill, int da, int db,
                       int dt) {
  State *state = checked_state(handle);
  if (!state || state_model(state) != RD_MODEL_GRAY_SCOTT) {
    return -1;
  }
  const int values[RD_GRAY_SCOTT_PARAMS] = {feed, kill, da, db, dt};
  return rd_set_params(state, values, RD_GRAY_SCOTT_PARAMS);
}

int rd_model(void *handle) {
  State *state = checked_state(handle);
  return state ? state_model(state) : -1;
}

SIZE_OPT int rd_param_count(int model) {
  return model >= 0 && model < RD_MODEL_COUNT ? PARAM_COUNT[model] : -1;
}

SIZE_OPT int rd_check_params(int model, const int *values, int count) {
  return params_valid(model, values, count) ? 0 : -1;
}

int rd_palette(void *handle, const uint8_t *rgb, int count) {
  State *state = checked_state(handle);
  if (!state || !rgb || count < 2 || count > RD_PALETTE_MAX_STOPS) {
    return -1;
  }
  memcpy(state->custom_stops, rgb, (size_t)count * 3);
  state->custom_count = (uint8_t)count;
  if (state->lut_palette == RD_PALETTE_CUSTOM) {
    state->lut_palette = NO_PALETTE;
  }
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

/* Seed a disk given in display coordinates, except in masked cells: A = 0.5
 * and B = 0.25 inside it, or u = rest + 0.5 with v unchanged. Each grid
 * cell samples the display coordinate it covers. */
SIZE_OPT int rd_seed(void *handle, int x, int y, int radius) {
  State *state = checked_state(handle);
  if (!state || x < 0 || x >= RD_DISPLAY_WIDTH || y < 0 ||
      y >= RD_DISPLAY_HEIGHT || radius < 1 || radius > MAX_SEED_RADIUS) {
    return -1;
  }
  int fhn = state_model(state) == RD_MODEL_FHN;
  unsigned seed_u = rest_code(state) + FHN_SEED_OFFSET;
  for (int grid_y = 0; grid_y < RD_GRID_HEIGHT; grid_y++) {
    for (int grid_x = 0; grid_x < RD_GRID_WIDTH; grid_x++) {
      int display_x = grid_x * RD_DISPLAY_WIDTH / RD_GRID_WIDTH;
      int display_y = grid_y * RD_DISPLAY_HEIGHT / RD_GRID_HEIGHT;
      int dx = wrap_delta(display_x - x, RD_DISPLAY_WIDTH);
      int dy = wrap_delta(display_y - y, RD_DISPLAY_HEIGHT);
      if (dx * dx + dy * dy > radius * radius ||
          cell_masked(state, grid_x, grid_y)) {
        continue;
      }
      int cell_index = grid_y * RD_GRID_WIDTH + grid_x;
      if (fhn) {
        plane(state, RD_SPECIES_B)[cell_index] = (uint16_t)seed_u;
      } else {
        store_codes(state, cell_index, Q15_SEED_A, Q15_SEED_B);
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

/* Floor of a / b for b > 0. */
static int floor_div(int a, int b) {
  return a >= 0 ? a / b : -((-a + b - 1) / b);
}

/* Interpolation position of display coordinate p on an axis of n cells
 * spanning size display pixels: the pixel center in cell units is
 * u = ((2p + 1) n - size) / (2 size), and q = floor(256 u) gives the lower
 * cell, periodic, and the weight of the next cell in 256ths. */
static void axis_sample(int p, int n, int size, int *index, int *weight) {
  int q = floor_div(((2 * p + 1) * n - size) * 256, 2 * size);
  int cell = floor_div(q, 256);
  *weight = q - cell * 256;
  *index = (cell + n) % n;
}

/* The resting field of the model everywhere: A = 1 and B = 0, or u = rest
 * and v = rest / av (0 if av is 0) with one integer division, both clamped
 * to the codes. */
static SIZE_OPT void fill_rest(State *state) {
  unsigned code_a = Q15_FULL_A, code_b = 0;
  if (state_model(state) == RD_MODEL_FHN) {
    int32_t av = state->params[FHN_AV];
    /* v = rest / av, which balances the v equation, or k with av = 0,
     * where the v equation forces u = 0 and the u equation then gives
     * v = k. */
    int32_t v13 = av ? fhn_q13(state->params[FHN_REST]) * RD_Q15_ONE / av
                     : fhn_q13(state->params[FHN_K]);
    v13 = v13 < -FHN_ZERO ? -FHN_ZERO : v13 > FHN_ZERO ? FHN_ZERO : v13;
    code_a = (unsigned)(FHN_ZERO + v13);
    code_b = rest_code(state);
  }
  for (int i = 0; i < CELLS; i++) {
    store_codes(state, i, code_a, code_b);
  }
}

/* The broken wave of init 1 over the resting field: u = 1 in the excited
 * band and v = 1 in the refractory band above it, each grid cell sampling
 * the display coordinate it covers. No LCG is used. */
static SIZE_OPT void seed_wave(State *state) {
  for (int grid_y = 0; grid_y < RD_GRID_HEIGHT; grid_y++) {
    int display_y = grid_y * RD_DISPLAY_HEIGHT / RD_GRID_HEIGHT;
    int species =
        display_y >= WAVE_TOP && display_y < WAVE_TOP + WAVE_EXCITED
            ? RD_SPECIES_B
        : display_y >= WAVE_TOP - WAVE_REFRACTORY && display_y < WAVE_TOP
            ? RD_SPECIES_A
            : -1;
    for (int grid_x = 0; species >= 0 && grid_x < RD_GRID_WIDTH; grid_x++) {
      if (grid_x * RD_DISPLAY_WIDTH / RD_GRID_WIDTH < WAVE_END) {
        plane(state, species)[grid_y * RD_GRID_WIDTH + grid_x] =
            FHN_ZERO + FHN_Q13_ONE;
      }
    }
  }
}

/* Initialize the block to the resting field of the model, then seed
 * INITIAL_DISKS disks at LCG-chosen display positions and radii, or the
 * broken wave of FitzHugh-Nagumo init 1. Every argument is checked before
 * the block is written. */
SIZE_OPT void *rd_init_model(void *memory, size_t bytes, int model,
                             uint32_t seed, const int *params, int count) {
  size_t required = rd_bytes();
  if (!memory || bytes < required || !model_supported(model) ||
      (params && !params_valid(model, params, count))) {
    return NULL;
  }
  /* Align up: the State starts at most STATE_ALIGNMENT - 1 bytes into the
   * block, and everything after it is exactly the other components. */
  State *state = (State *)(((uintptr_t)memory + STATE_ALIGNMENT - 1) &
                           ~(uintptr_t)(STATE_ALIGNMENT - 1));
  memset(state, 0, required - (STATE_ALIGNMENT - 1));
  state->magic = STATE_MAGIC;
  state->seed = seed;
  state->lut_palette = NO_PALETTE;
  state->custom_count = PALETTE_STOP_COUNT[RD_PALETTE_LIME];
  memcpy(state->custom_stops, PALETTE_STOPS[RD_PALETTE_LIME],
         sizeof state->custom_stops);
  for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
    int index, weight;
    axis_sample(x, RD_GRID_WIDTH, RD_DISPLAY_WIDTH, &index, &weight);
    column_index(state)[x] = (uint8_t)index;
    column_weight(state)[x] = (uint8_t)weight;
    column_right(state)[x] =
        (uint8_t)(index + 1 == RD_GRID_WIDTH ? 0 : index + 1);
  }
  state->model = model;
  memcpy(state->params, params ? params : DEFAULT_PARAMS[model],
         PARAM_COUNT[model] * sizeof(int));
  fill_rest(state);
  if (model == RD_MODEL_FHN && state->params[FHN_INIT] == 1) {
    seed_wave(state);
    return state;
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

SIZE_OPT void *rd_init(void *memory, size_t bytes, uint32_t seed) {
  return rd_init_model(memory, bytes, RD_MODEL_GRAY_SCOTT, seed, NULL, 0);
}

/* The new Q24 value of a Q15 code after a Q30 rate: with dt = 1 the code
 * times 2^9 plus the rate rounded half up to Q24, else the rate times dt
 * rounded the same way in 64 bits. */
static inline int32_t advance_code(unsigned code, int32_t rate, int32_t dt,
                                   int unit_dt) {
  if (unit_dt) {
    return (int32_t)(code << Q15_SHIFT) + ((rate >> 6) + ((rate >> 5) & 1));
  }
  return (int32_t)(code << Q15_SHIFT) +
         (int32_t)(((int64_t)rate * dt + (1 << 20)) >> 21);
}

/* Update of one Gray-Scott cell, from its Q15 codes and the
 * 20-fold Laplacian sums of the codes, in 32-bit arithmetic:
 *   rate_a = fold_da * sum_a - A B^2 + feed * (1 - A)         (Q30)
 *   rate_b = fold_db * sum_b + A B^2 - (feed + kill) * B      (Q30)
 * A B^2 is exact to the Q30 unit (the product A B split at bit 15). With
 * fold_da at most 1638 the Laplacian terms stay below 2^30 in magnitude, so
 * rate_a always fits int32, and of rate_b only the final subtraction of
 * (feed + kill) B, up to 2^31, can leave the range, where it saturates. The new
 * value is the code plus the rate rounded half up to Q24, as a Q24 value for
 * the encoder, or with a general dt the rate times dt rounded the same way. */
static inline void react_codes(unsigned a, unsigned b, int32_t sum_a,
                               int32_t sum_b, int32_t feed, int32_t decay,
                               int32_t fold_da, int32_t fold_db, int32_t dt,
                               int unit_dt, int32_t *next_a, int32_t *next_b) {
  uint32_t ab = a * b;
  int32_t reaction = (int32_t)((ab >> 15) * b + (((ab & 32767) * b) >> 15));
  int32_t rate_a =
      fold_da * sum_a - reaction + feed * (int32_t)(RD_Q15_ONE - a);
  int32_t rate_b;
  if (__builtin_sub_overflow(fold_db * sum_b + reaction, (uint32_t)decay * b,
                             &rate_b)) {
    rate_b = INT32_MIN;
  }
  *next_a = advance_code(a, rate_a, dt, unit_dt);
  *next_b = advance_code(b, rate_b, dt, unit_dt);
}

/* Update of one FitzHugh-Nagumo cell, from its Q15 codes (v on plane 0, u
 * on plane 1) and the 20-fold Laplacian sums of the codes, in 32-bit
 * arithmetic on x in Q13 (the code minus FHN_ZERO):
 *   rate_u = fold_du S_u + ru (u - u^3 - v + k) - pull (u - rest)   (Q30)
 *   rate_v = fold_dv S_v + rv (u - av v)                            (Q30)
 * u^2, u^3, and av v are floored to Q13 by arithmetic shifts. A Q15
 * coefficient times a Q13 value is the Q30 rate of the stored fraction
 * (x + 2) / 4, the 1/4 of the storage being the step from Q15 to Q13. With
 * |x| <= 2, ru <= RD_FHN_RU_MAX, and pull <= RD_MASK_PULL (4096) both
 * rates fit int32, and the bound holds for a pull up to 8192
 * (docs/core.md has the bounds). The new values are formed as in
 * react_codes. */
static inline void react_fhn_codes(unsigned v, unsigned u, int32_t sum_v,
                                   int32_t sum_u, int32_t pull, int32_t ru,
                                   int32_t rv, int32_t av, int32_t k13,
                                   int32_t rest13, int32_t fold_dv,
                                   int32_t fold_du, int32_t dt, int unit_dt,
                                   int32_t *next_v, int32_t *next_u) {
  int32_t u13 = (int32_t)u - FHN_ZERO, v13 = (int32_t)v - FHN_ZERO;
  int32_t uu = (u13 * u13) >> FHN_Q13_BITS;
  int32_t uuu = (uu * u13) >> FHN_Q13_BITS;
  int32_t rate_u =
      fold_du * sum_u + ru * (u13 - uuu - v13 + k13) - pull * (u13 - rest13);
  int32_t rate_v = fold_dv * sum_v + rv * (u13 - ((av * v13) >> 15));
  *next_v = advance_code(v, rate_v, dt, unit_dt);
  *next_u = advance_code(u, rate_u, dt, unit_dt);
}

/* The 20-fold nine-point Laplacian sum of a row of Q15 codes, written to
 * every stride-th entry of out, so both species can share one row of
 * pairs. */
static void laplacian_sums(const uint16_t *restrict up,
                           const uint16_t *restrict cur,
                           const uint16_t *restrict down, int32_t *restrict out,
                           int stride, int width) {
  int last = width - 1;
  int32_t s_first = up[0] + down[0], c_first = cur[0];
  int32_t s_prev = up[last] + down[last], c_prev = cur[last];
  int32_t s_cur = s_first, c_cur = c_first;
  for (int x = 0; x < last; x++) {
    int32_t s_next = up[x + 1] + down[x + 1], c_next = cur[x + 1];
    out[x * stride] = LAPLACIAN_AXIAL_WEIGHT * (s_cur + c_prev + c_next) +
                      s_prev + s_next - LAPLACIAN_SCALE * c_cur;
    s_prev = s_cur;
    c_prev = c_cur;
    s_cur = s_next;
    c_cur = c_next;
  }
  out[last * stride] = LAPLACIAN_AXIAL_WEIGHT * (s_cur + c_prev + c_first) +
                       s_prev + s_first - LAPLACIAN_SCALE * c_cur;
}

/* Fill the step context before a step. The
 * residual rows themselves persist: after the last row they hold the shares
 * for row 0 of the next step. */
static void begin_step(State *state, StepContext *ctx) {
  const int *params = state->params;
  ctx->model = state_model(state);
  ctx->masked_code = (int32_t)rest_code(state);
  if (ctx_model(ctx) == RD_MODEL_FHN) {
    /* Unused by FitzHugh-Nagumo, but react_row loads them for both. */
    ctx->feed = ctx->decay = 0;
    ctx->fold_da = (params[FHN_DV] + LAPLACIAN_SCALE / 2) / LAPLACIAN_SCALE;
    ctx->fold_db = (params[FHN_DU] + LAPLACIAN_SCALE / 2) / LAPLACIAN_SCALE;
    ctx->ru = params[FHN_RU];
    ctx->rv = params[FHN_RV];
    ctx->av = params[FHN_AV];
    ctx->k13 = fhn_q13(params[FHN_K]);
    ctx->rest13 = fhn_q13(params[FHN_REST]);
    for (int level = 0; level <= RD_MASK_RAMP; level++) {
      ctx->level_pull[level] =
          RD_MASK_PULL * (RD_MASK_RAMP - level) / RD_MASK_RAMP;
    }
    ctx->dt = params[FHN_DT];
  } else {
    int feed = params[GS_FEED], kill = params[GS_KILL];
    ctx->feed = feed;
    ctx->decay = kill + feed;
    ctx->fold_da = (params[GS_DA] + LAPLACIAN_SCALE / 2) / LAPLACIAN_SCALE;
    ctx->fold_db = (params[GS_DB] + LAPLACIAN_SCALE / 2) / LAPLACIAN_SCALE;
    for (int level = 0; level <= RD_MASK_RAMP; level++) {
      ctx->level_decay[level] =
          feed + kill +
          (RD_MASK_KILL - kill) * (RD_MASK_RAMP - level) / RD_MASK_RAMP;
    }
    ctx->dt = params[GS_DT];
  }
  ctx->unit_dt = ctx->dt == RD_Q15_ONE;
  for (int species = 0; species < SPECIES_COUNT; species++) {
    ctx->residual[species] = residual_row(state, species);
    ctx->shares[species].carry = ctx->shares[species].pending =
        ctx->shares[species].wrap = ctx->shares[species].held = 0;
  }
}

/* Encode a Q24 value at column x of the row being written as the nearest
 * Q15 code, with Floyd-Steinberg error diffusion. The residual row holds
 * the shares this row received from the row above; each column is consumed
 * and then reused for the shares given to the row below. Cells are encoded
 * in row order, each row from column 0 up, with finish_row after each
 * row. */
static inline unsigned encode_cell(int32_t *restrict residual, Shares *shares,
                                   int x, int32_t value) {
  value += residual[x] + shares->carry;
  if (value < 0) {
    value = 0;
  }
  if (value > RD_VALUE_ONE) {
    value = RD_VALUE_ONE;
  }
  /* The nearest code, halfway upward: the floor code plus one when the
   * fraction is at least half a step. At the top value 1.0 this is the
   * largest code, so no limit test is needed. */
  unsigned code = (unsigned)(value + (1 << (Q15_SHIFT - 1))) >> Q15_SHIFT;
  int32_t error = value - decode_q15(code);
  /* Floyd-Steinberg shares, truncated toward zero. */
  int32_t right = error * DIFFUSION_RIGHT / DIFFUSION_DENOMINATOR;
  int32_t below_left = error * DIFFUSION_BELOW_LEFT / DIFFUSION_DENOMINATOR;
  int32_t below = error * DIFFUSION_BELOW / DIFFUSION_DENOMINATOR;
  shares->carry = right;
  if (x > 0) {
    residual[x - 1] = shares->held + below_left;
  } else {
    shares->wrap = below_left;
  }
  shares->held = shares->pending + below;
  shares->pending = error - right - below_left - below;
  return code;
}

/* The displayed species in a masked cell: held at the model's resting code,
 * B = 0 for Gray-Scott and u = rest for FitzHugh-Nagumo (masked_code),
 * which the caller stores, so the cell has no rounding error of its own. The
 * shares it received from the row above and from the cell to its left are
 * dropped, and the below-right share of the cell to its left passes through to
 * the row below. */
static inline void encode_masked(int32_t *restrict residual, Shares *shares,
                                 int x) {
  if (x > 0) {
    residual[x - 1] = shares->held;
  }
  shares->held = shares->pending;
  shares->carry = 0;
  shares->pending = 0;
}

/* Deliver the shares that wrapped around the periodic row ends. The right
 * share of the last column carries into column 0 of the next row. */
static void finish_row(StepContext *ctx) {
  for (int species = 0; species < SPECIES_COUNT; species++) {
    int32_t *residual = ctx->residual[species];
    Shares *shares = &ctx->shares[species];
    residual[RD_GRID_WIDTH - 1] = shares->held + shares->wrap;
    residual[0] += shares->pending;
    shares->wrap = shares->pending = shares->held = 0;
  }
}

/* A row is written in three passes, each with few live values so that the
 * Cortex-M keeps them in registers: react_row computes the new Q24 values
 * of both species from the old codes and the Laplacian sums into a row of
 * pairs, then encode_row writes one species with its error diffusion.
 * Inlined at both calls in rd_step, so rows away from the
 * mask run without the level lookups (and, for FitzHugh-Nagumo, with the
 * pull a constant 0). The model is chosen once per row. */
static ALWAYS_INLINE void
react_row(StepContext *ctx, const uint16_t *restrict cur_a,
          const uint16_t *restrict cur_b, const int32_t *restrict lap,
          int32_t *restrict next, const uint8_t *restrict levels) {
  const int32_t feed = ctx->feed, decay = ctx->decay, fold_da = ctx->fold_da,
                fold_db = ctx->fold_db, dt = ctx->dt;
  const int unit_dt = ctx->unit_dt, width = RD_GRID_WIDTH;
  if (ctx_model(ctx) == RD_MODEL_FHN) {
    const int32_t ru = ctx->ru, rv = ctx->rv, av = ctx->av, k13 = ctx->k13,
                  rest13 = ctx->rest13;
    for (int x = 0; x < width; x++) {
      react_fhn_codes(cur_a[x], cur_b[x], lap[2 * x], lap[2 * x + 1],
                      levels ? ctx->level_pull[levels[x]] : 0, ru, rv, av, k13,
                      rest13, fold_da, fold_db, dt, unit_dt, &next[2 * x],
                      &next[2 * x + 1]);
    }
    return;
  }
  for (int x = 0; x < width; x++) {
    react_codes(cur_a[x], cur_b[x], lap[2 * x], lap[2 * x + 1], feed,
                levels ? ctx->level_decay[levels[x]] : decay, fold_da, fold_db,
                dt, unit_dt, &next[2 * x], &next[2 * x + 1]);
  }
}

/* Encode one species of a row from every second entry of the pair row,
 * holding the displayed species at its resting code in masked cells
 * (levels is NULL for the other species and away from the mask). */
static ALWAYS_INLINE void encode_row(StepContext *ctx, int species,
                                     const int32_t *restrict values,
                                     uint16_t *restrict cells,
                                     const uint8_t *restrict levels) {
  int32_t *restrict residual = ctx->residual[species];
  Shares shares = ctx->shares[species];
  const int width = RD_GRID_WIDTH;
  const uint16_t masked = (uint16_t)ctx->masked_code;
  for (int x = 0; x < width; x++) {
    if (levels && levels[x] == 0) {
      encode_masked(residual, &shares, x);
      cells[x] = masked;
    } else {
      cells[x] = (uint16_t)encode_cell(residual, &shares, x, values[2 * x]);
    }
  }
  ctx->shares[species] = shares;
}

/* Advance the field in place, on the codes themselves: the stored rows
 * below are still unwritten and are read in place, and only the old row
 * being rewritten is copied, to serve as the row above of the next row.
 * The step scratch holds the row of Laplacian pairs, the row of new value
 * pairs, and the old rows above and at the row and the original first row
 * as 16-bit codes. */
int rd_step(void *handle, int count) {
  State *state = checked_state(handle);
  if (!state || count < 0 || count > MAX_STEPS_PER_CALL) {
    return -1;
  }
  const int width = RD_GRID_WIDTH, height = RD_GRID_HEIGHT;
  size_t row_bytes = (size_t)width * sizeof(uint16_t);
  uint16_t *restrict cells_a = plane(state, RD_SPECIES_A);
  uint16_t *restrict cells_b = plane(state, RD_SPECIES_B);
  int32_t *lap = scratch(state), *next = lap + 2 * width;
  uint16_t *codes = (uint16_t *)(next + 2 * width);
  StepContext ctx;
  for (int iteration = 0; iteration < count; iteration++) {
    uint16_t *up[SPECIES_COUNT] = {codes, codes + width};
    uint16_t *cur[SPECIES_COUNT] = {codes + 2 * width, codes + 3 * width};
    uint16_t *first[SPECIES_COUNT] = {codes + 4 * width, codes + 5 * width};
    memcpy(up[0], cells_a + (size_t)(height - 1) * width, row_bytes);
    memcpy(up[1], cells_b + (size_t)(height - 1) * width, row_bytes);
    memcpy(first[0], cells_a, row_bytes);
    memcpy(first[1], cells_b, row_bytes);
    begin_step(state, &ctx);
    for (int y = 0; y < height; y++) {
      size_t row = (size_t)y * width;
      memcpy(cur[0], cells_a + row, row_bytes);
      memcpy(cur[1], cells_b + row, row_bytes);
      const uint16_t *down_a =
          y == height - 1 ? first[0] : cells_a + row + width;
      const uint16_t *down_b =
          y == height - 1 ? first[1] : cells_b + row + width;
      laplacian_sums(up[0], cur[0], down_a, lap, 2, width);
      laplacian_sums(up[1], cur[1], down_b, lap + 1, 2, width);
      if (y >= state->mask_first && y < state->mask_end) {
        const uint8_t *levels = mask_levels(state) + row;
        react_row(&ctx, cur[0], cur[1], lap, next, levels);
        encode_row(&ctx, RD_SPECIES_A, next, cells_a + row, NULL);
        encode_row(&ctx, RD_SPECIES_B, next + 1, cells_b + row, levels);
      } else {
        react_row(&ctx, cur[0], cur[1], lap, next, NULL);
        encode_row(&ctx, RD_SPECIES_A, next, cells_a + row, NULL);
        encode_row(&ctx, RD_SPECIES_B, next + 1, cells_b + row, NULL);
      }
      finish_row(&ctx);
      /* The old row just copied becomes the row above. */
      for (int species = 0; species < SPECIES_COUNT; species++) {
        uint16_t *tmp = up[species];
        up[species] = cur[species];
        cur[species] = tmp;
      }
    }
    state->step++;
  }
  return 0;
}

int rd_get(void *handle, int x, int y, int species) {
  State *state = checked_state(handle);
  if (!state || x < 0 || x >= RD_GRID_WIDTH || y < 0 || y >= RD_GRID_HEIGHT ||
      species < 0 || species >= SPECIES_COUNT) {
    return -1;
  }
  return decode_q15(plane(state, species)[y * RD_GRID_WIDTH + x]);
}

uint32_t rd_steps(void *handle) {
  State *state = checked_state(handle);
  return state ? state->step : 0;
}

int rd_width(void *handle) {
  State *state = checked_state(handle);
  return state ? RD_GRID_WIDTH : -1;
}

int rd_height(void *handle) {
  State *state = checked_state(handle);
  return state ? RD_GRID_HEIGHT : -1;
}

/* Install a cell mask (NULL clears it): find the band of rows within
 * RD_MASK_RAMP - 1 rows of a masked cell, derive the levels of the band,
 * and set the displayed species to its resting code (B = 0, u = rest) in
 * the masked cells right away. */
MASK_SIZE_OPT int rd_mask(void *handle, const uint8_t *mask) {
  State *state = checked_state(handle);
  if (!state) {
    return -1;
  }
  const int width = RD_GRID_WIDTH, height = RD_GRID_HEIGHT;
  int stride = (width + 7) / 8, first = -1, last = -1;
  unsigned resting = rest_code(state);
  state->mask_first = state->mask_end = 0;
  if (!mask) {
    return 0;
  }
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      if (mask[y * stride + (x >> 3)] >> (x & 7) & 1) {
        plane(state, RD_SPECIES_B)[y * width + x] = (uint16_t)resting;
        if (first < 0) {
          first = y;
        }
        last = y;
      }
    }
  }
  if (first < 0) {
    return 0;
  }
  int band_first =
      first - (RD_MASK_RAMP - 1) < 0 ? 0 : first - (RD_MASK_RAMP - 1);
  int band_end = last + RD_MASK_RAMP > height ? height : last + RD_MASK_RAMP;
  /* The step scratch is free between steps and holds the ring of distance
   * rows. */
  uint8_t *ring = (uint8_t *)scratch(state);
  uint8_t *levels = mask_levels(state);
  for (int y = band_first; y < band_end; y++) {
    advance_ring(mask, ring, width, height, y, y == band_first);
    ring_levels(ring, width, y, levels + (size_t)y * width);
  }
  state->mask_first = band_first;
  state->mask_end = band_end;
  return 0;
}

#undef MASK_SIZE_OPT

int rd_mask_level(void *handle, int x, int y) {
  State *state = checked_state(handle);
  if (!state || x < 0 || x >= RD_GRID_WIDTH || y < 0 || y >= RD_GRID_HEIGHT) {
    return -1;
  }
  return cell_level(state, x, y);
}

/* FNV-1a over the stored words as canonical little-endian 16-bit values,
 * plane 0 then plane 1, so native and Wasm builds produce comparable
 * hashes. The residual rows are not part of the hash. */
uint32_t rd_hash(void *handle) {
  State *state = checked_state(handle);
  if (!state) {
    return 0;
  }
  uint32_t hash = FNV_OFFSET_BASIS;
  for (int index = 0; index < SPECIES_COUNT; index++) {
    for (int i = 0; i < CELLS; i++) {
      unsigned value = plane(state, index)[i];
      hash = (hash ^ (value & 0xff)) * FNV_PRIME;
      hash = (hash ^ (value >> 8)) * FNV_PRIME;
    }
  }
  return hash;
}

/* The three color channels of a Q24 value of the displayed species: the
 * intensity, the value minus DISPLAY_OFFSET times DISPLAY_GAIN of the model
 * clamped to [0, 1.0], interpolated between the equally spaced stops of the
 * palette, or a monochrome threshold, and optionally quantized to RGB2
 * levels. Each channel is the nearest integer to the exact interpolation,
 * halves rounded up, so intensity 0 and 1.0 give the end stops. With two
 * stops this is the former endpoint formula low + ((high - low) * intensity
 * + 2^14) / 2^15. */
static void value_rgb(const State *state, int palette, int quantize,
                      int32_t value, uint8_t rgb[3]) {
  int model = state_model(state);
  int intensity = (int)round_shift((int64_t)(value - DISPLAY_OFFSET[model]) *
                                       DISPLAY_GAIN[model],
                                   Q15_SHIFT);
  if (intensity < 0) {
    intensity = 0;
  }
  if (intensity > RD_Q15_ONE) {
    intensity = RD_Q15_ONE;
  }
  if (palette == RD_PALETTE_MONO) {
    uint8_t color =
        intensity * 100 >= MONO_THRESHOLD_PERCENT * RD_Q15_ONE ? 255 : 0;
    rgb[0] = rgb[1] = rgb[2] = color;
    return;
  }
  int custom = palette == RD_PALETTE_CUSTOM;
  const uint8_t (*stops)[3] =
      custom ? state->custom_stops : PALETTE_STOPS[palette];
  int count = custom ? state->custom_count : PALETTE_STOP_COUNT[palette];
  /* The segment between two stops and the Q15 position inside it. */
  int position = intensity * (count - 1);
  int segment = position >> 15;
  int fraction = position & (RD_Q15_ONE - 1);
  if (segment == count - 1) {
    segment--;
    fraction = RD_Q15_ONE;
  }
  for (int channel = 0; channel < 3; channel++) {
    int low = stops[segment][channel];
    int high = stops[segment + 1][channel];
    int color = low - PALETTE_FLOOR_BIAS +
                (((high - low) * fraction + RD_Q15_ONE / 2 +
                  (PALETTE_FLOOR_BIAS << 15)) >>
                 15);
    rgb[channel] =
        (uint8_t)(quantize ? (color + RGB2_STEP / 2) / RGB2_STEP * RGB2_STEP
                           : color);
  }
}

/* The quantized color of a Q24 value as an opaque ARGB8 byte. Kept out of
 * line: the render loops call it only for the rare buckets that straddle a
 * color step. */
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static uint8_t value_argb8(const State *state, int palette, int32_t value) {
  uint8_t rgb[3];
  value_rgb(state, palette, 1, value, rgb);
  return (uint8_t)(ARGB8_OPAQUE | (rgb[0] >> ARGB8_CHANNEL_SHIFT) << 4 |
                   (rgb[1] >> ARGB8_CHANNEL_SHIFT) << 2 |
                   (rgb[2] >> ARGB8_CHANNEL_SHIFT));
}

/* Build the display lookup table for a palette unless it is current: the
 * color shared by every Q15 value of a LUT_BUCKET bucket, or 0 (never a
 * valid pixel) where they differ. */
static void ensure_lookup_table(State *state, int palette) {
  if (state->lut_palette == palette) {
    return;
  }
  uint8_t *table = lookup_table(state);
  for (unsigned bucket = 0; bucket < LUT_ENTRIES; bucket++) {
    unsigned first = bucket << LUT_BUCKET_BITS;
    unsigned last = first + LUT_BUCKET - 1;
    if (last > Q15_CODE_MAX) {
      last = Q15_CODE_MAX;
    }
    uint8_t value = value_argb8(state, palette, decode_q15(first));
    for (unsigned code = first + 1; code <= last; code++) {
      if (value_argb8(state, palette, decode_q15(code)) != value) {
        value = 0;
        break;
      }
    }
    table[bucket] = value;
  }
  state->lut_palette = palette;
}

/* The quantized color of a Q15 value through the lookup table. */
static inline uint8_t value_color(const State *state, const uint8_t *table,
                                  int palette, unsigned value) {
  uint8_t color = table[value >> LUT_BUCKET_BITS];
  return color ? color : value_argb8(state, palette, decode_q15(value));
}

/* Linear interpolation between two Q15 values with a weight in 256ths,
 * rounded half up: (l (256 - w) + r w + 128) >> 8. That equals
 * l + floor(((r - l) w + 128) / 256) with one multiply. The product is
 * above -2^23, so adding 2^23 before the shift and taking 2^15 off after
 * it gives the floor without shifting a negative value. */
static inline unsigned lerp256(unsigned left, unsigned right, int weight) {
  int t = ((int)right - (int)left) * weight + 128;
  return (unsigned)((int)left + ((t + (1 << 23)) >> 8) - (1 << 15));
}

/* Interpolate the grid rows around display row y into the scratch row. */
static const uint16_t *interpolate_rows(State *state, int y) {
  int grid_y, weight;
  axis_sample(y, RD_GRID_HEIGHT, RD_DISPLAY_HEIGHT, &grid_y, &weight);
  int next_y = grid_y + 1 == RD_GRID_HEIGHT ? 0 : grid_y + 1;
  uint16_t *row = interpolation_row(state);
  const uint16_t *top =
      plane(state, RD_SPECIES_B) + (size_t)grid_y * RD_GRID_WIDTH;
  const uint16_t *bottom =
      plane(state, RD_SPECIES_B) + (size_t)next_y * RD_GRID_WIDTH;
  for (int x = 0; x < RD_GRID_WIDTH; x++) {
    row[x] = (uint16_t)lerp256(top[x], bottom[x], weight);
  }
  return row;
}

/* Interpolated Q15 B value of display column x from the scratch row. */
static inline unsigned interpolate_column(State *state, const uint16_t *row,
                                          int x) {
  return lerp256(row[column_index(state)[x]], row[column_right(state)[x]],
                 column_weight(state)[x]);
}

/* Render display row y into the shared RGBA output row. Each display pixel
 * samples the grid cell that covers it, or with RD_ROW_BILINEAR the B
 * value interpolated at its center. */
uint8_t *rd_row(void *handle, int y, int palette, int flags) {
  State *state = checked_state(handle);
  if (!state || y < 0 || y >= RD_DISPLAY_HEIGHT || palette < 0 ||
      palette >= RD_PALETTE_COUNT ||
      (flags & ~(RD_ROW_QUANTIZE | RD_ROW_BILINEAR))) {
    return NULL;
  }
  int quantize = flags & RD_ROW_QUANTIZE;
  uint8_t *output = output_row(state);
  if (flags & RD_ROW_BILINEAR) {
    const uint16_t *row = interpolate_rows(state, y);
    for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
      value_rgb(state, palette, quantize,
                decode_q15(interpolate_column(state, row, x)),
                output + x * BYTES_PER_PIXEL);
      output[x * BYTES_PER_PIXEL + 3] = 255; /* opaque alpha */
    }
    return output;
  }
  const uint16_t *cells =
      plane(state, RD_SPECIES_B) +
      (size_t)(y * RD_GRID_HEIGHT / RD_DISPLAY_HEIGHT) * RD_GRID_WIDTH;
  for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
    value_rgb(state, palette, quantize,
              decode_q15(cells[x * RD_GRID_WIDTH / RD_DISPLAY_WIDTH]),
              output + x * BYTES_PER_PIXEL);
    output[x * BYTES_PER_PIXEL + 3] = 255; /* opaque alpha */
  }
  return output;
}

/* Render display columns first to last of row y as quantized ARGB8 bytes
 * into dst[first..last]. */
#if defined(__GNUC__)
/* Kept out of line: rd_row_rgb2 and rd_row_rgb2_into share one copy. */
__attribute__((noinline))
#endif
static void render_rgb2(State *state, int y, int palette, int flags,
                        uint8_t *dst, int first, int last) {
#ifdef RD_RENDER_FLAGS
  /* A watch build that fixes its flags keeps only that rendering. */
  (void)flags;
  flags = RD_RENDER_FLAGS;
#endif
  ensure_lookup_table(state, palette);
  const uint8_t *table = lookup_table(state);
  if (flags & RD_ROW_BILINEAR) {
    const uint16_t *row = interpolate_rows(state, y);
    if (first == 0 && last == RD_DISPLAY_WIDTH - 1) {
      /* At 120 cells, display pixels 5k to 5k + 4 sample cells 3k - 1 to
       * 3k + 3 with the weights 204, 102, 0, 153, and 51 (axis_sample),
       * so the column tables reduce to constants. */
      unsigned previous = row[119];
      for (int k = 0; k < 40; k++) {
        const uint16_t *cells = row + 3 * k;
        unsigned c0 = cells[0], c1 = cells[1], c2 = cells[2];
        unsigned c3 = k == 39 ? row[0] : cells[3];
        uint8_t *out = dst + 5 * k;
        out[0] = value_color(state, table, palette, lerp256(previous, c0, 204));
        out[1] = value_color(state, table, palette, lerp256(c0, c1, 102));
        out[2] = value_color(state, table, palette, c1);
        out[3] = value_color(state, table, palette, lerp256(c1, c2, 153));
        out[4] = value_color(state, table, palette, lerp256(c2, c3, 51));
        previous = c2;
      }
      return;
    }
    const uint8_t *left = column_index(state), *right = column_right(state);
    const uint8_t *weight = column_weight(state);
    for (int x = first; x <= last; x++) {
      dst[x] = value_color(state, table, palette,
                           lerp256(row[left[x]], row[right[x]], weight[x]));
    }
    return;
  }
  const uint16_t *cells =
      plane(state, RD_SPECIES_B) +
      (size_t)(y * RD_GRID_HEIGHT / RD_DISPLAY_HEIGHT) * RD_GRID_WIDTH;
  for (int x = first; x <= last; x++) {
    dst[x] = value_color(state, table, palette,
                         cells[x * RD_GRID_WIDTH / RD_DISPLAY_WIDTH]);
  }
}

static int rgb2_arguments_valid(State *state, int y, int palette, int flags) {
  return state && y >= 0 && y < RD_DISPLAY_HEIGHT && palette >= 0 &&
         palette < RD_PALETTE_COUNT &&
         !(flags & ~(RD_ROW_QUANTIZE | RD_ROW_BILINEAR));
}

/* Render display row y as quantized ARGB8 bytes into the shared output row,
 * the value rd_row with RD_ROW_QUANTIZE and the same RD_ROW_BILINEAR flag
 * gives after packing each channel's top two bits. */
uint8_t *rd_row_rgb2(void *handle, int y, int palette, int flags) {
  State *state = checked_state(handle);
  if (!rgb2_arguments_valid(state, y, palette, flags)) {
    return NULL;
  }
  uint8_t *output = output_row(state);
  render_rgb2(state, y, palette, flags, output, 0, RD_DISPLAY_WIDTH - 1);
  return output;
}

/* The same pixels written straight into a caller row, columns first to
 * last, for example a framebuffer row. */
int rd_row_rgb2_into(void *handle, int y, int palette, int flags, uint8_t *dst,
                     int first, int last) {
  State *state = checked_state(handle);
  if (!rgb2_arguments_valid(state, y, palette, flags) || !dst || first < 0 ||
      last >= RD_DISPLAY_WIDTH || first > last) {
    return -1;
  }
  render_rgb2(state, y, palette, flags, dst, first, last);
  return 0;
}
