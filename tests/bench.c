/* Host run of the phase timing in core/rd_bench.c: build/bench [count]
 * [analog], with the mask of the digits or, with `analog`, of the analog
 * face. Host times only rank the phases, and the watch numbers decide. */
#define _POSIX_C_SOURCE 199309L
#include "../core/rd.c"
#include "../core/clock_mask.c"
#include "../core/rd_bench.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t now_us(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint32_t)(t.tv_sec * 1000000 + t.tv_nsec / 1000);
}

int main(int argc, char **argv) {
  int count = argc > 1 ? atoi(argv[1]) : 200;
  void *memory = malloc(rd_bytes());
  void *state = rd_init(memory, rd_bytes(), 42);
  uint8_t *mask = malloc(cm_bytes(rd_width(state), rd_height(state)));
  if (argc > 2 && strcmp(argv[2], "analog") == 0) {
    cm_build_analog(mask, rd_width(state), rd_height(state), CM_FONT_LECO, 360,
                    600, 2046, 8, 29, 1, 1);
  } else {
    cm_build(mask, rd_width(state), rd_height(state), CM_FONT_LECO, 13, 57,
             2046, 8, 29, 1, 1);
  }
  rd_mask(state, mask);
  rd_step(state, 300);
  RdBench b;
  rd_bench(state, count, now_us, &b);
  printf("per step: laplacian %.1f us, react %.1f us, "
         "encode+store %.1f us, step %.1f us, render %.1f us per frame\n",
         (double)b.laplacian / count, (double)(b.react - b.laplacian) / count,
         (double)(b.step - b.react) / count, (double)b.step / count,
         (double)b.render / count);
  free(mask);
  free(memory);
  return 0;
}
