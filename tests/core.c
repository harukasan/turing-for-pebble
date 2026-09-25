#include "../core/rd.c"
#include "../core/clock_mask.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Full-screen oracle: one step computed from a complete copy of both planes
 * with independent neighbor indexing, so the row-buffer scheme in rd_step is
 * checked against a straightforward implementation. The Laplacian rounding is
 * written out on purpose instead of calling laplacian_row(). Cells are
 * encoded in row order through the shared error diffusion functions, and
 * the masked cell rule is written out as docs/core.md states it. */
/* Mask levels of the cells for the oracle, from level_reference, or NULL
 * for no mask. */
static const int *oracle_levels;

static void reference(State *state) {
  int cells = state->width * state->height;
  size_t plane_bytes = (size_t)cells * sizeof(uint16_t);
  uint16_t *next_planes = malloc(plane_bytes * planes(state));
  assert(next_planes);
  uint16_t *saved = plane(state, 0);
  StepContext ctx;
  begin_step(state, &ctx);
  for (int y = 0; y < state->height; y++) {
    for (int x = 0; x < state->width; x++) {
      int cell_index = y * state->width + x;
      int level = oracle_levels ? oracle_levels[cell_index] : RD_MASK_RAMP;
      int32_t decay =
          state->feed + state->kill +
          (RD_MASK_KILL - state->kill) * (RD_MASK_RAMP - level) / RD_MASK_RAMP;
      int32_t laps[SPECIES_COUNT];
      for (int species = 0; species < SPECIES_COUNT; species++) {
        int32_t sum = 0;
        for (int dy = -1; dy <= 1; dy++) {
          for (int dx = -1; dx <= 1; dx++) {
            int weight = dx == 0 && dy == 0 ? -20 : dx == 0 || dy == 0 ? 4 : 1;
            sum += weight *
                   rd_get(state, (x + dx + state->width) % state->width,
                          (y + dy + state->height) % state->height, species);
          }
        }
        laps[species] = sum < 0 ? -((-sum + 10) / 20) : (sum + 10) / 20;
      }
      int32_t next_a, next_b;
      react_cell(rd_get(state, x, y, RD_SPECIES_A),
                 rd_get(state, x, y, RD_SPECIES_B), laps[0], laps[1], ctx.feed,
                 decay, ctx.da, ctx.db, ctx.dt, ctx.unit_dt, &next_a, &next_b);
      uint32_t dither =
          is_packed(state) ? cell_dither(ctx.step_salt, cell_index) : 0;
      unsigned code_a =
          encode_cell(is_packed(state), RD_SPECIES_A, ctx.residual[0],
                      &ctx.shares[0], x, next_a, dither & DITHER_MASK);
      unsigned code_b = 0;
      if (level == 0) {
        /* Masked: B is held at 0, and only the pending share passes. */
        ctx.residual[1][x] = ctx.shares[1].pending;
        ctx.shares[1].carry = ctx.shares[1].pending = 0;
      } else {
        code_b = encode_cell(is_packed(state), RD_SPECIES_B, ctx.residual[1],
                             &ctx.shares[1], x, next_b,
                             (dither >> DITHER_B_SHIFT) & DITHER_MASK);
      }
      if (is_packed(state)) {
        next_planes[cell_index] = (uint16_t)((code_a << B_BITS) | code_b);
      } else {
        next_planes[cell_index] = (uint16_t)code_a;
        next_planes[cells + cell_index] = (uint16_t)code_b;
      }
    }
    finish_row(&ctx);
  }
  memcpy(saved, next_planes, plane_bytes * planes(state));
  free(next_planes);
  state->step++;
}

/* Bit-by-bit integer square root, the reference for the table version. */
static uint32_t isqrt_reference(uint32_t value) {
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

/* Rounding helpers and codes. */
static void check_codes(void) {
  assert(round_shift(1 << 23, 24) == 1 && round_shift(-(1 << 23), 24) == -1);
  assert(round_shift((1 << 23) - 1, 24) == 0 &&
         round_shift(-(1 << 23) + 1, 24) == 0);
  assert(round_shift((int64_t)3 << 40, 30) == 3 << 10);
  assert(round_shift_unsigned((1u << 23) - 1, 24) == 0 &&
         round_shift_unsigned(1u << 23, 24) == 1);
  /* The unit dt shortcut is exact for both signs. */
  for (int64_t rate = -3000000; rate <= 3000000; rate += 7919) {
    assert(round_shift(rate * RD_Q15_ONE, RATE_SHIFT) ==
           round_shift(rate, UNIT_RATE_SHIFT));
  }
  /* isqrt is exact for the whole 18-bit domain used by floor_b. */
  for (uint32_t value = 0; value < 1u << ROOT_BITS; value++) {
    uint32_t root = isqrt(value);
    assert(root == isqrt_reference(value));
    assert(root * root <= value && (root + 1) * (root + 1) > value);
  }
  /* Decoding is strictly increasing and floor_* returns the largest code
   * decoding within the value, for every code and just below the next. The
   * A table is the rounded division it replaces. */
  for (unsigned code = 0; code <= A_CODE_MAX; code++) {
    assert(decode_a(code) ==
           (int32_t)((((uint32_t)code << RD_VALUE_BITS) + A_CODE_MAX / 2) /
                     A_CODE_MAX));
    assert(code == 0 || decode_a(code) > decode_a(code - 1));
    assert(floor_a(decode_a(code)) == code);
    assert(code == A_CODE_MAX || floor_a(decode_a(code + 1) - 1) == code);
  }
  assert(decode_a(A_CODE_MAX) == RD_VALUE_ONE);
  for (unsigned code = 0; code <= B_CODE_MAX; code++) {
    assert(code == 0 || decode_b(code) > decode_b(code - 1));
    assert(floor_b(decode_b(code)) == code);
    assert(code == B_CODE_MAX || floor_b(decode_b(code + 1) - 1) == code);
  }
  assert(decode_b(B_CODE_MAX) == B_VALUE_MAX);
  assert(decode_b(PACKED_SEED_B) == RD_VALUE_ONE / 4);
  for (unsigned code = 0; code <= Q15_CODE_MAX; code++) {
    assert(floor_q15(decode_q15(code)) == code);
    assert(code == Q15_CODE_MAX || floor_q15(decode_q15(code + 1) - 1) == code);
  }
}

/* Nine-point Laplacian of one column, as the contract states it. */
static int32_t laplacian_reference(const int32_t *up, const int32_t *cur,
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

/* The in-place row pass equals the per-column definition, including the
 * wrapped columns, on pseudo-random rows of every relevant width. */
static void check_laplacian(void) {
  const int widths[3] = {RD_DISPLAY_WIDTH, RD_DISPLAY_WIDTH / 2, 3};
  for (int w = 0; w < 3; w++) {
    int width = widths[w];
    int32_t *up = malloc(width * sizeof(int32_t));
    int32_t *cur = malloc(width * sizeof(int32_t));
    int32_t *down = malloc(width * sizeof(int32_t));
    int32_t *expected = malloc(width * sizeof(int32_t));
    for (int trial = 0; trial < 50; trial++) {
      for (int x = 0; x < width; x++) {
        up[x] =
            (int32_t)(mix32((uint32_t)(trial * 7919 + x)) % (RD_VALUE_ONE + 1));
        cur[x] = (int32_t)(mix32((uint32_t)(trial * 104729 + x + 1)) %
                           (RD_VALUE_ONE + 1));
        down[x] = (int32_t)(mix32((uint32_t)(trial * 15485863 + x + 2)) %
                            (RD_VALUE_ONE + 1));
        if (trial == 0) {
          up[x] = cur[x] = down[x] = RD_VALUE_ONE;
        }
      }
      for (int x = 0; x < width; x++) {
        expected[x] = laplacian_reference(up, cur, down, x, width);
      }
      laplacian_row(up, cur, down, width);
      for (int x = 0; x < width; x++) {
        assert(up[x] == expected[x]);
        assert(trial != 0 || up[x] == 0);
      }
    }
    free(up);
    free(cur);
    free(down);
    free(expected);
  }
}

/* Encode one value with the residual rows cleared, returning the code. */
static unsigned decide(State *state, int species, int32_t value,
                       uint32_t random) {
  StepContext ctx;
  memset(residual_row(state, species), 0,
         (size_t)state->width * sizeof(int32_t));
  begin_step(state, &ctx);
  unsigned code = encode_cell(is_packed(state), species, ctx.residual[species],
                              &ctx.shares[species], 0, value, random);
  memset(residual_row(state, species), 0,
         (size_t)state->width * sizeof(int32_t));
  return code;
}

/* Packed codes: the dithered threshold lies in [1/4, 3/4) of the code
 * step. Q15 codes: the nearest code, halfway upward, whatever the dither. */
static void check_dither(State *state) {
  for (int species = 0; species < SPECIES_COUNT; species++) {
    int packed = is_packed(state);
    unsigned code = species == RD_SPECIES_A ? 40 : 130;
    int32_t low = decode(packed, species, code),
            step = decode(packed, species, code + 1) - low;
    /* random 2^14 is the midpoint threshold: halfway rounds up, one less
     * rounds down. */
    assert(decide(state, species, low + step / 2, DITHER_BASE) == code + 1);
    assert(decide(state, species, low + step / 2 - 1, DITHER_BASE) == code);
    if (!packed) {
      assert(decide(state, species, low + step * 3 / 10, 0) == code);
      assert(decide(state, species, low + step * 7 / 10, DITHER_MASK) ==
             code + 1);
      assert(decide(state, species, low, 0) == code);
      assert(decide(state, species, low + step / 2, 0) == code + 1);
      assert(decide(state, species, low + step / 2 - 1, DITHER_MASK) == code);
      continue;
    }
    /* random 0 is a quarter: 30% rounds up. random 2^15 - 1 is just below
     * three quarters: 70% rounds down, 76% rounds up. */
    assert(decide(state, species, low + step * 3 / 10, 0) == code + 1);
    assert(decide(state, species, low + step * 7 / 10, DITHER_MASK) == code);
    assert(decide(state, species, low + step * 76 / 100, DITHER_MASK) ==
           code + 1);
    /* Exact codes never move, whatever the dither. */
    assert(decide(state, species, low, 0) == code);
    assert(decide(state, species, low, DITHER_MASK) == code);
    /* The largest step of the species decides in the same way through the
     * 32-bit and 64-bit comparisons. */
    unsigned top = code_limit(packed, species) - 1;
    int32_t top_low = decode(packed, species, top),
            top_step = decode(packed, species, top + 1) - top_low;
    assert(decide(state, species, top_low + top_step / 2, DITHER_BASE) ==
           top + 1);
    assert(decide(state, species, top_low + top_step / 2 - 1, DITHER_BASE) ==
           top);
    assert(decide(state, species, top_low + top_step - 1, DITHER_MASK) ==
           top + 1);
    assert(decide(state, species, top_low + top_step / 4 - 1, 0) == top);
  }
}

/* Error diffusion conserves the encoded mass of a row exactly: the decoded
 * codes plus the shares still held equal the sum of the inputs. */
static void check_diffusion(State *state, int species, int32_t value) {
  int packed = is_packed(state);
  int32_t *residual = residual_row(state, species);
  memset(residual, 0, (size_t)state->width * sizeof(int32_t));
  StepContext ctx;
  begin_step(state, &ctx);
  int64_t decoded = 0, held = 0;
  for (int x = 0; x < state->width; x++) {
    uint32_t random = mix32((uint32_t)x) & DITHER_MASK;
    decoded += decode(packed, species,
                      encode_cell(packed, species, ctx.residual[species],
                                  &ctx.shares[species], x, value, random));
  }
  finish_row(&ctx);
  for (int x = 0; x < state->width; x++) {
    held += residual[x];
  }
  held += ctx.shares[species].carry;
  assert(decoded + held == (int64_t)value * state->width);
  /* The decoded row average is within one code step of the input. */
  unsigned code = floor_code(packed, species, value);
  int32_t step =
      decode(packed, species, code + 1) - decode(packed, species, code);
  assert(llabs(decoded - (int64_t)value * state->width) <=
         (int64_t)step * state->width);
  memset(residual, 0, (size_t)state->width * sizeof(int32_t));
}

/* rd_row_rgb2 is the quantized rd_row output packed to two bits per
 * channel, for every palette and display row, and every B code goes
 * through the lookup table or its fallback to the same byte. */
static void check_rgb2(State *state) {
  uint8_t copy[RD_DISPLAY_WIDTH];
  for (int palette = 0; palette <= RD_PALETTE_MONO; palette++) {
    for (int y = 0; y < RD_DISPLAY_HEIGHT; y += y < 8 ? 1 : 7) {
      uint8_t *rgb2 = rd_row_rgb2(state, y, palette);
      assert(rgb2);
      memcpy(copy, rgb2, sizeof copy);
      uint8_t *rgba = rd_row(state, y, palette, 1);
      assert(rgba);
      for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
        uint8_t expected =
            (uint8_t)(ARGB8_OPAQUE | (rgba[x * 4] >> 6) << 4 |
                      (rgba[x * 4 + 1] >> 6) << 2 | (rgba[x * 4 + 2] >> 6));
        assert(copy[x] == expected);
        assert((copy[x] & ARGB8_OPAQUE) == ARGB8_OPAQUE);
      }
    }
    ensure_lookup_table(state, palette);
    const uint8_t *table = lookup_table(state);
    unsigned limit = code_limit(is_packed(state), RD_SPECIES_B);
    for (unsigned code = 0; code <= limit; code++) {
      uint8_t expected = pixel_argb8(state, palette, code);
      uint8_t value =
          is_packed(state) ? table[code] : table[code >> LUT_BUCKET_BITS];
      assert(value == 0 || value == expected);
    }
  }
  /* Invalid arguments return NULL without building a table. */
  assert(!rd_row_rgb2(state, RD_DISPLAY_HEIGHT, 0));
  assert(!rd_row_rgb2(state, 0, RD_PALETTE_MONO + 1));
}

/* Mask levels by definition: 0 in a masked cell, else the chessboard
 * distance to the nearest masked cell without wrapping, capped at
 * RD_MASK_RAMP. */
static int *level_reference(int width, int height, const uint8_t *mask) {
  int stride = (width + 7) / 8;
  int *levels = malloc((size_t)width * height * sizeof(int));
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int level = RD_MASK_RAMP;
      for (int dy = -RD_MASK_RAMP; dy <= RD_MASK_RAMP; dy++) {
        for (int dx = -RD_MASK_RAMP; dx <= RD_MASK_RAMP; dx++) {
          int xx = x + dx, yy = y + dy;
          int d = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
          if (xx >= 0 && xx < width && yy >= 0 && yy < height &&
              mask[yy * stride + xx / 8] >> (xx % 8) & 1 && d < level) {
            level = d;
          }
        }
      }
      levels[y * width + x] = level;
    }
  }
  return levels;
}

/* rd_mask_level reports the defined levels and B is 0 in masked cells. */
static void check_masked_cells(State *state, const int *levels) {
  for (int y = 0; y < state->height; y++) {
    for (int x = 0; x < state->width; x++) {
      int level = levels ? levels[y * state->width + x] : RD_MASK_RAMP;
      assert(rd_mask_level(state, x, y) == level);
      if (level == 0) {
        assert(rd_get(state, x, y, RD_SPECIES_B) == 0);
      }
    }
  }
}

/* A developed field under a rectangle plus scattered cells matches the
 * oracle with the mask levels by definition, keeps B at 0 in the mask, and
 * continues as an unmasked field once the mask is cleared. */
static void check_mask(int mode) {
  size_t size = rd_bytes(mode);
  uint8_t *memory = malloc(size), *reference_memory = malloc(size);
  State *state = rd_init(memory, size, mode, 7);
  State *reference_state = rd_init(reference_memory, size, mode, 7);
  rd_step(state, 60);
  rd_step(reference_state, 60);
  assert(rd_memory(mode, RD_COMPONENT_MASK) ==
         (size_t)state->width * state->height);
  size_t bytes = cm_bytes(state->width, state->height);
  uint8_t *mask = calloc(bytes, 1);
  int stride = (state->width + 7) / 8;
  for (int y = 0; y < state->height; y++) {
    for (int x = 0; x < state->width; x++) {
      int rectangle = x >= state->width / 4 && x < state->width / 2 &&
                      y >= state->height / 3 && y < state->height / 2;
      /* Column 0, the last column, and the last row exercise the wrapped
       * shares and the unwrapped distances. */
      if (rectangle || mix32((uint32_t)(y * state->width + x)) % 97 == 0 ||
          (y == 5 && (x == 0 || x == state->width - 1)) ||
          (y == state->height - 1 && x == 3)) {
        mask[y * stride + x / 8] |= (uint8_t)(1 << (x % 8));
      }
    }
  }
  int *levels = level_reference(state->width, state->height, mask);
  assert(rd_mask(state, mask) == 0 && rd_mask(reference_state, mask) == 0);
  assert(rd_hash(state) == rd_hash(reference_state));
  check_masked_cells(state, levels);
  /* Seeds skip masked cells. */
  rd_seed(state, RD_DISPLAY_WIDTH * 3 / 8, RD_DISPLAY_HEIGHT * 5 / 12, 30);
  rd_seed(reference_state, RD_DISPLAY_WIDTH * 3 / 8, RD_DISPLAY_HEIGHT * 5 / 12,
          30);
  check_masked_cells(state, levels);
  oracle_levels = levels;
  for (int i = 0; i < 25; i++) {
    rd_step(state, 1);
    reference(reference_state);
    assert(rd_hash(state) == rd_hash(reference_state));
    check_masked_cells(state, levels);
  }
  oracle_levels = NULL;
  assert(rd_mask(state, NULL) == 0 && rd_mask(reference_state, NULL) == 0);
  check_masked_cells(state, NULL);
  for (int i = 0; i < 5; i++) {
    rd_step(state, 1);
    reference(reference_state);
    assert(rd_hash(state) == rd_hash(reference_state));
  }
  free(levels);
  free(mask);
  free(memory);
  free(reference_memory);
}

/* The masked share rule on one cell. */
static void check_encode_masked(void) {
  int32_t residual[2] = {17, 99};
  Shares shares = {7, 5, 3};
  encode_masked(residual, &shares, 1);
  assert(residual[0] == 17 && residual[1] == 5);
  assert(shares.carry == 0 && shares.pending == 0 && shares.wrap == 3);
}

/* Clock masks: sizes, argument checks, and the pixel count of the LECO
 * clock verified against the emulator. */
static void check_clock_mask(void) {
  assert(cm_bytes(100, 114) == 1482 && cm_bytes(200, 228) == 5700);
  assert(cm_bytes(0, 114) == 0);
  static uint8_t mask[5700];
  memset(mask, 0xa5, sizeof mask);
  const int bad[][10] = {{150, 171, 0, 14, 50, 2026, 9, 24, 0},
                         {100, 100, 0, 14, 50, 2026, 9, 24, 0},
                         {100, 114, CM_FONT_COUNT, 14, 50, 2026, 9, 24, 0},
                         {100, 114, -1, 14, 50, 2026, 9, 24, 0},
                         {100, 114, 0, 24, 50, 2026, 9, 24, 0},
                         {100, 114, 0, 14, 60, 2026, 9, 24, 0},
                         {100, 114, 0, 14, 50, 10000, 9, 24, 0},
                         {100, 114, 0, 14, 50, 2026, 0, 24, 0},
                         {100, 114, 0, 14, 50, 2026, 13, 24, 0},
                         {100, 114, 0, 14, 50, 2026, 9, 0, 0},
                         {100, 114, 0, 14, 50, 2026, 9, 32, 0},
                         {100, 114, 0, 14, 50, 2026, 9, 24, -1},
                         {100, 114, 0, 14, 50, 2026, 9, 24, CM_MAX_HALO + 1}};
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    const int *b = bad[i];
    assert(cm_build(mask, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                    b[8]) == -1);
  }
  assert(cm_build(NULL, 100, 114, 0, 14, 50, 2026, 9, 24, 0) == -1);
  for (size_t i = 0; i < sizeof mask; i++) {
    assert(mask[i] == 0xa5);
  }
  assert(!cm_layout(-1) && !cm_layout(CM_FONT_COUNT));
  for (int font = 0; font < CM_FONT_COUNT; font++) {
    assert(cm_font_available(font) && cm_layout(font));
  }
  assert(!cm_font_available(CM_FONT_COUNT));
  assert(cm_build(mask, 200, 228, CM_FONT_LECO, 14, 50, 2026, 9, 24, 0) == 0);
  int count = 0;
  for (size_t i = 0; i < 5700; i++) {
    count += __builtin_popcount(mask[i]);
  }
  assert(count == 2427);
}

int main(int argc, char **argv) {
  if (argc > 1) {
    /* Hash mode: print the field hash after `count` steps of `mode`,
     * optionally with the clock mask of `font hour minute year month day
     * halo` installed first. */
    int mode = atoi(argv[1]), count = argc > 2 ? atoi(argv[2]) : 100;
    void *memory = malloc(rd_bytes(mode));
    void *state = rd_init(memory, rd_bytes(mode), mode, 42);
    if (argc > 9) {
      int v[7];
      for (int i = 0; i < 7; i++) {
        v[i] = atoi(argv[3 + i]);
      }
      uint8_t *mask = malloc(cm_bytes(rd_width(state), rd_height(state)));
      if (cm_build(mask, rd_width(state), rd_height(state), v[0], v[1], v[2],
                   v[3], v[4], v[5], v[6]) ||
          rd_mask(state, mask)) {
        fprintf(stderr, "invalid mask arguments\n");
        return 1;
      }
      free(mask);
    }
    rd_step(state, count);
    printf("%u\n", rd_hash(state));
    free(memory);
    return 0;
  }
  assert(rd_bytes(-1) == 0);
  assert(rd_init(NULL, 0, 0, 0) == NULL);
  check_codes();
  check_laplacian();
  check_encode_masked();
  check_clock_mask();
  for (int mode = 0; mode < 3; mode++) {
    size_t size = rd_bytes(mode);
    uint8_t *memory = malloc(size + 16);
    uint8_t *reference_memory = malloc(size);
    uint8_t *snapshot = malloc(size);
    memset(memory, 0xa5, size + 16);
    /* An undersized block is rejected without touching it. */
    assert(!rd_init(memory, size - 1, mode, 42));
    for (size_t i = 0; i < size + 16; i++) {
      assert(memory[i] == 0xa5);
    }
    State *state = rd_init(memory, size, mode, 42);
    State *reference_state = rd_init(reference_memory, size, mode, 42);
    /* Seeds are exact codes: A = 1 outside disks, B = 0.25 inside. */
    assert(rd_get(state, 0, 0, RD_SPECIES_A) == RD_VALUE_ONE ||
           rd_get(state, 0, 0, RD_SPECIES_B) == RD_VALUE_ONE / 4);
    /* Row-buffer stepping matches the full-screen oracle. */
    for (int i = 0; i < 25; i++) {
      rd_step(state, 1);
      reference(reference_state);
      assert(rd_hash(state) == rd_hash(reference_state));
    }
    /* Coefficient endpoints and a non-unit timestep are accepted and still
     * match the oracle. */
    const int trials[3][5] = {
        {0, 0, 0, 0, 0},
        {RD_Q15_ONE, RD_Q15_ONE, RD_Q15_ONE, RD_Q15_ONE, RD_Q15_ONE},
        {950, 1868, RD_Q15_ONE, RD_Q15_ONE / 2, 12345}};
    for (int trial = 0; trial < 3; trial++) {
      const int *t = trials[trial];
      assert(!rd_params(state, t[0], t[1], t[2], t[3], t[4]));
      assert(!rd_params(reference_state, t[0], t[1], t[2], t[3], t[4]));
      rd_step(state, 3);
      for (int j = 0; j < 3; j++) {
        reference(reference_state);
      }
      assert(rd_hash(state) == rd_hash(reference_state));
    }
    assert(
        !rd_params(state, 950, 1868, RD_Q15_ONE, RD_Q15_ONE / 2, RD_Q15_ONE));
    /* Rendering paths agree. */
    check_rgb2(state);
    /* Invalid arguments leave the block and its surroundings untouched. */
    memcpy(snapshot, memory, size);
    assert(rd_params(state, -1, 0, 0, 0, 0) == -1);
    assert(rd_seed(state, RD_DISPLAY_WIDTH, 0, 4) == -1);
    assert(rd_step(state, -1) == -1);
    assert(!rd_row(state, RD_DISPLAY_HEIGHT, 0, 1));
    assert(!rd_row_rgb2(state, -1, 0));
    assert(rd_get(state, -1, 0, RD_SPECIES_A) == -1);
    assert(rd_mask(NULL, NULL) == -1 && rd_mask_level(state, -1, 0) == -1);
    assert(rd_mask_level(state, 0, state->height) == -1);
    assert(rd_width(NULL) == -1);
    assert(rd_width(state) == state->width &&
           rd_height(state) == state->height);
    assert(memcmp(snapshot, memory, size) == 0);
    for (size_t i = size; i < size + 16; i++) {
      assert(memory[i] == 0xa5);
    }
    /* A disk seeded at the origin wraps around to the opposite corner. */
    rd_seed(state, 0, 0, 8);
    assert(rd_get(state, state->width - 1, state->height - 1, RD_SPECIES_B) >
           0);
    /* Concentrations stay within the codable range. */
    for (int i = 0; i < state->width * state->height; i++) {
      int a = rd_get(state, i % state->width, i / state->width, RD_SPECIES_A);
      int b = rd_get(state, i % state->width, i / state->width, RD_SPECIES_B);
      assert(a >= 0 && a <= RD_VALUE_ONE && b >= 0 && b <= RD_VALUE_ONE);
    }
    /* A uniform field stays exactly at the equilibrium A = 1, B = 0, even
     * with every coefficient at its maximum. */
    for (int i = 0; i < state->width * state->height; i++) {
      store_codes(state, i, is_packed(state) ? PACKED_FULL_A : Q15_FULL_A, 0);
    }
    /* The residual rows still hold shares of the previous field; a uniform
     * field is exact only together with zero residuals, as after rd_init. */
    memset(residual_row(state, RD_SPECIES_A), 0,
           (size_t)SPECIES_COUNT * state->width * sizeof(int32_t));
    rd_params(state, RD_Q15_ONE, RD_Q15_ONE, RD_Q15_ONE, RD_Q15_ONE,
              RD_Q15_ONE);
    rd_step(state, 4);
    for (int y = 0; y < state->height; y++) {
      for (int x = 0; x < state->width; x++) {
        assert(rd_get(state, x, y, RD_SPECIES_A) == RD_VALUE_ONE);
        assert(rd_get(state, x, y, RD_SPECIES_B) == 0);
      }
    }
    /* Dithered rounding and error diffusion mass conservation. */
    check_dither(state);
    check_diffusion(state, RD_SPECIES_A, RD_VALUE_ONE / 3);
    check_diffusion(state, RD_SPECIES_B, RD_VALUE_ONE / 7);
    check_diffusion(state, RD_SPECIES_B, 12345);
    /* Unaligned blocks: the State is aligned inside the block and nothing
     * outside the block is written. */
    for (int alignment = 0; alignment < 4; alignment++) {
      uint8_t *unaligned = malloc(size + 8);
      memset(unaligned, 0xa5, size + 8);
      State *aligned_state = rd_init(unaligned + alignment, size, mode, 42);
      assert(aligned_state);
      rd_step(aligned_state, 1);
      assert(rd_row(aligned_state, RD_DISPLAY_HEIGHT - 1, RD_PALETTE_MONO, 1));
      assert(
          rd_row_rgb2(aligned_state, RD_DISPLAY_HEIGHT - 1, RD_PALETTE_CYAN));
      for (int j = 0; j < alignment; j++) {
        assert(unaligned[j] == 0xa5);
      }
      for (size_t j = size + alignment; j < size + 8; j++) {
        assert(unaligned[j] == 0xa5);
      }
      free(unaligned);
    }
    check_mask(mode);
    printf("mode %d: %zu bytes, "
           "oracle/laplacian/codes/rgb2/validation/dither/diffusion/mask OK\n",
           mode, size);
    free(memory);
    free(reference_memory);
    free(snapshot);
  }
  return 0;
}
