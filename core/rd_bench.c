/*
 * Phase timing of rd_step for the speed study. Included after rd.c (it
 * uses its internal functions) by a watch build with RD_BENCH and by
 * tests/bench.c on the host. Each phase runs over the whole field `count`
 * times so that a millisecond clock resolves it:
 *   decode:     every stored row decoded to Q24 rows,
 *   laplacian:  the row loop of rd_step up to the Laplacians,
 *   react:      the same plus react_cell for every cell,
 *   step:       rd_step itself.
 * Differences give the Laplacian, the reaction, and the encoding and store
 * (step - react). The field advances only in the step phase.
 */
typedef struct {
  uint32_t decode, laplacian, react, step;
} RdBench;

/* The row loop of rd_step without writing: decode, the Laplacians, and with
 * react set react_cell for every cell. Returns a sum so the work is kept. */
static int32_t bench_rows(State *state, int react) {
  int width = state->width, height = state->height;
  size_t row_bytes = (size_t)width * sizeof(int32_t);
  int32_t *rows = saved_rows(state);
  int32_t *prev[SPECIES_COUNT], *cur[SPECIES_COUNT], *next[SPECIES_COUNT],
      *first[SPECIES_COUNT];
  for (int species = 0; species < SPECIES_COUNT; species++) {
    prev[species] = rows + (0 * SPECIES_COUNT + species) * width;
    cur[species] = rows + (1 * SPECIES_COUNT + species) * width;
    next[species] = rows + (2 * SPECIES_COUNT + species) * width;
    first[species] = rows + (3 * SPECIES_COUNT + species) * width;
  }
  StepContext ctx;
  begin_step(state, &ctx);
  int32_t sum = 0;
  decode_row(state, height - 1, prev[0], prev[1]);
  decode_row(state, 0, cur[0], cur[1]);
  memcpy(first[0], cur[0], row_bytes);
  memcpy(first[1], cur[1], row_bytes);
  for (int y = 0; y < height; y++) {
    if (y == height - 1) {
      memcpy(next[0], first[0], row_bytes);
      memcpy(next[1], first[1], row_bytes);
    } else {
      decode_row(state, y + 1, next[0], next[1]);
    }
    laplacian_row(prev[0], cur[0], next[0], width);
    laplacian_row(prev[1], cur[1], next[1], width);
    if (react) {
      for (int x = 0; x < width; x++) {
        int32_t next_a, next_b;
        react_cell(cur[0][x], cur[1][x], prev[0][x], prev[1][x], ctx.feed,
                   ctx.decay, ctx.da, ctx.db, ctx.dt, ctx.unit_dt, &next_a,
                   &next_b);
        sum += next_a ^ next_b;
      }
    } else {
      sum += prev[0][0] ^ prev[1][width - 1];
    }
    for (int species = 0; species < SPECIES_COUNT; species++) {
      int32_t *tmp = prev[species];
      prev[species] = cur[species];
      cur[species] = next[species];
      next[species] = tmp;
    }
  }
  return sum;
}

static void rd_bench(void *handle, int count, uint32_t (*now)(void),
                     RdBench *out) {
  State *state = checked_state(handle);
  volatile int32_t sink = 0;
  uint32_t start = now();
  for (int i = 0; i < count; i++) {
    int32_t *rows = saved_rows(state);
    for (int y = 0; y < state->height; y++) {
      decode_row(state, y, rows, rows + state->width);
    }
    sink += rows[0];
  }
  out->decode = now() - start;
  start = now();
  for (int i = 0; i < count; i++) {
    sink += bench_rows(state, 0);
  }
  out->laplacian = now() - start;
  start = now();
  for (int i = 0; i < count; i++) {
    sink += bench_rows(state, 1);
  }
  out->react = now() - start;
  start = now();
  rd_step(state, count);
  out->step = now() - start;
  (void)sink;
}
