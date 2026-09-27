#include "../core/rd.c"
#include "../core/clock_mask.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* 32-bit integer mixing function (lowbias32) for pseudo-random test data. */
static uint32_t mix32(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  return value ^ (value >> 16);
}

/* Read the codes of one cell. */
static void load_codes(State *state, int cell_index, unsigned *code_a,
                       unsigned *code_b) {
  *code_a = plane(state, RD_SPECIES_A)[cell_index];
  *code_b = plane(state, RD_SPECIES_B)[cell_index];
}

/* Full-screen oracle: one step computed from a complete copy of both planes
 * with independent neighbor indexing, so the row-buffer scheme in rd_step is
 * checked against a straightforward implementation. Cells are encoded in row
 * order through reference_encode, which keeps the error diffusion on a plain
 * residual row, and the masked cell rule is written out as docs/core.md states
 * it. */
/* Mask levels of the cells for the oracle, from level_reference, or NULL
 * for no mask. */
static const int *oracle_levels;

/* Floor of a / b for b > 0 in 64 bits, written out for the oracle. */
static int64_t floor_div64(int64_t a, int64_t b) {
  int64_t q = a / b;
  return q * b > a ? q - 1 : q;
}

/* The FitzHugh-Nagumo resting code of u, (x + 2) / 4 of rest rounded to
 * Q13 as docs/core.md states it. */
static unsigned fhn_rest_reference(int rest) {
  return (unsigned)(16384 + floor_div64(rest + 2, 4));
}

/* The write of one species as docs/core.md states it, on a plain residual
 * row: the value plus the shares it received, clamped, rounded to the
 * nearest Q15 code, and the error divided with C integer division to the
 * right neighbor (carry), the lower-left neighbor (wrap at column 0), the
 * neighbor below, and the lower-right neighbor (pending). */
static unsigned reference_encode(int32_t *residual, int32_t *carry,
                                 int32_t *pending, int32_t *wrap, int x,
                                 int32_t value) {
  value += residual[x] + *carry;
  value = value < 0 ? 0 : value > RD_VALUE_ONE ? RD_VALUE_ONE : value;
  unsigned code = (unsigned)(value + 256) >> 9;
  int32_t error = value - (int32_t)(code << 9);
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
  int cells = RD_GRID_WIDTH * RD_GRID_HEIGHT;
  const int *params = state->params;
  int fhn = state->model == RD_MODEL_FHN;
  size_t plane_bytes = (size_t)cells * sizeof(uint16_t);
  uint16_t *next_planes = malloc(plane_bytes * SPECIES_COUNT);
  assert(next_planes);
  uint16_t *saved = plane(state, 0);
  StepContext ctx;
  begin_step(state, &ctx);
  int32_t carry_a = 0, pending_a = 0, wrap_a = 0;
  int32_t carry_b = 0, pending_b = 0, wrap_b = 0;
  for (int y = 0; y < RD_GRID_HEIGHT; y++) {
    for (int x = 0; x < RD_GRID_WIDTH; x++) {
      int cell_index = y * RD_GRID_WIDTH + x;
      int level = oracle_levels ? oracle_levels[cell_index] : RD_MASK_RAMP;
      int32_t decay = params[GS_FEED] + params[GS_KILL] +
                      (RD_MASK_KILL - params[GS_KILL]) *
                          (RD_MASK_RAMP - level) / RD_MASK_RAMP;
      int32_t next_a, next_b;
      if (fhn) {
        /* Version 5 FitzHugh-Nagumo written out from the definition: x in
         * Q13 from the codes (v on plane 0, u on plane 1), 20-fold
         * Laplacian sums, floored products, and both rates within int32
         * without saturation. */
        const uint16_t *pv = plane(state, RD_SPECIES_A);
        const uint16_t *pu = plane(state, RD_SPECIES_B);
        int64_t sums[SPECIES_COUNT] = {0, 0};
        for (int dy = -1; dy <= 1; dy++) {
          for (int dx = -1; dx <= 1; dx++) {
            int weight = dx == 0 && dy == 0 ? -20 : dx == 0 || dy == 0 ? 4 : 1;
            int index =
                ((y + dy + RD_GRID_HEIGHT) % RD_GRID_HEIGHT) * RD_GRID_WIDTH +
                (x + dx + RD_GRID_WIDTH) % RD_GRID_WIDTH;
            sums[0] += weight * pv[index];
            sums[1] += weight * pu[index];
          }
        }
        int64_t v = pv[cell_index], u = pu[cell_index];
        int64_t u13 = u - 16384, v13 = v - 16384;
        int64_t uu = floor_div64(u13 * u13, 8192);
        int64_t uuu = floor_div64(uu * u13, 8192);
        int64_t k13 = floor_div64(params[FHN_K] + 2, 4);
        int64_t rest13 = floor_div64(params[FHN_REST] + 2, 4);
        int64_t pull =
            (int64_t)RD_MASK_PULL * (RD_MASK_RAMP - level) / RD_MASK_RAMP;
        int64_t fold_u = (params[FHN_DU] + 10) / 20;
        int64_t fold_v = (params[FHN_DV] + 10) / 20;
        int64_t rate_u = fold_u * sums[1] +
                         params[FHN_RU] * (u13 - uuu - v13 + k13) -
                         pull * (u13 - rest13);
        int64_t rate_v =
            fold_v * sums[0] +
            params[FHN_RV] * (u13 - floor_div64(params[FHN_AV] * v13, 32768));
        assert(rate_u >= INT32_MIN && rate_u <= INT32_MAX);
        assert(rate_v >= INT32_MIN && rate_v <= INT32_MAX);
        int64_t dt = params[FHN_DT];
        next_a = (int32_t)(v * 512 + floor_div64(rate_v * dt + (1 << 20),
                                                 (int64_t)1 << 21));
        next_b = (int32_t)(u * 512 + floor_div64(rate_u * dt + (1 << 20),
                                                 (int64_t)1 << 21));
      } else {
        /* Version 4 written out from the definition: Q15 codes, 20-fold
         * Laplacian sums, exact products, the rate clamped to int32. */
        const uint16_t *pa = plane(state, RD_SPECIES_A);
        const uint16_t *pb = plane(state, RD_SPECIES_B);
        int64_t sums[SPECIES_COUNT] = {0, 0};
        for (int dy = -1; dy <= 1; dy++) {
          for (int dx = -1; dx <= 1; dx++) {
            int weight = dx == 0 && dy == 0 ? -20 : dx == 0 || dy == 0 ? 4 : 1;
            int index =
                ((y + dy + RD_GRID_HEIGHT) % RD_GRID_HEIGHT) * RD_GRID_WIDTH +
                (x + dx + RD_GRID_WIDTH) % RD_GRID_WIDTH;
            sums[0] += weight * pa[index];
            sums[1] += weight * pb[index];
          }
        }
        int64_t a = pa[cell_index], b = pb[cell_index];
        int64_t fold_a = (params[GS_DA] + 10) / 20;
        int64_t fold_b = (params[GS_DB] + 10) / 20;
        int64_t ab = a * b;
        int64_t reaction = (ab >> 15) * b + (((ab & 32767) * b) >> 15);
        int64_t rate_a =
            fold_a * sums[0] - reaction + params[GS_FEED] * (32768 - a);
        int64_t rate_b = fold_b * sums[1] + reaction - (int64_t)decay * b;
        assert(rate_a >= INT32_MIN && rate_a <= INT32_MAX);
        rate_b = rate_b < INT32_MIN   ? INT32_MIN
                 : rate_b > INT32_MAX ? INT32_MAX
                                      : rate_b;
        int64_t dt = params[GS_DT];
        next_a = (int32_t)((a << 9) + ((rate_a * dt + (1 << 20)) >> 21));
        next_b = (int32_t)((b << 9) + ((rate_b * dt + (1 << 20)) >> 21));
      }
      unsigned code_a = reference_encode(ctx.residual[0], &carry_a, &pending_a,
                                         &wrap_a, x, next_a);
      unsigned code_b = fhn ? fhn_rest_reference(params[FHN_REST]) : 0;
      if (level == 0) {
        /* Masked: the displayed species is held at its resting code, and
         * only the pending share passes. */
        ctx.residual[1][x] = pending_b;
        carry_b = pending_b = 0;
      } else {
        code_b = reference_encode(ctx.residual[1], &carry_b, &pending_b,
                                  &wrap_b, x, next_b);
      }
      next_planes[cell_index] = (uint16_t)code_a;
      next_planes[cells + cell_index] = (uint16_t)code_b;
    }
    /* Row end: the wrapped shares reach the last column and column 0. */
    ctx.residual[0][RD_GRID_WIDTH - 1] += wrap_a;
    ctx.residual[1][RD_GRID_WIDTH - 1] += wrap_b;
    ctx.residual[0][0] += pending_a;
    ctx.residual[1][0] += pending_b;
    wrap_a = pending_a = wrap_b = pending_b = 0;
  }
  memcpy(saved, next_planes, plane_bytes * SPECIES_COUNT);
  free(next_planes);
  state->step++;
}

/* Rounding helpers and the render constants. */
static void check_codes(void) {
  assert(round_shift(1 << 23, 24) == 1 && round_shift(-(1 << 23), 24) == -1);
  assert(round_shift((1 << 23) - 1, 24) == 0 &&
         round_shift(-(1 << 23) + 1, 24) == 0);
  assert(round_shift((int64_t)3 << 40, 30) == 3 << 10);
  /* round_shift equals the definition, halfway away from zero, for both
   * signs at the shift the renderer uses. */
  for (int i = 0; i < 200000; i++) {
    int64_t value = (int64_t)((uint64_t)mix32((uint32_t)i) << 32 |
                              mix32((uint32_t)i * 7919u)) >>
                    (i % 24);
    int64_t half = (int64_t)1 << (Q15_SHIFT - 1);
    int64_t expected = value < 0 ? -((-value + half) >> Q15_SHIFT)
                                 : (value + half) >> Q15_SHIFT;
    assert(round_shift(value, Q15_SHIFT) == expected);
  }
  /* The constants of the 120-cell render loop are the axis samples. */
  for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
    const int offsets[5] = {-1, 0, 1, 1, 2},
              weights[5] = {204, 102, 0, 153, 51};
    int index, weight;
    axis_sample(x, RD_GRID_WIDTH, RD_DISPLAY_WIDTH, &index, &weight);
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
}

/* laplacian_sums gives the 20-fold nine-point sums of a row of Q15 codes,
 * including the wrapped columns, on pseudo-random rows of the grid width
 * and of a minimal width. A uniform row gives zero. */
static void check_laplacian(void) {
  const int widths[2] = {RD_GRID_WIDTH, 3};
  for (int w = 0; w < 2; w++) {
    int width = widths[w];
    uint16_t *codes = malloc(3 * width * sizeof(uint16_t));
    int32_t *sums = malloc(2 * width * sizeof(int32_t));
    for (int trial = 0; trial < 50; trial++) {
      for (int x = 0; x < 3 * width; x++) {
        codes[x] = trial == 0 ? Q15_CODE_MAX
                              : (uint16_t)(mix32((uint32_t)(trial * 7919 + x)) %
                                           (Q15_CODE_MAX + 1));
      }
      /* Written to every second entry, as rd_step shares one pair row. */
      laplacian_sums(codes, codes + width, codes + 2 * width, sums + 1, 2,
                     width);
      for (int x = 0; x < width; x++) {
        int left = (x + width - 1) % width, right = (x + 1) % width;
        const uint16_t *u = codes, *c = codes + width, *d = codes + 2 * width;
        int32_t expected_sum =
            LAPLACIAN_AXIAL_WEIGHT * (c[left] + c[right] + u[x] + d[x]) +
            (u[left] + u[right] + d[left] + d[right]) - LAPLACIAN_SCALE * c[x];
        assert(sums[2 * x + 1] == expected_sum);
        assert(trial != 0 || sums[2 * x + 1] == 0);
      }
    }
    free(codes);
    free(sums);
  }
}

/* Encode one value with the residual rows cleared, returning the code. */
static unsigned decide(State *state, int species, int32_t value) {
  StepContext ctx;
  memset(residual_row(state, species), 0, RD_GRID_WIDTH * sizeof(int32_t));
  begin_step(state, &ctx);
  unsigned code =
      encode_cell(ctx.residual[species], &ctx.shares[species], 0, value);
  memset(residual_row(state, species), 0, RD_GRID_WIDTH * sizeof(int32_t));
  return code;
}

/* The nearest code, halfway upward, and clamping at both ends. */
static void check_rounding(State *state) {
  for (int species = 0; species < SPECIES_COUNT; species++) {
    unsigned code = species == RD_SPECIES_A ? 40 : 130;
    int32_t low = decode_q15(code), step = decode_q15(code + 1) - low;
    assert(decide(state, species, low) == code);
    assert(decide(state, species, low + step * 3 / 10) == code);
    assert(decide(state, species, low + step / 2 - 1) == code);
    assert(decide(state, species, low + step / 2) == code + 1);
    assert(decide(state, species, low + step * 7 / 10) == code + 1);
    assert(decide(state, species, -12345) == 0);
    assert(decide(state, species, RD_VALUE_ONE) == Q15_CODE_MAX);
    assert(decide(state, species, RD_VALUE_ONE + 12345) == Q15_CODE_MAX);
  }
}

/* Error diffusion conserves the encoded mass of a row exactly: the decoded
 * codes plus the shares still held equal the sum of the inputs. */
static void check_diffusion(State *state, int species, int32_t value) {
  int32_t *residual = residual_row(state, species);
  memset(residual, 0, RD_GRID_WIDTH * sizeof(int32_t));
  StepContext ctx;
  begin_step(state, &ctx);
  int64_t decoded = 0, held = 0;
  for (int x = 0; x < RD_GRID_WIDTH; x++) {
    decoded += decode_q15(
        encode_cell(ctx.residual[species], &ctx.shares[species], x, value));
  }
  finish_row(&ctx);
  for (int x = 0; x < RD_GRID_WIDTH; x++) {
    held += residual[x];
  }
  held += ctx.shares[species].carry;
  assert(decoded + held == (int64_t)value * RD_GRID_WIDTH);
  /* The decoded row average is within one code step of the input. */
  assert(llabs(decoded - (int64_t)value * RD_GRID_WIDTH) <=
         (int64_t)decode_q15(1) * RD_GRID_WIDTH);
  memset(residual, 0, RD_GRID_WIDTH * sizeof(int32_t));
}

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
  int w = RD_GRID_WIDTH, h = RD_GRID_HEIGHT;
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
    for (unsigned value = 0; value <= Q15_CODE_MAX; value++) {
      uint8_t expected = value_argb8(state, palette, decode_q15(value));
      uint8_t entry = table[value >> LUT_BUCKET_BITS];
      assert(entry == 0 || entry == expected);
      assert(value_color(state, table, palette, value) == expected);
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

/* FitzHugh-Nagumo colors from the definition: the intensity is
 * u = 4 (s - 1/2) of the stored fraction s, rounded to Q15 and clamped to
 * [0, 1], so s <= 1/2 shows the dark end of the palette and s >= 3/4 the
 * light end. Gray-Scott shows 3 B. Every channel is the nearest integer to
 * the exact interpolation between the stops, halves rounded up. */
static void check_colors(void) {
  void *memories[RD_MODEL_COUNT];
  State *states[RD_MODEL_COUNT];
  for (int model = 0; model < RD_MODEL_COUNT; model++) {
    memories[model] = malloc(rd_bytes());
    states[model] =
        rd_init_model(memories[model], rd_bytes(), model, 42, NULL, 0);
    assert(states[model]);
  }
  for (int palette = 0; palette < RD_PALETTE_CUSTOM; palette++) {
    int count = PALETTE_STOP_COUNT[palette];
    if (palette == RD_PALETTE_MONO) {
      continue;
    }
    for (int32_t s = 0; s <= RD_VALUE_ONE; s += 4099) {
      int64_t scaled = ((int64_t)s - RD_VALUE_ONE / 2) * 4;
      int64_t fhn_intensity = scaled < 0 ? 0 : (scaled + 256) >> 9;
      fhn_intensity = fhn_intensity > RD_Q15_ONE ? RD_Q15_ONE : fhn_intensity;
      int64_t gray = ((int64_t)s * 3 + 256) >> 9;
      gray = gray > RD_Q15_ONE ? RD_Q15_ONE : gray;
      for (int model = 0; model < RD_MODEL_COUNT; model++) {
        int intensity = (int)(model == RD_MODEL_FHN ? fhn_intensity : gray);
        long position = (long)intensity * (count - 1);
        int segment =
            intensity == RD_Q15_ONE ? count - 2 : (int)(position / RD_Q15_ONE);
        long fraction = position - (long)segment * RD_Q15_ONE;
        uint8_t rgb[3];
        value_rgb(states[model], palette, 0, s, rgb);
        for (int channel = 0; channel < 3; channel++) {
          long low = PALETTE_STOPS[palette][segment][channel];
          long high = PALETTE_STOPS[palette][segment + 1][channel];
          long exact = low * RD_Q15_ONE + (high - low) * fraction;
          long color = (long)rgb[channel] * RD_Q15_ONE;
          assert(exact >= color - RD_Q15_ONE / 2 &&
                 exact < color + RD_Q15_ONE / 2);
          if (model == RD_MODEL_FHN && s <= RD_VALUE_ONE / 2) {
            assert(rgb[channel] == PALETTE_STOPS[palette][0][channel]);
          }
          if (model == RD_MODEL_FHN && s >= RD_VALUE_ONE / 4 * 3) {
            assert(rgb[channel] == PALETTE_STOPS[palette][count - 1][channel]);
          }
        }
      }
    }
  }
  for (int model = 0; model < RD_MODEL_COUNT; model++) {
    free(memories[model]);
  }
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
 * the lookup table built for the old stops, rejects invalid stops without
 * change, and belongs to its handle. */
static void check_custom_palette(void) {
  void *memory = malloc(rd_bytes());
  void *other_memory = malloc(rd_bytes());
  State *state = rd_init(memory, rd_bytes(), 42);
  State *other = rd_init(other_memory, rd_bytes(), 42);
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

/* The Q24 value of the displayed species at rest: B = 0, or the stored
 * fraction of u = rest. */
static int resting_value(State *state) {
  return state->model == RD_MODEL_FHN
             ? (int)fhn_rest_reference(state->params[FHN_REST]) << 9
             : 0;
}

/* rd_mask_level reports the defined levels and the displayed species is at
 * rest in masked cells. */
static void check_masked_cells(State *state, const int *levels) {
  for (int y = 0; y < RD_GRID_HEIGHT; y++) {
    for (int x = 0; x < RD_GRID_WIDTH; x++) {
      int level = levels ? levels[y * RD_GRID_WIDTH + x] : RD_MASK_RAMP;
      assert(rd_mask_level(state, x, y) == level);
      if (level == 0) {
        assert(rd_get(state, x, y, RD_SPECIES_B) == resting_value(state));
      }
    }
  }
}

/* A developed field under a rectangle plus scattered cells matches the
 * oracle with the mask levels by definition, keeps the displayed species at
 * rest in the mask, and continues as an unmasked field once the mask is
 * cleared. */
static void check_mask(int model) {
  size_t size = rd_bytes();
  uint8_t *memory = malloc(size), *reference_memory = malloc(size);
  State *state = rd_init_model(memory, size, model, 7, NULL, 0);
  State *reference_state =
      rd_init_model(reference_memory, size, model, 7, NULL, 0);
  rd_step(state, 60);
  rd_step(reference_state, 60);
  assert(rd_memory(RD_COMPONENT_MASK) ==
         (size_t)RD_GRID_WIDTH * RD_GRID_HEIGHT);
  size_t bytes = cm_bytes(RD_GRID_WIDTH, RD_GRID_HEIGHT);
  uint8_t *mask = calloc(bytes, 1);
  int stride = (RD_GRID_WIDTH + 7) / 8;
  for (int y = 0; y < RD_GRID_HEIGHT; y++) {
    for (int x = 0; x < RD_GRID_WIDTH; x++) {
      int rectangle = x >= RD_GRID_WIDTH / 4 && x < RD_GRID_WIDTH / 2 &&
                      y >= RD_GRID_HEIGHT / 3 && y < RD_GRID_HEIGHT / 2;
      /* Column 0, the last column, and the last row exercise the wrapped
       * shares and the unwrapped distances. */
      if (rectangle || mix32((uint32_t)(y * RD_GRID_WIDTH + x)) % 97 == 0 ||
          (y == 5 && (x == 0 || x == RD_GRID_WIDTH - 1)) ||
          (y == RD_GRID_HEIGHT - 1 && x == 3)) {
        mask[y * stride + x / 8] |= (uint8_t)(1 << (x % 8));
      }
    }
  }
  int *levels = level_reference(RD_GRID_WIDTH, RD_GRID_HEIGHT, mask);
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

/* Pixels of the LECO analog face with the hour hand at angle 360 (3
 * o'clock), the minute hand at 600 (25 minutes), and the date 2046.08.29. */
#define ANALOG_PIXELS 1620

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
    assert(cm_build(mask, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8],
                    1) == -1);
  }
  assert(cm_build(NULL, 100, 114, 0, 14, 50, 2026, 9, 24, 0, 1) == -1);
  for (size_t i = 0; i < sizeof mask; i++) {
    assert(mask[i] == 0xa5);
  }
  assert(!cm_layout(-1) && !cm_layout(CM_FONT_COUNT));
  for (int font = 0; font < CM_FONT_COUNT; font++) {
    assert(cm_font_available(font) && cm_layout(font));
  }
  assert(!cm_font_available(CM_FONT_COUNT));
  assert(cm_build(mask, 200, 228, CM_FONT_LECO, 14, 50, 2026, 9, 24, 0, 1) ==
         0);
  int count = 0;
  for (size_t i = 0; i < 5700; i++) {
    count += __builtin_popcount(mask[i]);
  }
  assert(count == 2427);
  /* cm_draw sets exactly the glyph pixels: the cells of the 200-wide mask
   * at halo 0, for both fonts, with and without the date. Without it the
   * time's ink is centered on the display, the digits 1, 4, 5, and 0
   * spanning the full ink height of the time glyphs. */
  assert(cm_time_top(-1, 1) == -1 && cm_time_top(CM_FONT_COUNT, 0) == -1);
  for (int font = 0; font < CM_FONT_COUNT; font++) {
    assert(cm_time_top(font, 1) == cm_layout(font)->time_top);
    for (int date = 0; date <= 1; date++) {
      static uint8_t screen[RD_DISPLAY_HEIGHT][RD_DISPLAY_WIDTH];
      memset(screen, 0, sizeof screen);
      assert(cm_draw(test_row, screen, 0xff, font, 14, 50, 2026, 9, 24, date) ==
             0);
      assert(cm_build(mask, 200, 228, font, 14, 50, 2026, 9, 24, 0, date) == 0);
      int drawn = 0, first = RD_DISPLAY_HEIGHT, last = -1;
      for (int y = 0; y < RD_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
          int bit = mask[y * 25 + x / 8] >> (x % 8) & 1;
          assert((screen[y][x] == 0xff) == bit);
          drawn += bit;
          if (bit) {
            first = y < first ? y : first;
            last = y;
          }
        }
      }
      assert(font != CM_FONT_LECO || !date || drawn == 2427);
      if (!date) {
        /* The ink rows first to last sit centered, to a pixel. */
        int above = first, below = RD_DISPLAY_HEIGHT - 1 - last;
        assert(above - below >= -1 && above - below <= 1);
        assert(first > cm_layout(font)->time_top);
      }
    }
  }
  assert(cm_draw(NULL, NULL, 0xff, 0, 14, 50, 2026, 9, 24, 1) == -1);
  assert(cm_draw(test_row, NULL, 0xff, 0, 24, 50, 2026, 9, 24, 1) == -1);
}

/* Pixel (x, y) of a 200 x 228 bitmap in the mask format. */
static int bitmap_bit(const uint8_t *bits, int x, int y) {
  return bits[y * 25 + x / 8] >> (x % 8) & 1;
}

static void bitmap_pixel(void *context, int x, int y) {
  uint8_t *bits = context;
  assert(x >= 0 && x < RD_DISPLAY_WIDTH && y >= 0 && y < RD_DISPLAY_HEIGHT);
  bits[y * 25 + x / 8] |= (uint8_t)(1u << (x % 8));
}

/* The hands and the center disk alone, as cm_visit_analog visits them. */
static void hands_bitmap(uint8_t bits[5700], int hour_angle, int minute_angle) {
  memset(bits, 0, 5700);
  cm_hand(hour_angle, CM_HOUR_LENGTH, CM_HOUR_RADIUS, bitmap_pixel, bits);
  cm_hand(minute_angle, CM_MINUTE_LENGTH, CM_MINUTE_RADIUS, bitmap_pixel, bits);
  cm_capsule(CM_DIAL_X, CM_DIAL_Y, CM_DIAL_X, CM_DIAL_Y, CM_CENTER_RADIUS,
             bitmap_pixel, bits);
}

/* Analog face: argument checks, drawing against the mask, geometry,
 * mirror symmetry, the mask of wider halos and coarser grids, and the
 * sweep. */
static void check_analog(void) {
  static uint8_t mask[5700], pixels[5700], other[5700];
  memset(mask, 0xa5, sizeof mask);
  const int bad[][10] = {{100, 114, 0, -1, 600, 2046, 8, 29, 1},
                         {100, 114, 0, CM_TURN, 600, 2046, 8, 29, 1},
                         {100, 114, 0, 360, -1, 2046, 8, 29, 1},
                         {100, 114, 0, 360, CM_TURN, 2046, 8, 29, 1},
                         {100, 114, CM_FONT_COUNT, 360, 600, 2046, 8, 29, 1},
                         {100, 114, -1, 360, 600, 2046, 8, 29, 1},
                         {201, 229, 0, 360, 600, 2046, 8, 29, 1},
                         {40, 45, 0, 360, 600, 2046, 8, 29, 1},
                         {100, 100, 0, 360, 600, 2046, 8, 29, 1},
                         {100, 114, 0, 360, 600, 10000, 8, 29, 1},
                         {100, 114, 0, 360, 600, 2046, 0, 29, 1},
                         {100, 114, 0, 360, 600, 2046, 13, 29, 1},
                         {100, 114, 0, 360, 600, 2046, 8, 0, 1},
                         {100, 114, 0, 360, 600, 2046, 8, 32, 1},
                         {100, 114, 0, 360, 600, 2046, 8, 29, -1},
                         {100, 114, 0, 360, 600, 2046, 8, 29, CM_MAX_HALO + 1}};
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    const int *b = bad[i];
    assert(cm_build_analog(mask, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                           b[8], 1) == -1);
  }
  assert(cm_build_analog(NULL, 100, 114, 0, 360, 600, 2046, 8, 29, 1, 1) == -1);
  for (size_t i = 0; i < sizeof mask; i++) {
    assert(mask[i] == 0xa5);
  }
  assert(cm_draw_analog(NULL, NULL, 0xff, 0, 360, 600, 2046, 8, 29, 1) == -1);
  assert(cm_draw_analog(test_row, NULL, 0xff, 0, CM_TURN, 600, 2046, 8, 29,
                        1) == -1);
  assert(cm_draw_analog(test_row, NULL, 0xff, 0, 360, 600, 2046, 13, 29, 1) ==
         -1);
  assert(cm_analog_date_top(-1) == -1 &&
         cm_analog_date_top(CM_FONT_COUNT) == -1);
  /* cm_draw_analog sets exactly the pixels of the 200-wide mask at halo
   * 0, for both fonts. */
  for (int font = 0; font < CM_FONT_COUNT; font++) {
    static uint8_t screen[RD_DISPLAY_HEIGHT][RD_DISPLAY_WIDTH];
    memset(screen, 0, sizeof screen);
    assert(cm_draw_analog(test_row, screen, 0xff, font, 360, 600, 2046, 8, 29,
                          1) == 0);
    assert(cm_build_analog(mask, 200, 228, font, 360, 600, 2046, 8, 29, 0, 1) ==
           0);
    int drawn = 0;
    for (int y = 0; y < RD_DISPLAY_HEIGHT; y++) {
      for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
        int bit = bitmap_bit(mask, x, y);
        assert((screen[y][x] == 0xff) == bit);
        drawn += bit;
      }
    }
    assert(font != CM_FONT_LECO || drawn == ANALOG_PIXELS);
  }
  /* Without the date the face is the hands and the disk alone. */
  for (int font = 0; font < CM_FONT_COUNT; font++) {
    static uint8_t screen[RD_DISPLAY_HEIGHT][RD_DISPLAY_WIDTH];
    memset(screen, 0, sizeof screen);
    assert(cm_draw_analog(test_row, screen, 0xff, font, 360, 600, 2046, 8, 29,
                          0) == 0);
    assert(cm_build_analog(mask, 200, 228, font, 360, 600, 2046, 8, 29, 0, 0) ==
           0);
    hands_bitmap(pixels, 360, 600);
    assert(memcmp(mask, pixels, sizeof mask) == 0);
    for (int y = 0; y < RD_DISPLAY_HEIGHT; y++) {
      for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
        assert((screen[y][x] == 0xff) == bitmap_bit(mask, x, y));
      }
    }
  }
  /* Every row of a capsule is one run holding the seed: the scan visits
   * exactly the pixels within the radius of the segment, for every angle
   * of both hands. */
  for (int angle = 0; angle < CM_TURN; angle++) {
    for (int hand = 0; hand < 2; hand++) {
      int length = hand ? CM_MINUTE_LENGTH : CM_HOUR_LENGTH;
      int r = hand ? CM_MINUTE_RADIUS : CM_HOUR_RADIUS;
      int dx = cm_scale(length, cm_sin(angle));
      int dy = -cm_scale(length, cm_cos(angle));
      memset(pixels, 0, sizeof pixels);
      cm_hand(angle, length, r, bitmap_pixel, pixels);
      for (int y = 0; y < RD_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
          int inside = cm_inside(x, y, CM_DIAL_X, CM_DIAL_Y, dx, dy,
                                 dx * dx + dy * dy, r * r);
          assert(bitmap_bit(pixels, x, y) == inside);
        }
      }
    }
  }
  /* Geometry over every time: the hands and the disk stay within the dial
   * and the date line starts at its row with an empty gap above it. */
  for (int font = 0; font < CM_FONT_COUNT; font++) {
    int date_first = font == CM_FONT_LECO ? 210 : 201;
    int date_last = font == CM_FONT_LECO ? 223 : 221;
    for (int t = 0; t < 720; t++) {
      assert(cm_build_analog(mask, 200, 228, font,
                             cm_hour_angle(t / 60, t % 60),
                             cm_minute_angle(t % 60), 2046, 8, 29, 0, 1) == 0);
      int first = -1, last = -1;
      for (int y = 0; y < RD_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
          if (!bitmap_bit(mask, x, y)) {
            continue;
          }
          if (y <= 186) {
            assert(x >= 18 && x <= 182 && y >= 22);
          } else {
            assert(y >= date_first && y <= date_last);
            first = first < 0 ? y : first;
            last = y;
          }
        }
      }
      assert(first == date_first && last == date_last);
    }
  }
  /* Mirrored times give mirrored hands: left to right about x = 100 and
   * top to bottom about y = 104. */
  for (int a = 0; a < CM_TURN; a += 15) {
    for (int b = 0; b < CM_TURN; b += 90) {
      int bb = (b + a / 15 * 7) % CM_TURN;
      hands_bitmap(pixels, a, bb);
      hands_bitmap(other, (CM_TURN - a) % CM_TURN, (CM_TURN - bb) % CM_TURN);
      for (int y = 0; y < RD_DISPLAY_HEIGHT; y++) {
        assert(!bitmap_bit(pixels, 0, y));
        for (int x = 1; x < RD_DISPLAY_WIDTH; x++) {
          assert(bitmap_bit(pixels, x, y) == bitmap_bit(other, 200 - x, y));
        }
      }
      hands_bitmap(other, (CM_TURN * 3 / 2 - a) % CM_TURN,
                   (CM_TURN * 3 / 2 - bb) % CM_TURN);
      for (int y = 0; y < RD_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
          int mirrored = y <= 208 ? bitmap_bit(other, x, 208 - y) : 0;
          assert(bitmap_bit(pixels, x, y) == mirrored);
        }
      }
    }
  }
  /* The mask of a grid and halo is the splat of every face pixel. */
  assert(cm_build_analog(pixels, 200, 228, CM_FONT_BITHAM, 1438, 1416, 2046, 8,
                         29, 0, 1) == 0);
  const int widths[3] = {100, 120, 200};
  const int halos[3] = {0, 1, 3};
  for (int w = 0; w < 3; w++) {
    int width = widths[w], height = width * 228 / 200;
    for (int h = 0; h < 3; h++) {
      memset(other, 0, sizeof other);
      for (int y = 0; y < RD_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
          if (bitmap_bit(pixels, x, y)) {
            cm_splat(other, width, height, x, y, halos[h]);
          }
        }
      }
      assert(cm_build_analog(mask, width, height, CM_FONT_BITHAM, 1438, 1416,
                             2046, 8, 29, halos[h], 1) == 0);
      assert(memcmp(mask, other, cm_bytes(width, height)) == 0);
    }
  }
  /* The sweep: exact ends, the shorter way round, monotone, through 1439
   * before 0. */
  assert(cm_hour_angle(11, 59) == 1438 && cm_hour_angle(23, 59) == 1438);
  assert(cm_hour_angle(12, 0) == 0 && cm_hour_angle(24, 0) == -1);
  assert(cm_hour_angle(0, 60) == -1 && cm_hour_angle(-1, 0) == -1);
  assert(cm_minute_angle(59) == 1416 && cm_minute_angle(0) == 0);
  assert(cm_minute_angle(60) == -1 && cm_minute_angle(-1) == -1);
  assert(cm_sweep_angle(-1, 0, 0, 1000) == -1);
  assert(cm_sweep_angle(0, CM_TURN, 0, 1000) == -1);
  assert(cm_sweep_angle(0, 24, 0, 0) == -1);
  assert(cm_sweep_angle(0, 24, 0, 1 << 21) == -1);
  assert(cm_sweep_angle(0, 24, 5, (1 << 21) - 1) == 0);
  const int sweeps[3][2] = {{1416, 0}, {1430, 10}, {10, 1430}};
  for (int s = 0; s < 3; s++) {
    int from = sweeps[s][0], to = sweeps[s][1];
    int forward = s < 2;
    assert(cm_sweep_angle(from, to, -5, 1000) == from);
    assert(cm_sweep_angle(from, to, 0, 1000) == from);
    assert(cm_sweep_angle(from, to, 1000, 1000) == to);
    assert(cm_sweep_angle(from, to, 5000, 1000) == to);
    int previous = from, passed = 0;
    for (int t = 1; t <= 1000; t++) {
      int angle = cm_sweep_angle(from, to, t, 1000);
      int moved = ((angle - previous) * (forward ? 1 : -1) + CM_TURN) % CM_TURN;
      assert(moved >= 0 && moved <= 2);
      passed |= angle == 1439;
      previous = angle;
    }
    assert(previous == to && passed);
  }
  assert(cm_sweep_angle(1416, 0, 500, 1000) > 1416);
}

/* The vector of the model's defaults, written out. */
static const int *default_params(int model) { return DEFAULT_PARAMS[model]; }

/* Set every cell of a Q15 state to codes a and b and clear the residual
 * rows, so that the field is exact as after rd_init. */
static void fill_codes(State *state, unsigned a, unsigned b) {
  for (int i = 0; i < RD_GRID_WIDTH * RD_GRID_HEIGHT; i++) {
    store_codes(state, i, a, b);
  }
  memset(residual_row(state, RD_SPECIES_A), 0,
         (size_t)SPECIES_COUNT * RD_GRID_WIDTH * sizeof(int32_t));
}

/* The FitzHugh-Nagumo resting code of v, rest / av with C division, as
 * docs/core.md states it. */
static unsigned fhn_rest_v_reference(const int *params) {
  int64_t rest13 = floor_div64(params[FHN_REST] + 2, 4);
  int64_t v13 = params[FHN_AV] ? rest13 * 32768 / params[FHN_AV]
                               : floor_div64(params[FHN_K] + 2, 4);
  v13 = v13 < -16384 ? -16384 : v13 > 16384 ? 16384 : v13;
  return (unsigned)(16384 + v13);
}

/* The largest FitzHugh-Nagumo rates of either sign fit int32: one step of
 * fields built around the extreme cells matches the oracle, which asserts
 * the bound of every rate. With k = rest = 1 a masked u = 0, v = 0 cell
 * among u = v = 1 has the largest rate_u and an unmasked u = 1, v = 0 cell
 * among v = 1 the largest rate_v. The mirror field with k = rest = -1 has
 * the most negative ones. */
static void check_fhn_extremes(void) {
  size_t size = rd_bytes();
  uint8_t *memory = malloc(size), *reference_memory = malloc(size);
  for (int sign = -1; sign <= 1; sign += 2) {
    State *state = rd_init_model(memory, size, RD_MODEL_FHN, 1, NULL, 0);
    const int params[RD_FHN_PARAMS] = {
        RD_Q15_ONE, RD_Q15_ONE,        RD_FHN_RU_MAX,
        RD_Q15_ONE, RD_Q15_ONE,        sign * RD_Q15_ONE,
        RD_Q15_ONE, sign * RD_Q15_ONE, 0};
    assert(rd_set_params(state, params, RD_FHN_PARAMS) == 0);
    unsigned high = sign > 0 ? Q15_CODE_MAX : 0, low = Q15_CODE_MAX - high;
    int w = RD_GRID_WIDTH, first = 5 * w + 5, second = 20 * w + 20;
    uint8_t *mask = calloc(cm_bytes(w, RD_GRID_HEIGHT), 1);
    mask[5 * ((w + 7) / 8)] = 1 << 5;
    fill_codes(state, high, high);
    assert(rd_mask(state, mask) == 0);
    /* rd_mask set the masked u to rest, the extreme cells are written after
     * it. */
    store_codes(state, first, low, low);
    store_codes(state, second, low, high);
    memcpy(reference_memory, memory, size);
    State *reference_state =
        (State *)(reference_memory + ((uint8_t *)state - memory));
    int *levels = level_reference(w, RD_GRID_HEIGHT, mask);
    oracle_levels = levels;
    rd_step(state, 1);
    reference(reference_state);
    oracle_levels = NULL;
    assert(rd_hash(state) == rd_hash(reference_state));
    free(levels);
    free(mask);
  }
  free(memory);
  free(reference_memory);
}

/* FitzHugh-Nagumo initialization: the resting field with v = rest / av,
 * disks of u = rest + 0.5 that leave v, and the broken wave of init 1,
 * which then steps like the oracle. */
static void check_fhn_init(void) {
  size_t size = rd_bytes();
  uint8_t *memory = malloc(size), *reference_memory = malloc(size);
  /* fhn-hex and fhn-spiral of lib/presets.ts. */
  const int hex[RD_FHN_PARAMS] = {1311,  32768, 860,   2150, 19661,
                                  -7209, 32768, -9584, 0};
  const int spiral[RD_FHN_PARAMS] = {6554,  0,     8192,   410, 32768,
                                     -9830, 32768, -21936, 1};
  State *state =
      rd_init_model(memory, size, RD_MODEL_FHN, 42, hex, RD_FHN_PARAMS);
  unsigned rest = fhn_rest_reference(hex[FHN_REST]);
  unsigned rest_v = fhn_rest_v_reference(hex);
  assert(rest == 13988 && rest_v == 12391);
  int seeded = 0;
  for (int i = 0; i < RD_GRID_WIDTH * RD_GRID_HEIGHT; i++) {
    unsigned v, u;
    load_codes(state, i, &v, &u);
    assert(v == rest_v);
    assert(u == rest || u == rest + 4096);
    seeded += u != rest;
  }
  assert(seeded > 0 && seeded < RD_GRID_WIDTH * RD_GRID_HEIGHT / 2);
  /* v = rest / av is clamped to the codes. */
  const int low_v[RD_FHN_PARAMS] = {0, 0, 0, 0, 1, 0, 0, -RD_Q15_ONE, 0};
  const int high_v[RD_FHN_PARAMS] = {0, 0, 0, 0, 1, 0, 0, RD_Q15_ONE, 0};
  assert(fhn_rest_v_reference(low_v) == 0);
  assert(fhn_rest_v_reference(high_v) == Q15_CODE_MAX);
  /* With av = 0 the resting v is k, at u = rest = 0. */
  const int no_av[RD_FHN_PARAMS] = {0, 0, 0, 0, 0, 8192, 0, 0, 0};
  assert(fhn_rest_v_reference(no_av) == 16384 + 2048);
  {
    State *flat =
        rd_init_model(memory, size, RD_MODEL_FHN, 42, no_av, RD_FHN_PARAMS);
    unsigned v, u;
    load_codes(flat, 0, &v, &u);
    assert(v == 16384 + 2048);
  }
  for (int side = 0; side < 2; side++) {
    State *clamped = rd_init_model(memory, size, RD_MODEL_FHN, 42,
                                   side ? high_v : low_v, RD_FHN_PARAMS);
    unsigned v, u;
    load_codes(clamped, 0, &v, &u);
    assert(v == (side ? Q15_CODE_MAX : 0));
    assert(u == (side ? 24576u : 8192u) || u == (side ? 28672u : 12288u));
  }
  state = rd_init_model(memory, size, RD_MODEL_FHN, 42, hex, RD_FHN_PARAMS);
  /* A disk at the origin wraps to the opposite corner and leaves v. */
  store_codes(state, RD_GRID_WIDTH * RD_GRID_HEIGHT - 1, 5, rest);
  assert(rd_seed(state, 0, 0, 8) == 0);
  unsigned v, u;
  load_codes(state, RD_GRID_WIDTH * RD_GRID_HEIGHT - 1, &v, &u);
  assert(v == 5 && u == rest + 4096);

  state = rd_init_model(memory, size, RD_MODEL_FHN, 42, spiral, RD_FHN_PARAMS);
  State *reference_state = rd_init_model(reference_memory, size, RD_MODEL_FHN,
                                         7, spiral, RD_FHN_PARAMS);
  rest = fhn_rest_reference(spiral[FHN_REST]);
  rest_v = fhn_rest_v_reference(spiral);
  int excited = 0, refractory = 0;
  for (int y = 0; y < RD_GRID_HEIGHT; y++) {
    for (int x = 0; x < RD_GRID_WIDTH; x++) {
      int dx = x * RD_DISPLAY_WIDTH / RD_GRID_WIDTH;
      int dy = y * RD_DISPLAY_HEIGHT / RD_GRID_HEIGHT;
      int wave = dx < 100 && dy >= 190 && dy < 198;
      int back = dx < 100 && dy >= 174 && dy < 190;
      load_codes(state, y * RD_GRID_WIDTH + x, &v, &u);
      assert(u == (wave ? 24576 : rest));
      assert(v == (back ? 24576 : rest_v));
      excited += wave;
      refractory += back;
    }
  }
  assert(excited > 0 && refractory == 2 * excited);
  /* The wave does not use the seed. */
  assert(rd_hash(state) == rd_hash(reference_state));
  for (int i = 0; i < 20; i++) {
    rd_step(state, 1);
    reference(reference_state);
    assert(rd_hash(state) == rd_hash(reference_state));
  }
  free(memory);
  free(reference_memory);
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "defaults") == 0) {
    /* The default parameter vector of a model, for tests/adapter.mjs. */
    int model = atoi(argv[2]);
    if (model < 0 || model >= RD_MODEL_COUNT) {
      return 1;
    }
    for (int i = 0; i < PARAM_COUNT[model]; i++) {
      printf(i ? " %d" : "%d", default_params(model)[i]);
    }
    printf("\n");
    return 0;
  }
  if (argc == 4 && strcmp(argv[1], "render") == 0) {
    /* Render mode: FNV-1a of every interpolated, quantized lime row after
     * `steps` steps of `model` (default parameters). */
    int model = atoi(argv[2]), count = atoi(argv[3]);
    void *memory = malloc(rd_bytes());
    void *state = rd_init_model(memory, rd_bytes(), model, 42, NULL, 0);
    if (!state) {
      fprintf(stderr, "invalid model\n");
      return 1;
    }
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
    /* Hash mode: print the field hash after `count` steps of `model` with
     * its default parameters, optionally with the clock mask of
     * `font hour minute year month day halo`, or of the analog face with
     * `analog font hour_angle minute_angle year month day halo`, installed
     * first. A last argument `p=v0,v1,...` replaces the default parameter
     * vector, so a preset other than the default can be pinned by a golden
     * hash. */
    int model = atoi(argv[1]), count = argc > 2 ? atoi(argv[2]) : 100;
    int params[RD_PARAM_MAX], param_count = 0;
    if (argc > 3 && strncmp(argv[argc - 1], "p=", 2) == 0) {
      const char *cursor = argv[argc - 1] + 2;
      while (*cursor && param_count < RD_PARAM_MAX) {
        char *end;
        params[param_count++] = (int)strtol(cursor, &end, 10);
        if (end == cursor) {
          fprintf(stderr, "invalid parameter vector\n");
          return 1;
        }
        cursor = *end == ',' ? end + 1 : end;
      }
      argc--;
    }
    void *memory = malloc(rd_bytes());
    void *state = rd_init_model(memory, rd_bytes(), model, 42,
                                param_count ? params : NULL, param_count);
    if (!state) {
      fprintf(stderr, "invalid model or parameters\n");
      return 1;
    }
    int analog = argc > 10 && strcmp(argv[3], "analog") == 0;
    if (argc > 9) {
      int v[7];
      for (int i = 0; i < 7; i++) {
        v[i] = atoi(argv[3 + analog + i]);
      }
      uint8_t *mask = malloc(cm_bytes(rd_width(state), rd_height(state)));
      int built =
          analog ? cm_build_analog(mask, rd_width(state), rd_height(state),
                                   v[0], v[1], v[2], v[3], v[4], v[5], v[6], 1)
                 : cm_build(mask, rd_width(state), rd_height(state), v[0], v[1],
                            v[2], v[3], v[4], v[5], v[6], 1);
      if (built || rd_mask(state, mask)) {
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
  assert(rd_memory(-1) == 0 && rd_memory(RD_COMPONENT_COUNT) == 0);
  assert(rd_init(NULL, 0, 0) == NULL);
  assert(rd_model(NULL) == -1);
  /* The parameter vector queries without a handle. */
  assert(rd_param_count(RD_MODEL_GRAY_SCOTT) == RD_GRAY_SCOTT_PARAMS);
  assert(rd_param_count(RD_MODEL_FHN) == RD_FHN_PARAMS);
  assert(rd_param_count(-1) == -1 && rd_param_count(RD_MODEL_COUNT) == -1);
  for (int model = 0; model < RD_MODEL_COUNT; model++) {
    int count = PARAM_COUNT[model];
    int values[RD_PARAM_MAX];
    memcpy(values, default_params(model), sizeof values);
    assert(rd_check_params(model, values, count) == 0);
    assert(rd_check_params(model, values, count - 1) == -1);
    assert(rd_check_params(model, NULL, count) == -1);
    values[0] = -1;
    assert(rd_check_params(model, values, count) == -1);
  }
  assert(rd_check_params(RD_MODEL_COUNT, default_params(0), 5) == -1);
  check_codes();
  check_laplacian();
  check_encode_masked();
  check_clock_mask();
  check_analog();
  check_colors();
  check_custom_palette();
  /* Coefficient endpoints and a non-unit timestep of each model. */
  const int gs_trials[3][RD_GRAY_SCOTT_PARAMS] = {
      {0, 0, 0, 0, 0},
      {RD_Q15_ONE, RD_Q15_ONE, RD_Q15_ONE, RD_Q15_ONE, RD_Q15_ONE},
      {950, 1868, RD_Q15_ONE, RD_Q15_ONE / 2, 12345}};
  const int fhn_trials[4][RD_FHN_PARAMS] = {
      {0, 0, 0, 0, 0, 0, 0, 0, 0},
      {32768, 32768, 8192, 32768, 32768, 32768, 32768, 32768, 0},
      {32768, 32768, 8192, 32768, 32768, -32768, 32768, -32768, 0},
      {1638, 32768, 328, 819, 19661, 1000, 12345, -3000, 1}};
  for (int model = 0; model < RD_MODEL_COUNT; model++) {
    int fhn = model == RD_MODEL_FHN, count = PARAM_COUNT[model];
    {
      size_t size = rd_bytes();
      uint8_t *memory = malloc(size + 16);
      uint8_t *reference_memory = malloc(size);
      uint8_t *snapshot = malloc(size);
      memset(memory, 0xa5, size + 16);
      /* An undersized block, an unknown model, and invalid parameters are
       * rejected without touching the block. */
      assert(!rd_init_model(memory, size - 1, model, 42, NULL, 0));
      assert(!rd_init_model(memory, size, RD_MODEL_COUNT, 42, NULL, 0));
      assert(!rd_init_model(memory, size, -1, 42, NULL, 0));
      assert(!rd_init_model(memory, size, model, 42, default_params(model),
                            count - 1));
      const int bad[RD_PARAM_MAX] = {-1};
      assert(!rd_init_model(memory, size, model, 42, bad, count));
      for (size_t i = 0; i < size + 16; i++) {
        assert(memory[i] == 0xa5);
      }
      State *state = rd_init_model(memory, size, model, 42, NULL, 0);
      State *reference_state =
          rd_init_model(reference_memory, size, model, 42, NULL, 0);
      assert(rd_model(state) == model);
      /* The memory query is exact: the last area, the mask levels, ends
       * where the components after the alignment padding end. */
      assert(mask_levels(state) + CELLS ==
             (uint8_t *)state + size - rd_memory(RD_COMPONENT_ALIGNMENT));
      if (!fhn) {
        /* Seeds are exact codes: A = 1 outside disks, B = 0.25 inside. */
        assert(rd_get(state, 0, 0, RD_SPECIES_A) == RD_VALUE_ONE ||
               rd_get(state, 0, 0, RD_SPECIES_B) == RD_VALUE_ONE / 4);
      }
      /* Row-buffer stepping matches the full-screen oracle. */
      for (int i = 0; i < 25; i++) {
        rd_step(state, 1);
        reference(reference_state);
        assert(rd_hash(state) == rd_hash(reference_state));
      }
      /* Coefficient endpoints and a non-unit timestep are accepted and
       * still match the oracle. */
      for (int trial = 0; trial < (fhn ? 4 : 3); trial++) {
        const int *t = fhn ? fhn_trials[trial] : gs_trials[trial];
        assert(!rd_set_params(state, t, count));
        assert(!rd_set_params(reference_state, t, count));
        rd_step(state, 3);
        for (int j = 0; j < 3; j++) {
          reference(reference_state);
        }
        assert(rd_hash(state) == rd_hash(reference_state));
      }
      assert(!rd_set_params(state, default_params(model), count));
      /* Rendering paths agree. */
      check_rgb2(state);
      /* Invalid arguments leave the block and its surroundings untouched. */
      memcpy(snapshot, memory, size);
      int values[RD_PARAM_MAX];
      memcpy(values, default_params(model), sizeof values);
      assert(rd_set_params(state, NULL, count) == -1);
      assert(rd_set_params(state, values, count - 1) == -1);
      assert(rd_set_params(state, values, count + 1) == -1);
      assert(rd_set_params(NULL, values, count) == -1);
      for (int i = 0; i < count; i++) {
        int low = fhn && (i == FHN_K || i == FHN_REST) ? -RD_Q15_ONE : 0;
        int high = !fhn            ? RD_Q15_ONE
                   : i == FHN_RU   ? RD_FHN_RU_MAX
                   : i == FHN_INIT ? 1
                                   : RD_Q15_ONE;
        for (int side = 0; side < 2; side++) {
          values[i] = side ? high + 1 : low - 1;
          assert(rd_set_params(state, values, count) == -1);
        }
        values[i] = default_params(model)[i];
      }
      assert(rd_params(state, -1, 0, 0, 0, 0) == -1);
      if (fhn) {
        /* rd_params is the Gray-Scott form only. */
        assert(rd_params(state, 950, 1868, RD_Q15_ONE, RD_Q15_ONE / 2,
                         RD_Q15_ONE) == -1);
      }
      assert(rd_seed(state, RD_DISPLAY_WIDTH, 0, 4) == -1);
      assert(rd_step(state, -1) == -1);
      assert(!rd_row(state, RD_DISPLAY_HEIGHT, 0, 1));
      assert(!rd_row_rgb2(state, -1, 0, 0));
      assert(rd_get(state, -1, 0, RD_SPECIES_A) == -1);
      assert(rd_mask(NULL, NULL) == -1 && rd_mask_level(state, -1, 0) == -1);
      assert(rd_mask_level(state, 0, RD_GRID_HEIGHT) == -1);
      assert(rd_width(NULL) == -1);
      assert(rd_width(state) == RD_GRID_WIDTH &&
             rd_height(state) == RD_GRID_HEIGHT);
      assert(memcmp(snapshot, memory, size) == 0);
      for (size_t i = size; i < size + 16; i++) {
        assert(memory[i] == 0xa5);
      }
      /* A disk seeded at the origin wraps around to the opposite corner. */
      rd_seed(state, 0, 0, 8);
      assert(rd_get(state, RD_GRID_WIDTH - 1, RD_GRID_HEIGHT - 1,
                    RD_SPECIES_B) > (fhn ? resting_value(state) : 0));
      /* Concentrations stay within the codable range. */
      for (int i = 0; i < RD_GRID_WIDTH * RD_GRID_HEIGHT; i++) {
        int a =
            rd_get(state, i % RD_GRID_WIDTH, i / RD_GRID_WIDTH, RD_SPECIES_A);
        int b =
            rd_get(state, i % RD_GRID_WIDTH, i / RD_GRID_WIDTH, RD_SPECIES_B);
        assert(a >= 0 && a <= RD_VALUE_ONE && b >= 0 && b <= RD_VALUE_ONE);
      }
      /* A uniform field stays exactly at the equilibrium A = 1, B = 0, or
       * u = v = 0 with k = rest = 0, even with every other coefficient at
       * its maximum. The residual rows still hold shares of the previous
       * field. A uniform field is exact only together with zero residuals,
       * as after rd_init. */
      unsigned uniform_a = fhn ? FHN_ZERO : Q15_FULL_A;
      unsigned uniform_b = fhn ? FHN_ZERO : 0;
      fill_codes(state, uniform_a, uniform_b);
      const int gs_max[RD_GRAY_SCOTT_PARAMS] = {
          RD_Q15_ONE, RD_Q15_ONE, RD_Q15_ONE, RD_Q15_ONE, RD_Q15_ONE};
      const int fhn_max[RD_FHN_PARAMS] = {RD_Q15_ONE, RD_Q15_ONE, RD_FHN_RU_MAX,
                                          RD_Q15_ONE, RD_Q15_ONE, 0,
                                          RD_Q15_ONE, 0,          0};
      assert(!rd_set_params(state, fhn ? fhn_max : gs_max, count));
      rd_step(state, 4);
      for (int i = 0; i < RD_GRID_WIDTH * RD_GRID_HEIGHT; i++) {
        unsigned a, b;
        load_codes(state, i, &a, &b);
        assert(a == uniform_a && b == uniform_b);
      }
      if (!fhn) {
        /* The rate of B saturates instead of wrapping: a cell with
         * B = 1 among B = 0 neighbors under feed = kill = 1 has an exact
         * rate of about -3 * 2^30, and must end at B = 0, not 1. A cell with
         * A = 0 among A = 1 neighbors reaches the largest possible A rate. */
        int edge = 5 * RD_GRID_WIDTH + 5;
        store_codes(state, edge, 0, Q15_FULL_A);
        rd_params(state, RD_Q15_ONE, RD_Q15_ONE, RD_Q15_ONE, RD_Q15_ONE / 2,
                  RD_Q15_ONE);
        memcpy(reference_memory, memory, size);
        rd_step(state, 1);
        reference(reference_state);
        assert(rd_get(state, 5, 5, RD_SPECIES_B) == 0);
        assert(rd_get(state, 5, 5, RD_SPECIES_A) == RD_VALUE_ONE);
        assert(rd_hash(state) == rd_hash(reference_state));
        fill_codes(state, Q15_FULL_A, 0);
      }
      if (fhn) {
        check_fhn_extremes();
        check_fhn_init();
      }
      /* Rounding and error diffusion mass conservation. */
      check_rounding(state);
      check_diffusion(state, RD_SPECIES_A, RD_VALUE_ONE / 3);
      check_diffusion(state, RD_SPECIES_B, RD_VALUE_ONE / 7);
      check_diffusion(state, RD_SPECIES_B, 12345);
      /* Unaligned blocks: the State is aligned inside the block and nothing
       * outside the block is written. */
      for (int alignment = 0; alignment < 4; alignment++) {
        uint8_t *unaligned = malloc(size + 8);
        memset(unaligned, 0xa5, size + 8);
        State *aligned_state =
            rd_init_model(unaligned + alignment, size, model, 42, NULL, 0);
        assert(aligned_state);
        rd_step(aligned_state, 1);
        assert(
            rd_row(aligned_state, RD_DISPLAY_HEIGHT - 1, RD_PALETTE_MONO, 1));
        assert(rd_row_rgb2(aligned_state, RD_DISPLAY_HEIGHT - 1,
                           RD_PALETTE_CYAN, RD_ROW_BILINEAR));
        for (int j = 0; j < alignment; j++) {
          assert(unaligned[j] == 0xa5);
        }
        for (size_t j = size + alignment; j < size + 8; j++) {
          assert(unaligned[j] == 0xa5);
        }
        free(unaligned);
      }
      check_mask(model);
      printf("model %d: %zu bytes, "
             "oracle/laplacian/codes/rgb2/validation/rounding/diffusion/mask "
             "OK\n",
             model, size);
      free(memory);
      free(reference_memory);
      free(snapshot);
    }
  }
  return 0;
}
