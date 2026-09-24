#include "../core/rd.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Full-screen oracle: one step computed from a complete copy of both planes
 * with independent neighbor indexing, so the row-buffer scheme in rd_step is
 * checked against a straightforward implementation. The Laplacian rounding is
 * written out on purpose instead of calling laplacian(). Cells are encoded
 * in row order through the shared error diffusion functions. */
static void reference(State *state) {
  int cells = state->width * state->height;
  size_t plane_bytes = (size_t)cells * sizeof(uint16_t);
  uint16_t *next_planes = malloc(plane_bytes * planes(state));
  assert(next_planes);
  uint16_t *saved = plane(state, 0);
  begin_step(state);
  for (int y = 0; y < state->height; y++) {
    for (int x = 0; x < state->width; x++) {
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
      react_cell(state, rd_get(state, x, y, RD_SPECIES_A),
                 rd_get(state, x, y, RD_SPECIES_B), laps[0], laps[1], &next_a,
                 &next_b);
      int cell_index = y * state->width + x;
      uint32_t dither = cell_dither(state, cell_index);
      unsigned code_a =
          encode_cell(state, RD_SPECIES_A, x, next_a, dither & DITHER_MASK);
      unsigned code_b = encode_cell(state, RD_SPECIES_B, x, next_b,
                                    (dither >> DITHER_B_SHIFT) & DITHER_MASK);
      if (is_packed(state)) {
        next_planes[cell_index] = (uint16_t)((code_a << B_BITS) | code_b);
      } else {
        next_planes[cell_index] = (uint16_t)code_a;
        next_planes[cells + cell_index] = (uint16_t)code_b;
      }
    }
    finish_row(state);
  }
  memcpy(saved, next_planes, plane_bytes * planes(state));
  free(next_planes);
  state->step++;
}

/* Rounding helpers and codes. */
static void check_codes(void) {
  assert(round_shift(1 << 23, 24) == 1 && round_shift(-(1 << 23), 24) == -1);
  assert(round_shift((1 << 23) - 1, 24) == 0 &&
         round_shift(-(1 << 23) + 1, 24) == 0);
  assert(round_shift((int64_t)3 << 40, 30) == 3 << 10);
  /* isqrt is exact for the whole 18-bit domain used by floor_b. */
  for (uint32_t value = 0; value <= B_CODE_MAX * B_CODE_MAX; value++) {
    uint32_t root = isqrt(value);
    assert(root * root <= value && (root + 1) * (root + 1) > value);
  }
  /* Decoding is strictly increasing and floor_* returns the largest code
   * decoding within the value, for every code and just below the next. */
  for (unsigned code = 0; code <= A_CODE_MAX; code++) {
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

/* Encode one value with the residual rows cleared, returning the code. */
static unsigned decide(State *state, int species, int32_t value,
                       uint32_t random) {
  memset(residual_row(state, species), 0,
         (size_t)state->width * sizeof(int32_t));
  begin_step(state);
  unsigned code = encode_cell(state, species, 0, value, random);
  memset(residual_row(state, species), 0,
         (size_t)state->width * sizeof(int32_t));
  begin_step(state);
  return code;
}

/* The dithered threshold lies in [1/4, 3/4) of the code step. */
static void check_dither(State *state) {
  for (int species = 0; species < SPECIES_COUNT; species++) {
    unsigned code = species == RD_SPECIES_A ? 40 : 130;
    int32_t low = decode(state, species, code),
            step = decode(state, species, code + 1) - low;
    /* random 2^14 is the midpoint threshold: halfway rounds up, one less
     * rounds down. */
    assert(decide(state, species, low + step / 2, DITHER_BASE) == code + 1);
    assert(decide(state, species, low + step / 2 - 1, DITHER_BASE) == code);
    /* random 0 is a quarter: 30% rounds up. random 2^15 - 1 is just below
     * three quarters: 70% rounds down, 76% rounds up. */
    assert(decide(state, species, low + step * 3 / 10, 0) == code + 1);
    assert(decide(state, species, low + step * 7 / 10, DITHER_MASK) == code);
    assert(decide(state, species, low + step * 76 / 100, DITHER_MASK) ==
           code + 1);
    /* Exact codes never move, whatever the dither. */
    assert(decide(state, species, low, 0) == code);
    assert(decide(state, species, low, DITHER_MASK) == code);
  }
}

/* Error diffusion conserves the encoded mass of a row exactly: the decoded
 * codes plus the shares still held equal the sum of the inputs. */
static void check_diffusion(State *state, int species, int32_t value) {
  int32_t *residual = residual_row(state, species);
  memset(residual, 0, (size_t)state->width * sizeof(int32_t));
  begin_step(state);
  int64_t decoded = 0, held = 0;
  for (int x = 0; x < state->width; x++) {
    uint32_t random = mix32((uint32_t)x) & DITHER_MASK;
    decoded +=
        decode(state, species, encode_cell(state, species, x, value, random));
  }
  finish_row(state);
  for (int x = 0; x < state->width; x++) {
    held += residual[x];
  }
  held += state->carry[species];
  assert(decoded + held == (int64_t)value * state->width);
  /* The decoded row average is within one code step of the input. */
  unsigned code = floor_code(state, species, value);
  int32_t step =
      decode(state, species, code + 1) - decode(state, species, code);
  assert(llabs(decoded - (int64_t)value * state->width) <=
         (int64_t)step * state->width);
  memset(residual, 0, (size_t)state->width * sizeof(int32_t));
  begin_step(state);
}

int main(int argc, char **argv) {
  if (argc > 1) {
    /* Hash mode: print the field hash after `count` steps of `mode`. */
    int mode = atoi(argv[1]), count = argc > 2 ? atoi(argv[2]) : 100;
    void *memory = malloc(rd_bytes(mode));
    void *state = rd_init(memory, rd_bytes(mode), mode, 42);
    rd_step(state, count);
    printf("%u\n", rd_hash(state));
    free(memory);
    return 0;
  }
  assert(rd_bytes(-1) == 0);
  assert(rd_init(NULL, 0, 0, 0) == NULL);
  check_codes();
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
    /* Coefficient endpoints are accepted and still match the oracle. */
    for (int endpoint = 0; endpoint < 2; endpoint++) {
      int endpoint_value = endpoint ? RD_Q15_ONE : 0;
      assert(!rd_params(state, endpoint_value, endpoint_value, endpoint_value,
                        endpoint_value, endpoint_value));
      assert(!rd_params(reference_state, endpoint_value, endpoint_value,
                        endpoint_value, endpoint_value, endpoint_value));
      rd_step(state, 3);
      for (int j = 0; j < 3; j++) {
        reference(reference_state);
      }
      assert(rd_hash(state) == rd_hash(reference_state));
    }
    /* Invalid arguments leave the block and its surroundings untouched. */
    memcpy(snapshot, memory, size);
    assert(rd_params(state, -1, 0, 0, 0, 0) == -1);
    assert(rd_seed(state, RD_DISPLAY_WIDTH, 0, 4) == -1);
    assert(rd_step(state, -1) == -1);
    assert(!rd_row(state, RD_DISPLAY_HEIGHT, 0, 1));
    assert(rd_get(state, -1, 0, RD_SPECIES_A) == -1);
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
      for (int j = 0; j < alignment; j++) {
        assert(unaligned[j] == 0xa5);
      }
      for (size_t j = size + alignment; j < size + 8; j++) {
        assert(unaligned[j] == 0xa5);
      }
      free(unaligned);
    }
    printf("mode %d: %zu bytes, "
           "oracle/boundary/equilibrium/validation/dither/diffusion OK\n",
           mode, size);
    free(memory);
    free(reference_memory);
    free(snapshot);
  }
  return 0;
}
