#include "../core/rd.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Full-screen oracle: one step computed from a complete copy of both planes
 * with independent neighbor indexing, so the row-buffer scheme in rd_step is
 * checked against a straightforward implementation. The Laplacian rounding is
 * written out on purpose instead of calling laplacian(). */
static void reference(State *state) {
  int cells = state->width * state->height;
  int bytes_per_value = state->bits / 8;
  uint8_t *next_fields = malloc(cells * bytes_per_value * SPECIES_COUNT);
  assert(next_fields);
  for (int y = 0; y < state->height; y++) {
    for (int x = 0; x < state->width; x++) {
      int laps[SPECIES_COUNT];
      for (int species = 0; species < SPECIES_COUNT; species++) {
        int sum = 0;
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
      int a, b, cell_index = y * state->width + x;
      react_cell(state, cell_index, rd_get(state, x, y, RD_SPECIES_A),
                 rd_get(state, x, y, RD_SPECIES_B), laps[0], laps[1], &a, &b);
      store(state, next_fields, cell_index, a);
      store(state, next_fields + cells * bytes_per_value, cell_index, b);
    }
  }
  memcpy(species_field(state, RD_SPECIES_A), next_fields,
         cells * bytes_per_value * SPECIES_COUNT);
  free(next_fields);
  state->step++;
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
  assert(round_q15(-16384) == -1 && round_q15(16384) == 1);
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
    assert(memcmp(snapshot, memory, size) == 0);
    for (size_t i = size; i < size + 16; i++) {
      assert(memory[i] == 0xa5);
    }
    /* A disk seeded at the origin wraps around to the opposite corner. */
    rd_seed(state, 0, 0, 8);
    assert(rd_get(state, state->width - 1, state->height - 1, RD_SPECIES_B) >
           0);
    /* A uniform field stays exactly at the equilibrium A = 1, B = 0, even
     * with every coefficient at its maximum. */
    memset(species_field(state, RD_SPECIES_B), 0,
           state->width * state->height * (state->bits / 8));
    for (int i = 0; i < state->width * state->height; i++) {
      store(state, species_field(state, RD_SPECIES_A), i,
            state->bits == 8 ? FULL_BYTE : RD_Q15_ONE);
    }
    rd_params(state, RD_Q15_ONE, RD_Q15_ONE, RD_Q15_ONE, RD_Q15_ONE,
              RD_Q15_ONE);
    rd_step(state, 4);
    for (int y = 0; y < state->height; y++) {
      for (int x = 0; x < state->width; x++) {
        assert(rd_get(state, x, y, RD_SPECIES_A) == RD_Q15_ONE);
        assert(rd_get(state, x, y, RD_SPECIES_B) == 0);
      }
    }
    /* Stochastic rounding is unbiased on average. */
    if (state->bits == 8) {
      double sum = 0;
      for (int i = 0; i < 100000; i++) {
        state->seed = (uint32_t)i;
        sum += q15_to_stored(state, 12345, 21, RD_SPECIES_A);
      }
      assert(sum / 100000 > 12345. * FULL_BYTE / RD_Q15_ONE - .01 &&
             sum / 100000 < 12345. * FULL_BYTE / RD_Q15_ONE + .01);
    }
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
           "oracle/boundary/equilibrium/validation/rounding OK\n",
           mode, size);
    free(memory);
    free(reference_memory);
    free(snapshot);
  }
  return 0;
}
