#include "config.h"
#include "../../../core/rd.c"
#include "../../../core/clock_mask.c"
#ifdef RD_BENCH
#include "../../../core/rd_bench.c"

/* Phase timing for the speed study, called by main.c. */
void rd_bench_log(void *state, int count, uint32_t (*now)(void),
                  uint32_t out[4]) {
  RdBench b;
  rd_bench(state, count, now, &b);
  out[0] = b.laplacian;
  out[1] = b.react;
  out[2] = b.step;
  out[3] = b.render;
}
#endif
