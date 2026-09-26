#include "../core/rd.c"
#include "../core/clock_mask.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Full-screen oracle: one step computed from a complete copy of both planes
 * with independent neighbor indexing, so the row-buffer scheme in rd_step is
 * checked against a straightforward implementation. The Laplacian rounding is
 * written out on purpose instead of calling laplacian_row(). Cells are
 * encoded in row order through reference_encode, which keeps the error
 * diffusion on a plain residual row, and the masked cell rule is written
 * out as docs/core.md states it. */
/* Mask levels of the cells for the oracle, from level_reference, or NULL
 * for no mask. */
static const int *oracle_levels;

/* The write of one species as docs/core.md states it, on a plain residual
 * row: the value plus the shares it received, clamped, rounded to a code,
 * and the error divided with C integer division to the right neighbor
 * (carry), the lower-left neighbor (wrap at column 0), the neighbor below,
 * and the lower-right neighbor (pending). */
static unsigned reference_encode(int packed, int species, int32_t *residual,
                                 int32_t *carry, int32_t *pending,
                                 int32_t *wrap, int x, int32_t value,
                                 uint32_t random) {
  value += residual[x] + *carry;
  int32_t limit = value_limit(packed, species);
  value = value < 0 ? 0 : value > limit ? limit : value;
  unsigned code;
  int32_t low;
  if (!packed) {
    code = (unsigned)(value + 256) >> 9;
    low = (int32_t)(code << 9);
  } else {
    code = floor_code(packed, species, value);
    low = decode(packed, species, code);
    if (code < code_limit(packed, species)) {
      int32_t high = decode(packed, species, code + 1);
      if (rounds_up(packed, species, value, low, high, random)) {
        code++;
        low = high;
      }
    }
  }
  int32_t error = value - low;
  int32_t right = error * 7 / 16, below_left = error * 3 / 16,
          below = error * 5 / 16;
  *carry = right;
  residual[x] = *pending + below;
  if (x > 0) {
    residual[x - 1] += below_left;
  } else {
    *wrap = below_left;
  }
  *pending = error - right - below_left - below;
  return code;
}

static void reference(State *state) {
  int cells = state->width * state->height;
  size_t plane_bytes = (size_t)cells * sizeof(uint16_t);
  uint16_t *next_planes = malloc(plane_bytes * planes(state));
  assert(next_planes);
  uint16_t *saved = plane(state, 0);
  StepContext ctx;
  begin_step(state, &ctx);
  int32_t carry_a = 0, pending_a = 0, wrap_a = 0;
  int32_t carry_b = 0, pending_b = 0, wrap_b = 0;
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
      if (!is_packed(state)) {
        /* Version 4 written out from the definition: Q15 codes, 20-fold
         * Laplacian sums, exact products, the rate clamped to int32. */
        const uint16_t *pa = plane(state, RD_SPECIES_A);
        const uint16_t *pb = plane(state, RD_SPECIES_B);
        int64_t sums[SPECIES_COUNT] = {0, 0};
        for (int dy = -1; dy <= 1; dy++) {
          for (int dx = -1; dx <= 1; dx++) {
            int weight = dx == 0 && dy == 0 ? -20 : dx == 0 || dy == 0 ? 4 : 1;
            int index =
                ((y + dy + state->height) % state->height) * state->width +
                (x + dx + state->width) % state->width;
            sums[0] += weight * pa[index];
            sums[1] += weight * pb[index];
          }
        }
        int64_t a = pa[cell_index], b = pb[cell_index];
        int64_t fold_a = (state->da + 10) / 20, fold_b = (state->db + 10) / 20;
        int64_t ab = a * b;
        int64_t reaction = (ab >> 15) * b + (((ab & 32767) * b) >> 15);
        int64_t rate_a =
            fold_a * sums[0] - reaction + state->feed * (32768 - a);
        int64_t rate_b = fold_b * sums[1] + reaction - (int64_t)decay * b;
        assert(rate_a >= INT32_MIN && rate_a <= INT32_MAX);
        rate_b = rate_b < INT32_MIN   ? INT32_MIN
                 : rate_b > INT32_MAX ? INT32_MAX
                                      : rate_b;
        int64_t dt = state->dt;
        next_a = (int32_t)((a << 9) + ((rate_a * dt + (1 << 20)) >> 21));
        next_b = (int32_t)((b << 9) + ((rate_b * dt + (1 << 20)) >> 21));
      } else {
        react_cell(rd_get(state, x, y, RD_SPECIES_A),
                   rd_get(state, x, y, RD_SPECIES_B), laps[0], laps[1],
                   ctx.feed, decay, ctx.da, ctx.db, ctx.dt, ctx.unit_dt,
                   &next_a, &next_b);
      }
      uint32_t dither =
          is_packed(state) ? cell_dither(ctx.step_salt, cell_index) : 0;
      unsigned code_a = reference_encode(
          is_packed(state), RD_SPECIES_A, ctx.residual[0], &carry_a, &pending_a,
          &wrap_a, x, next_a, dither & DITHER_MASK);
      unsigned code_b = 0;
      if (level == 0) {
        /* Masked: B is held at 0, and only the pending share passes. */
        ctx.residual[1][x] = pending_b;
        carry_b = pending_b = 0;
      } else {
        code_b =
            reference_encode(is_packed(state), RD_SPECIES_B, ctx.residual[1],
                             &carry_b, &pending_b, &wrap_b, x, next_b,
                             (dither >> DITHER_B_SHIFT) & DITHER_MASK);
      }
      if (is_packed(state)) {
        next_planes[cell_index] = (uint16_t)((code_a << B_BITS) | code_b);
      } else {
        next_planes[cell_index] = (uint16_t)code_a;
        next_planes[cells + cell_index] = (uint16_t)code_b;
      }
    }
    /* Row end: the wrapped shares reach the last column and column 0. */
    ctx.residual[0][state->width - 1] += wrap_a;
    ctx.residual[1][state->width - 1] += wrap_b;
    ctx.residual[0][0] += pending_a;
    ctx.residual[1][0] += pending_b;
    wrap_a = pending_a = wrap_b = pending_b = 0;
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
  /* The branch-free rounding equals the definition, halfway away from
   * zero, for both signs and every shift used. */
  for (int i = 0; i < 200000; i++) {
    int64_t value = (int64_t)((uint64_t)mix32((uint32_t)i) << 32 |
                              mix32((uint32_t)i * 7919u)) >>
                    (i % 24);
    for (int bits = 15; bits <= 30; bits += 15) {
      int64_t half = (int64_t)1 << (bits - 1);
      int64_t expected =
          value < 0 ? -((-value + half) >> bits) : (value + half) >> bits;
      assert(round_shift(value, bits) == expected);
    }
  }
  /* The constants of the 120-cell render loop are the axis samples. */
  for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
    const int offsets[5] = {-1, 0, 1, 1, 2},
              weights[5] = {204, 102, 0, 153, 51};
    int index, weight;
    axis_sample(x, 120, RD_DISPLAY_WIDTH, &index, &weight);
    assert(index == (3 * (x / 5) + offsets[x % 5] + 120) % 120);
    assert(weight == weights[x % 5]);
  }
  /* The one-multiply lerp equals the two-weight form for Q15 values. */
  for (unsigned left = 0; left <= Q15_CODE_MAX; left += 257) {
    for (unsigned right = 0; right <= Q15_CODE_MAX; right += 263) {
      for (int weight = 0; weight < 256; weight++) {
        assert(lerp256(left, right, weight) ==
               (left * (256 - weight) + right * weight + 128) >> 8);
      }
    }
  }
  assert(lerp256(0, Q15_CODE_MAX, 255) == (Q15_CODE_MAX * 255 + 128) >> 8);
  assert(lerp256(Q15_CODE_MAX, 0, 255) == (Q15_CODE_MAX + 128) >> 8);
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
      /* laplacian_sums gives the 20-fold sums of a row of Q15 codes. */
      uint16_t *codes = malloc(3 * width * sizeof(uint16_t));
      int32_t *sums = malloc(width * sizeof(int32_t));
      for (int x = 0; x < 3 * width; x++) {
        const int32_t *source = x < width ? up : x < 2 * width ? cur : down;
        codes[x] = (uint16_t)((uint32_t)source[x % width] >> Q15_SHIFT);
      }
      laplacian_sums(codes, codes + width, codes + 2 * width, sums, 1, width);
      for (int x = 0; x < width; x++) {
        int left = (x + width - 1) % width, right = (x + 1) % width;
        const uint16_t *u = codes, *c = codes + width, *d = codes + 2 * width;
        int32_t expected_sum =
            LAPLACIAN_AXIAL_WEIGHT * (c[left] + c[right] + u[x] + d[x]) +
            (u[left] + u[right] + d[left] + d[right]) - LAPLACIAN_SCALE * c[x];
        assert(sums[x] == expected_sum);
      }
      free(codes);
      free(sums);
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
/* Floor of a / b for b > 0, written out for the references. */
static long floor_ratio(long a, long b) {
  long q = a / b;
  return q * b > a ? q - 1 : q;
}

/* Interpolated Q15 B at the center of display pixel (x, y), from the
 * definition: the center in cell units, the four surrounding cells with
 * periodic indices, a vertical then a horizontal lerp in 256ths rounded
 * half up. */
static unsigned interpolated_reference(State *state, int x, int y) {
  int w = state->width, h = state->height;
  long qx = floor_ratio(((2L * x + 1) * w - RD_DISPLAY_WIDTH) * 256,
                        2L * RD_DISPLAY_WIDTH);
  long qy = floor_ratio(((2L * y + 1) * h - RD_DISPLAY_HEIGHT) * 256,
                        2L * RD_DISPLAY_HEIGHT);
  long cx = floor_ratio(qx, 256), cy = floor_ratio(qy, 256);
  unsigned wx = (unsigned)(qx - cx * 256), wy = (unsigned)(qy - cy * 256);
  int x0 = (int)((cx % w + w) % w), x1 = (x0 + 1) % w;
  int y0 = (int)((cy % h + h) % h), y1 = (y0 + 1) % h;
  unsigned b00 = (unsigned)rd_get(state, x0, y0, RD_SPECIES_B) >> 9;
  unsigned b01 = (unsigned)rd_get(state, x0, y1, RD_SPECIES_B) >> 9;
  unsigned b10 = (unsigned)rd_get(state, x1, y0, RD_SPECIES_B) >> 9;
  unsigned b11 = (unsigned)rd_get(state, x1, y1, RD_SPECIES_B) >> 9;
  unsigned left = (b00 * (256 - wy) + b01 * wy + 128) >> 8;
  unsigned right = (b10 * (256 - wy) + b11 * wy + 128) >> 8;
  return (left * (256 - wx) + right * wx + 128) >> 8;
}

/* rd_row_rgb2 is the quantized rd_row output packed to two bits per
 * channel, for every palette, both sampling flags, and sampled display
 * rows. Interpolated pixels are the colors of the reference value, and
 * every B code or value goes through a lookup table or its fallback to the
 * same byte. */
static void check_rgb2(State *state) {
  uint8_t copy[RD_DISPLAY_WIDTH];
  for (int palette = 0; palette < RD_PALETTE_COUNT; palette++) {
    for (int sampling = 0; sampling <= RD_ROW_BILINEAR;
         sampling += RD_ROW_BILINEAR) {
      for (int y = 0; y < RD_DISPLAY_HEIGHT; y += y < 8 ? 1 : 7) {
        uint8_t *rgb2 = rd_row_rgb2(state, y, palette, sampling);
        assert(rgb2);
        memcpy(copy, rgb2, sizeof copy);
        uint8_t *rgba = rd_row(state, y, palette, sampling | RD_ROW_QUANTIZE);
        assert(rgba);
        for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
          uint8_t expected =
              (uint8_t)(ARGB8_OPAQUE | (rgba[x * 4] >> 6) << 4 |
                        (rgba[x * 4 + 1] >> 6) << 2 | (rgba[x * 4 + 2] >> 6));
          assert(copy[x] == expected);
          assert((copy[x] & ARGB8_OPAQUE) == ARGB8_OPAQUE);
          if (sampling) {
            unsigned value = interpolated_reference(state, x, y);
            assert(copy[x] == value_argb8(state, palette, decode_q15(value)));
          }
        }
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
    ensure_value_table(state, palette);
    for (unsigned value = 0; value <= Q15_CODE_MAX; value++) {
      assert(value_color(state, value_table(state), palette, value) ==
             value_argb8(state, palette, decode_q15(value)));
    }
  }
  /* rd_row_rgb2_into writes the same bytes into a caller row, only in
   * the requested columns. */
  uint8_t into[RD_DISPLAY_WIDTH + 2];
  for (int flags = 0; flags <= RD_ROW_BILINEAR; flags += RD_ROW_BILINEAR) {
    for (int y = 0; y < RD_DISPLAY_HEIGHT; y += 37) {
      memcpy(copy, rd_row_rgb2(state, y, RD_PALETTE_CYAN, flags), sizeof copy);
      memset(into, 0x5a, sizeof into);
      assert(rd_row_rgb2_into(state, y, RD_PALETTE_CYAN, flags, into + 1, 3,
                              150) == 0);
      for (int x = -1; x <= RD_DISPLAY_WIDTH; x++) {
        assert(into[x + 1] == (x >= 3 && x <= 150 ? copy[x] : 0x5a));
      }
    }
  }
  assert(rd_row_rgb2_into(state, 0, 0, 0, into, 5, 4) == -1);
  assert(rd_row_rgb2_into(state, 0, 0, 0, into, -1, 4) == -1);
  assert(rd_row_rgb2_into(state, 0, 0, 0, into, 0, RD_DISPLAY_WIDTH) == -1);
  assert(rd_row_rgb2_into(state, 0, 0, 0, NULL, 0, 4) == -1);
  assert(rd_row_rgb2_into(state, RD_DISPLAY_HEIGHT, 0, 0, into, 0, 4) == -1);
  /* Invalid arguments return NULL without building a table. */
  assert(!rd_row_rgb2(state, RD_DISPLAY_HEIGHT, 0, 0));
  assert(!rd_row_rgb2(state, 0, RD_PALETTE_COUNT, 0));
  assert(!rd_row(state, 0, RD_PALETTE_COUNT, 0) && !rd_row(state, 0, -1, 0));
  assert(!rd_row_rgb2(state, 0, 0, 4) && !rd_row(state, 0, 0, 4));
}

/* Every channel of every palette with stops is the nearest integer to the
 * exact interpolation between its stops, halves rounded up, over the Q15
 * codes, whose intensity is three times the code. */
static void check_stop_interpolation(State *state) {
  assert(sizeof PALETTE_STOP_COUNT == RD_PALETTE_CUSTOM);
  for (int palette = 0; palette < RD_PALETTE_CUSTOM; palette++) {
    int count = PALETTE_STOP_COUNT[palette];
    if (palette == RD_PALETTE_MONO) {
      assert(count == 0);
      continue;
    }
    assert(count >= 2 && count <= RD_PALETTE_MAX_STOPS);
    for (unsigned code = 0; code * 3 <= RD_Q15_ONE + 2; code++) {
      int intensity = code * 3 > RD_Q15_ONE ? RD_Q15_ONE : (int)code * 3;
      long position = (long)intensity * (count - 1);
      int segment =
          intensity == RD_Q15_ONE ? count - 2 : (int)(position / RD_Q15_ONE);
      long fraction = position - (long)segment * RD_Q15_ONE;
      uint8_t rgb[3];
      value_rgb(state, palette, 0, decode_q15(code), rgb);
      for (int c = 0; c < 3; c++) {
        long low = PALETTE_STOPS[palette][segment][c];
        long high = PALETTE_STOPS[palette][segment + 1][c];
        long exact = low * RD_Q15_ONE + (high - low) * fraction;
        long color = (long)rgb[c] * RD_Q15_ONE;
        assert(exact >= color - RD_Q15_ONE / 2 &&
               exact < color + RD_Q15_ONE / 2);
      }
    }
  }
}

static uint8_t palette_rgb2[2][RD_DISPLAY_HEIGHT][RD_DISPLAY_WIDTH];
static uint8_t palette_rgba[2][RD_DISPLAY_HEIGHT][RD_ROW_BYTES];

/* Two palettes draw the same rows, quantized, with both sampling flags.
 * The first is drawn first, so a stale lookup table for it would show. */
static void check_same_palette(State *state, int first, int second) {
  for (int flags = 0; flags <= RD_ROW_BILINEAR; flags += RD_ROW_BILINEAR) {
    int palettes[2] = {first, second};
    for (int k = 0; k < 2; k++) {
      for (int y = 0; y < RD_DISPLAY_HEIGHT; y++) {
        memcpy(palette_rgb2[k][y], rd_row_rgb2(state, y, palettes[k], flags),
               RD_DISPLAY_WIDTH);
        memcpy(palette_rgba[k][y],
               rd_row(state, y, palettes[k], flags | RD_ROW_QUANTIZE),
               RD_ROW_BYTES);
      }
    }
    assert(!memcmp(palette_rgb2[0], palette_rgb2[1], sizeof palette_rgb2[0]));
    assert(!memcmp(palette_rgba[0], palette_rgba[1], sizeof palette_rgba[0]));
  }
}

/* The custom palette starts as lime, takes the stops rd_palette sets, drops
 * lookup tables built for the old stops, rejects invalid stops without
 * change, and belongs to its handle. Checked in a packed and a Q15 mode,
 * which keep separate lookup tables. */
static void check_custom_palette(void) {
  for (int mode = 0; mode < MODE_COUNT; mode += 3) {
    void *memory = malloc(rd_bytes(mode));
    void *other_memory = malloc(rd_bytes(mode));
    State *state = rd_init(memory, rd_bytes(mode), mode, 42);
    State *other = rd_init(other_memory, rd_bytes(mode), mode, 42);
    check_stop_interpolation(state);
    rd_step(state, 300);
    rd_step(other, 300);
    check_same_palette(state, RD_PALETTE_LIME, RD_PALETTE_CUSTOM);
    const uint8_t *viridis = PALETTE_STOPS[RD_PALETTE_VIRIDIS][0];
    assert(rd_palette(NULL, viridis, 8) == -1);
    assert(rd_palette(state, NULL, 8) == -1);
    assert(rd_palette(state, viridis, 1) == -1);
    assert(rd_palette(state, viridis, RD_PALETTE_MAX_STOPS + 1) == -1);
    check_same_palette(state, RD_PALETTE_CUSTOM, RD_PALETTE_LIME);
    assert(rd_palette(state, viridis, 8) == 0);
    check_same_palette(state, RD_PALETTE_CUSTOM, RD_PALETTE_VIRIDIS);
    assert(rd_palette(state, PALETTE_STOPS[RD_PALETTE_CYAN][0], 2) == 0);
    check_same_palette(state, RD_PALETTE_CUSTOM, RD_PALETTE_CYAN);
    check_same_palette(other, RD_PALETTE_CUSTOM, RD_PALETTE_LIME);
    free(memory);
    free(other_memory);
  }
}

/* The nearest output of every rendering call for modes 0-2, hashed, equals
 * that of the core before interpolated rendering was added: a clock mask,
 * 300 steps, every palette, rd_row with flags 0 and 1, and rd_row_rgb2. */
static void check_nearest_unchanged(void) {
  const uint32_t expected[3] = {1597135921u, 4011507537u, 1149036341u};
  for (int mode = 0; mode < 3; mode++) {
    void *memory = malloc(rd_bytes(mode));
    void *state = rd_init(memory, rd_bytes(mode), mode, 42);
    uint8_t *mask = malloc(cm_bytes(rd_width(state), rd_height(state)));
    cm_build(mask, rd_width(state), rd_height(state), CM_FONT_BITHAM, 13, 57,
             2046, 8, 29, 1);
    rd_mask(state, mask);
    rd_step(state, 300);
    uint32_t hash = FNV_OFFSET_BASIS;
    for (int palette = 0; palette <= RD_PALETTE_MONO; palette++) {
      for (int y = 0; y < RD_DISPLAY_HEIGHT; y++) {
        for (int quantize = 0; quantize < 2; quantize++) {
          const uint8_t *row = rd_row(state, y, palette, quantize);
          for (int i = 0; i < RD_ROW_BYTES; i++) {
            hash = (hash ^ row[i]) * FNV_PRIME;
          }
        }
        const uint8_t *row = rd_row_rgb2(state, y, palette, 0);
        for (int i = 0; i < RD_DISPLAY_WIDTH; i++) {
          hash = (hash ^ row[i]) * FNV_PRIME;
        }
      }
    }
    assert(hash == expected[mode]);
    free(mask);
    free(memory);
  }
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
         mask_bytes(state->width, state->height));
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
  Shares shares = {7, 5, 3, 11};
  encode_masked(residual, &shares, 1);
  /* The held value of column 0 is stored, the pending share becomes the
   * held value of column 1, and the incoming residual is dropped. */
  assert(residual[0] == 11 && residual[1] == 99);
  assert(shares.carry == 0 && shares.pending == 0 && shares.wrap == 3 &&
         shares.held == 5);
}

/* Row callback of cm_draw over a 200 x 228 byte screen. */
static uint8_t *test_row(void *context, int y) {
  return (uint8_t *)context + y * RD_DISPLAY_WIDTH;
}

/* Clock masks: sizes, argument checks, and the pixel count of the LECO
 * clock verified against the emulator. */
static void check_clock_mask(void) {
  assert(cm_bytes(100, 114) == 1482 && cm_bytes(200, 228) == 5700);
  assert(cm_bytes(0, 114) == 0);
  static uint8_t mask[5700];
  memset(mask, 0xa5, sizeof mask);
  const int bad[][10] = {{201, 229, 0, 14, 50, 2026, 9, 24, 0},
                         {40, 45, 0, 14, 50, 2026, 9, 24, 0},
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
  /* cm_draw sets exactly the glyph pixels: the cells of the 200-wide mask
   * at halo 0, for both fonts. */
  for (int font = 0; font < CM_FONT_COUNT; font++) {
    static uint8_t screen[RD_DISPLAY_HEIGHT][RD_DISPLAY_WIDTH];
    memset(screen, 0, sizeof screen);
    assert(cm_draw(test_row, screen, 0xff, font, 14, 50, 2026, 9, 24) == 0);
    assert(cm_build(mask, 200, 228, font, 14, 50, 2026, 9, 24, 0) == 0);
    int drawn = 0;
    for (int y = 0; y < RD_DISPLAY_HEIGHT; y++) {
      for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
        int bit = mask[y * 25 + x / 8] >> (x % 8) & 1;
        assert((screen[y][x] == 0xff) == bit);
        drawn += bit;
      }
    }
    assert(font != CM_FONT_LECO || drawn == 2427);
  }
  assert(cm_draw(NULL, NULL, 0xff, 0, 14, 50, 2026, 9, 24) == -1);
  assert(cm_draw(test_row, NULL, 0xff, 0, 24, 50, 2026, 9, 24) == -1);
}

int main(int argc, char **argv) {
  if (argc == 4 && strcmp(argv[1], "render") == 0) {
    /* Render mode: FNV-1a of every interpolated, quantized lime row after
     * `steps` steps of `mode`. */
    int mode = atoi(argv[2]), count = atoi(argv[3]);
    void *memory = malloc(rd_bytes(mode));
    void *state = rd_init(memory, rd_bytes(mode), mode, 42);
    rd_step(state, count);
    uint32_t hash = FNV_OFFSET_BASIS;
    for (int y = 0; y < RD_DISPLAY_HEIGHT; y++) {
      const uint8_t *row =
          rd_row(state, y, RD_PALETTE_LIME, RD_ROW_QUANTIZE | RD_ROW_BILINEAR);
      for (int i = 0; i < RD_ROW_BYTES; i++) {
        hash = (hash ^ row[i]) * FNV_PRIME;
      }
    }
    printf("%u\n", hash);
    free(memory);
    return 0;
  }
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
  check_nearest_unchanged();
  check_custom_palette();
  for (int mode = 0; mode < MODE_COUNT; mode++) {
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
    assert(!rd_row_rgb2(state, -1, 0, 0));
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
    if (!is_packed(state)) {
      /* The rate of B saturates instead of wrapping: a cell with
       * B = 1 among B = 0 neighbors under feed = kill = 1 has an exact rate
       * of about -3 * 2^30, and must end at B = 0, not 1. A cell with A = 0
       * among A = 1 neighbors reaches the largest possible A rate. */
      int edge = 5 * state->width + 5;
      store_codes(state, edge, 0, Q15_FULL_A);
      rd_params(state, RD_Q15_ONE, RD_Q15_ONE, RD_Q15_ONE, RD_Q15_ONE / 2,
                RD_Q15_ONE);
      memcpy(reference_memory, memory, size);
      rd_step(state, 1);
      reference(reference_state);
      assert(rd_get(state, 5, 5, RD_SPECIES_B) == 0);
      assert(rd_get(state, 5, 5, RD_SPECIES_A) == RD_VALUE_ONE);
      assert(rd_hash(state) == rd_hash(reference_state));
      for (int i = 0; i < state->width * state->height; i++) {
        store_codes(state, i, Q15_FULL_A, 0);
      }
      memset(residual_row(state, RD_SPECIES_A), 0,
             (size_t)SPECIES_COUNT * state->width * sizeof(int32_t));
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
      assert(rd_row_rgb2(aligned_state, RD_DISPLAY_HEIGHT - 1, RD_PALETTE_CYAN,
                         RD_ROW_BILINEAR));
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
