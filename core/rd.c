/*
 * Shared Gray-Scott reaction-diffusion core.
 *
 * Concentrations and coefficients are Q15 fixed point (RD_Q15_ONE is 1.0).
 * Mode 1 stores Q15 values directly as 16-bit. Modes 0 and 2 store 8-bit
 * values with stochastic rounding. docs/core.md is the numerical contract.
 */
#include "rd.h"
#include <string.h>

/* Bits per stored value for each mode. */
#define MODE_BITS(mode) ((mode) == 1 ? 16 : 8)

/* 'R', 'D', '1', 0x01: marks an initialized State. */
#define STATE_MAGIC 0x52443101u
/* The State is placed on the first 4-byte boundary inside the caller block. */
#define STATE_ALIGNMENT 4

#define SPECIES_COUNT 2
/* Saved rows per species: previous, current, next, and original first row. */
#define ROW_BUFFERS_PER_SPECIES 4
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

/* Stored values written by rd_init and rd_seed. These are exact constants and
 * never go through the stochastic rounding of q15_to_stored. */
#define FULL_BYTE 255
#define SEED_A_BYTE 128
#define SEED_B_BYTE 64
#define SEED_A_Q15 (RD_Q15_ONE / 2)
#define SEED_B_Q15 (RD_Q15_ONE / 4)

#define MAX_STEPS_PER_CALL 1000000

/* Nine-point Laplacian: (4 * axial_sum + diagonal_sum - 20 * center) / 20. */
#define LAPLACIAN_AXIAL_WEIGHT 4
#define LAPLACIAN_SCALE 20

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

/* Salt mixed into the stochastic rounding, indexed by species. */
static const uint32_t SPECIES_SALT[SPECIES_COUNT] = {0x63d83595u, 0xa511e9b3u};

/* Palette endpoints, RGB per palette, at intensity 0 and intensity 1.0. */
static const int PALETTE_LOW[3][3] = {{0, 30, 18}, {0, 0, 45}, {0, 0, 0}};
static const int PALETTE_HIGH[3][3] = {
    {210, 255, 85}, {85, 255, 255}, {255, 255, 255}};

/*
 * Control structure at the start of the aligned block. The caller block is
 * laid out as:
 *
 *   [<= 3 bytes padding][State][plane A][plane B]
 *   [row buffers A: prev, cur, next, first][row buffers B: same]
 *   [output row, RD_ROW_BYTES]
 *
 * rd_memory reports the same bytes as an accounting breakdown by component,
 * which is not the physical order.
 */
typedef struct {
  uint32_t magic, seed, step;
  int width, height, bits, feed, kill, da, db, dt;
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

/* Bits per stored value. A Pebble build fixes the mode at compile time, so
 * this folds to a constant there. */
static inline int storage_bits(const State *state) {
#ifdef RD_MODE
  (void)state;
  return MODE_BITS(RD_MODE);
#else
  return state->bits;
#endif
}

size_t rd_memory(int mode, int component) {
  if (!mode_supported(mode)) {
    return 0;
  }
  int width = grid_width(mode);
  int height = grid_height(width);
  size_t bytes_per_value = MODE_BITS(mode) / 8;
  switch (component) {
  case RD_COMPONENT_FIELDS:
    return (size_t)width * height * SPECIES_COUNT * bytes_per_value;
  case RD_COMPONENT_ROW_BUFFERS:
    return (size_t)width * SPECIES_COUNT * ROW_BUFFERS_PER_SPECIES *
           bytes_per_value;
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

/* Concentration plane of one species, immediately after the State. */
static uint8_t *species_field(State *state, int species) {
  return (uint8_t *)(state + 1) + (size_t)species * state->width *
                                      state->height * (storage_bits(state) / 8);
}

/* Saved rows for both species, after the two planes. */
static uint8_t *row_buffers(State *state) {
  return species_field(state, SPECIES_COUNT);
}

/* RGBA output row, after the row buffers. */
static uint8_t *output_row(State *state) {
  return row_buffers(state) + SPECIES_COUNT * ROW_BUFFERS_PER_SPECIES *
                                  state->width * (storage_bits(state) / 8);
}

static int load_stored(State *state, const uint8_t *plane, int cell_index) {
  return storage_bits(state) == 8 ? plane[cell_index]
                                  : ((const uint16_t *)plane)[cell_index];
}

static void store(State *state, uint8_t *plane, int cell_index, int value) {
  if (storage_bits(state) == 8) {
    plane[cell_index] = (uint8_t)value;
  } else {
    ((uint16_t *)plane)[cell_index] = (uint16_t)value;
  }
}

/* Reading expands 8-bit storage to Q15. */
static int stored_to_q15(State *state, int value) {
  return storage_bits(state) == 8
             ? (value * RD_Q15_ONE + FULL_BYTE / 2) / FULL_BYTE
             : value;
}

/* Read one cell of a plane as Q15. */
static int load_q15(State *state, const uint8_t *plane, int cell_index) {
  return stored_to_q15(state, load_stored(state, plane, cell_index));
}

/* Round a Q15 product to nearest, with halfway values away from zero. */
static int round_q15(int64_t value) {
  return value < 0 ? -(int)((-value + RD_Q15_ONE / 2) / RD_Q15_ONE)
                   : (int)((value + RD_Q15_ONE / 2) / RD_Q15_ONE);
}

static int mul_q15(int a, int b) { return round_q15((int64_t)a * b); }

/* 32-bit integer mixing function (lowbias32) for stochastic rounding. */
static uint32_t mix32(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  return value ^ (value >> 16);
}

/* Writing clamps to [0, 1.0]. For 8-bit storage the value is scaled by 255
 * and the remaining fraction is rounded stochastically, using a hash of the
 * seed, the linear cell coordinate, the step counter, and the species. */
static int q15_to_stored(State *state, int value, int cell_index, int species) {
  if (value < 0) {
    value = 0;
  }
  if (value > RD_Q15_ONE) {
    value = RD_Q15_ONE;
  }
  if (storage_bits(state) == 16) {
    return value;
  }
  uint32_t scaled = (uint32_t)value * FULL_BYTE;
  uint32_t random_bits = mix32(state->seed ^ mix32((uint32_t)cell_index) ^
                               mix32(state->step) ^ SPECIES_SALT[species]);
  uint32_t fraction = scaled % RD_Q15_ONE;
  return (int)(scaled / RD_Q15_ONE) +
         ((random_bits & (RD_Q15_ONE - 1)) < fraction);
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
  int eight_bit = storage_bits(state) == 8;
  for (int grid_y = 0; grid_y < state->height; grid_y++) {
    for (int grid_x = 0; grid_x < state->width; grid_x++) {
      int display_x = grid_x * RD_DISPLAY_WIDTH / state->width;
      int display_y = grid_y * RD_DISPLAY_HEIGHT / state->height;
      int dx = wrap_delta(display_x - x, RD_DISPLAY_WIDTH);
      int dy = wrap_delta(display_y - y, RD_DISPLAY_HEIGHT);
      if (dx * dx + dy * dy <= radius * radius) {
        int cell_index = grid_y * state->width + grid_x;
        store(state, species_field(state, RD_SPECIES_A), cell_index,
              eight_bit ? SEED_A_BYTE : SEED_A_Q15);
        store(state, species_field(state, RD_SPECIES_B), cell_index,
              eight_bit ? SEED_B_BYTE : SEED_B_Q15);
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
  state->bits = MODE_BITS(mode);
  rd_params(state, DEFAULT_FEED, DEFAULT_KILL, RD_Q15_ONE, RD_Q15_ONE / 2,
            RD_Q15_ONE);
  int eight_bit = storage_bits(state) == 8;
  for (int i = 0; i < state->width * state->height; i++) {
    store(state, species_field(state, RD_SPECIES_A), i,
          eight_bit ? FULL_BYTE : RD_Q15_ONE);
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

/* Nine-point Laplacian of one species at column x, from the saved rows above,
 * at, and below the current row. The exact rational weights keep a uniform
 * field (including A = 1) exactly uniform. */
static int laplacian(State *state, const uint8_t *up, const uint8_t *cur,
                     const uint8_t *down, int x) {
  int left = (x + state->width - 1) % state->width;
  int right = (x + 1) % state->width;
  int axial_sum = load_q15(state, cur, left) + load_q15(state, cur, right) +
                  load_q15(state, up, x) + load_q15(state, down, x);
  int diagonal_sum = load_q15(state, up, left) + load_q15(state, up, right) +
                     load_q15(state, down, left) + load_q15(state, down, right);
  int value = LAPLACIAN_AXIAL_WEIGHT * axial_sum + diagonal_sum -
              LAPLACIAN_SCALE * load_q15(state, cur, x);
  return value < 0 ? -((-value + LAPLACIAN_SCALE / 2) / LAPLACIAN_SCALE)
                   : (value + LAPLACIAN_SCALE / 2) / LAPLACIAN_SCALE;
}

/* One explicit Euler step of the Gray-Scott update for a single cell:
 *   A' = clamp(A + dt * (Da * lap(A) - A * B^2 + feed * (1 - A)))
 *   B' = clamp(B + dt * (Db * lap(B) + A * B^2 - (feed + kill) * B))
 * The results are written in stored form to *next_a and *next_b. */
static void react_cell(State *state, int cell_index, int a, int b, int lap_a,
                       int lap_b, int *next_a, int *next_b) {
  int reaction = mul_q15(mul_q15(a, b), b);
  *next_a = q15_to_stored(state,
                          a + mul_q15(mul_q15(state->da, lap_a) - reaction +
                                          mul_q15(state->feed, RD_Q15_ONE - a),
                                      state->dt),
                          cell_index, RD_SPECIES_A);
  *next_b = q15_to_stored(state,
                          b + mul_q15(mul_q15(state->db, lap_b) + reaction -
                                          mul_q15(state->kill + state->feed, b),
                                      state->dt),
                          cell_index, RD_SPECIES_B);
}

/* Advance the field in place. Each species keeps four saved rows so that
 * neighbors still see the old values of rows that were already rewritten. */
int rd_step(void *handle, int count) {
  State *state = checked_state(handle);
  if (!state || count < 0 || count > MAX_STEPS_PER_CALL) {
    return -1;
  }
  int row_bytes = state->width * (storage_bits(state) / 8);
  uint8_t *buffers = row_buffers(state);
  for (int iteration = 0; iteration < count; iteration++) {
    /* prev, cur, and next hold the old rows above, at, and below the row
     * being updated. first keeps the original row 0, which is the periodic
     * neighbor below the last row after row 0 has been rewritten. */
    uint8_t *prev[SPECIES_COUNT], *cur[SPECIES_COUNT], *next[SPECIES_COUNT],
        *first[SPECIES_COUNT];
    for (int species = 0; species < SPECIES_COUNT; species++) {
      prev[species] = buffers + species * ROW_BUFFERS_PER_SPECIES * row_bytes;
      cur[species] = prev[species] + row_bytes;
      next[species] = cur[species] + row_bytes;
      first[species] = next[species] + row_bytes;
      memcpy(prev[species],
             species_field(state, species) + (state->height - 1) * row_bytes,
             row_bytes);
      memcpy(cur[species], species_field(state, species), row_bytes);
      memcpy(first[species], cur[species], row_bytes);
    }
    for (int y = 0; y < state->height; y++) {
      for (int species = 0; species < SPECIES_COUNT; species++) {
        memcpy(next[species],
               y == state->height - 1
                   ? first[species]
                   : species_field(state, species) + (y + 1) * row_bytes,
               row_bytes);
      }
      for (int x = 0; x < state->width; x++) {
        int a, b, cell_index = y * state->width + x;
        react_cell(state, cell_index, load_q15(state, cur[0], x),
                   load_q15(state, cur[1], x),
                   laplacian(state, prev[0], cur[0], next[0], x),
                   laplacian(state, prev[1], cur[1], next[1], x), &a, &b);
        store(state, species_field(state, RD_SPECIES_A), cell_index, a);
        store(state, species_field(state, RD_SPECIES_B), cell_index, b);
      }
      /* Rotate: the row just finished becomes the row above, and the buffer
       * that held the old row above is reused for the next row below. */
      for (int species = 0; species < SPECIES_COUNT; species++) {
        uint8_t *tmp = prev[species];
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
  return load_q15(state, species_field(state, species), y * state->width + x);
}

uint32_t rd_steps(void *handle) {
  State *state = checked_state(handle);
  return state ? state->step : 0;
}

/* FNV-1a over the stored values as canonical little-endian 16-bit words.
 * 8-bit modes hash a zero high byte, so native and Wasm builds of the same
 * mode produce comparable hashes. */
uint32_t rd_hash(void *handle) {
  State *state = checked_state(handle);
  if (!state) {
    return 0;
  }
  uint32_t hash = FNV_OFFSET_BASIS;
  for (int species = 0; species < SPECIES_COUNT; species++) {
    for (int i = 0; i < state->width * state->height; i++) {
      int value = load_stored(state, species_field(state, species), i);
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
    int intensity = rd_get(state, grid_x, grid_y, RD_SPECIES_B) * DISPLAY_GAIN;
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
