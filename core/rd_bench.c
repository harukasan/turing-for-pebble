/*
 * Phase timing of rd_step for the speed study. Included after rd.c (it
 * uses its internal functions) by a watch build with RD_BENCH and by
 * tests/bench.c on the host. Each phase runs over the whole field `count`
 * times so that a millisecond clock resolves it:
 *   laplacian:  the row loop of rd_step up to the 20-fold Laplacian sums,
 *               including the row copies,
 *   react:      the same plus react_codes for every cell,
 *   step:       rd_step itself,
 *   render:     full interpolated frames of rd_row_rgb2 (all 228 rows).
 * Differences give the reaction and the encoding and store (step - react).
 * The field advances only in the step phase.
 */
typedef struct {
  uint32_t laplacian, react, step, render;
} RdBench;

/* The row loop of rd_step without writing: the row copies, the Laplacian
 * sums, and with react set react_codes for every cell. Returns a sum so the
 * work is kept. */
static int32_t bench_rows(State *state, int react) {
  const int width = RD_GRID_WIDTH, height = RD_GRID_HEIGHT;
  size_t row_bytes = (size_t)width * sizeof(uint16_t);
  uint16_t *cells_a = plane(state, RD_SPECIES_A);
  uint16_t *cells_b = plane(state, RD_SPECIES_B);
  int32_t *lap = scratch(state), *next = lap + 2 * width;
  uint16_t *codes = (uint16_t *)(next + 2 * width);
  uint16_t *up[SPECIES_COUNT] = {codes, codes + width};
  uint16_t *cur[SPECIES_COUNT] = {codes + 2 * width, codes + 3 * width};
  uint16_t *first[SPECIES_COUNT] = {codes + 4 * width, codes + 5 * width};
  StepContext ctx;
  begin_step(state, &ctx);
  int32_t sum = 0;
  memcpy(up[0], cells_a + (size_t)(height - 1) * width, row_bytes);
  memcpy(up[1], cells_b + (size_t)(height - 1) * width, row_bytes);
  memcpy(first[0], cells_a, row_bytes);
  memcpy(first[1], cells_b, row_bytes);
  for (int y = 0; y < height; y++) {
    size_t row = (size_t)y * width;
    memcpy(cur[0], cells_a + row, row_bytes);
    memcpy(cur[1], cells_b + row, row_bytes);
    const uint16_t *down_a = y == height - 1 ? first[0] : cells_a + row + width;
    const uint16_t *down_b = y == height - 1 ? first[1] : cells_b + row + width;
    laplacian_sums(up[0], cur[0], down_a, lap, 2, width);
    laplacian_sums(up[1], cur[1], down_b, lap + 1, 2, width);
    if (react) {
      react_row(&ctx, cur[0], cur[1], lap, next, NULL);
      sum += next[0] ^ next[2 * width - 1];
    } else {
      sum += lap[0] ^ lap[2 * width - 1];
    }
    for (int species = 0; species < SPECIES_COUNT; species++) {
      uint16_t *tmp = up[species];
      up[species] = cur[species];
      cur[species] = tmp;
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
  start = now();
  for (int i = 0; i < count; i++) {
    for (int y = 0; y < RD_DISPLAY_HEIGHT; y++) {
      sink += rd_row_rgb2(state, y, RD_PALETTE_LIME, RD_ROW_BILINEAR)[y % 200];
    }
  }
  out->render = now() - start;
  (void)sink;
}
