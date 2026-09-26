/* Run one mode / parameter vector / seed case and print a JSON summary:
 * host timing, B statistics, and how much one further step changes cells
 * and display pixels. The rendered display is also written as a PPM.
 * Driven by scripts/compare.py, which takes the vectors of the presets from
 * scripts/list-presets.mjs.
 *
 *   build/compare mode model p0 ... p9 seed steps out.ppm
 */
#include "../core/rd.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Length of the parameter vector (RD_PARAM_MAX of numerical definition
 * version 5). Only Gray-Scott, whose rd_params takes the first five, is
 * supported so far. */
#ifndef RD_PARAM_MAX
#define RD_PARAM_MAX 10
#endif

int main(int argc, char **argv) {
  if (argc != 6 + RD_PARAM_MAX) {
    return 1;
  }
  int mode = atoi(argv[1]), model = atoi(argv[2]);
  int params[RD_PARAM_MAX];
  for (int i = 0; i < RD_PARAM_MAX; i++) {
    params[i] = atoi(argv[3 + i]);
  }
  uint32_t seed = (uint32_t)strtoul(argv[3 + RD_PARAM_MAX], NULL, 10);
  int count = atoi(argv[4 + RD_PARAM_MAX]);
  const char *path = argv[5 + RD_PARAM_MAX];
  if (mode < 0 || mode > 3 || model != 0 || count < 1) {
    return 1;
  }
  void *memory = malloc(rd_bytes(mode));
  void *state = rd_init(memory, rd_bytes(mode), mode, seed);
  if (!state ||
      rd_params(state, params[0], params[1], params[2], params[3], params[4])) {
    return 1;
  }
  clock_t start = clock();
  rd_step(state, count);
  double ms = 1000. * (clock() - start) / CLOCKS_PER_SEC / count;
  int width = rd_width(state), height = rd_height(state);
  /* Mode 3 is shown interpolated, like the watch. */
  int flags = RD_ROW_QUANTIZE | (mode == 3 ? RD_ROW_BILINEAR : 0);
  double sum = 0, sum_squares = 0;
  int changed = 0, changed_pixels = 0;
  int *previous = malloc(width * height * sizeof(int));
  unsigned char *colors = malloc(RD_DISPLAY_WIDTH * RD_DISPLAY_HEIGHT * 4);
  FILE *ppm = fopen(path, "wb");
  if (!ppm) {
    return 1;
  }
  fprintf(ppm, "P6\n%d %d\n255\n", RD_DISPLAY_WIDTH, RD_DISPLAY_HEIGHT);
  for (int y = 0; y < RD_DISPLAY_HEIGHT; y++) {
    unsigned char *row = rd_row(state, y, RD_PALETTE_LIME, flags);
    for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
      fwrite(row + x * 4, 1, 3, ppm);
      for (int channel = 0; channel < 4; channel++) {
        colors[(y * RD_DISPLAY_WIDTH + x) * 4 + channel] = row[x * 4 + channel];
      }
    }
  }
  fclose(ppm);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int value = rd_get(state, x, y, RD_SPECIES_B);
      previous[y * width + x] = value;
      sum += value / (double)RD_VALUE_ONE;
      sum_squares +=
          (value / (double)RD_VALUE_ONE) * (value / (double)RD_VALUE_ONE);
    }
  }
  uint32_t hash = rd_hash(state);
  rd_step(state, 1);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      changed += previous[y * width + x] != rd_get(state, x, y, RD_SPECIES_B);
    }
  }
  for (int y = 0; y < RD_DISPLAY_HEIGHT; y++) {
    unsigned char *row = rd_row(state, y, RD_PALETTE_LIME, flags);
    for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
      unsigned char *before = colors + (y * RD_DISPLAY_WIDTH + x) * 4;
      changed_pixels += row[x * 4] != before[0] ||
                        row[x * 4 + 1] != before[1] ||
                        row[x * 4 + 2] != before[2];
    }
  }
  printf("{\"mode\":%d,\"model\":%d,\"seed\":%u,\"steps\":%d,\"hash\":%u,"
         "\"hostMsPerStep\":%.6f,\"bMean\":%.6f,\"bVariance\":%.6f,"
         "\"changedCellsFraction\":%.6f,\"changedDisplayPixelsFraction\":%.6f,"
         "\"coreBytes\":%zu}",
         mode, model, seed, count, hash, ms, sum / (width * height),
         sum_squares / (width * height) -
             sum * sum / ((double)width * height * width * height),
         (double)changed / (width * height),
         (double)changed_pixels / (RD_DISPLAY_WIDTH * RD_DISPLAY_HEIGHT),
         rd_bytes(mode));
  free(colors);
  free(previous);
  free(memory);
  return 0;
}
