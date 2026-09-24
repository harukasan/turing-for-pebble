/* Run one mode / preset / seed case and print a JSON summary: host timing,
 * B statistics, and how much one further step changes cells and display
 * pixels. The rendered display is also written as a PPM. Driven by
 * scripts/compare.py. */
#include "../core/rd.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define PRESET_COUNT 4

int main(int argc, char **argv) {
  if (argc != 6) {
    return 1;
  }
  int mode = atoi(argv[1]), preset = atoi(argv[2]), count = atoi(argv[4]);
  uint32_t seed = (uint32_t)strtoul(argv[3], NULL, 10);
  const int feeds[PRESET_COUNT] = {950, 1786, 1203, 1147};
  const int kills[PRESET_COUNT] = {1868, 2032, 2127, 2130};
  if (mode < 0 || mode > 2 || preset < 0 || preset >= PRESET_COUNT ||
      count < 1) {
    return 1;
  }
  void *memory = malloc(rd_bytes(mode));
  void *state = rd_init(memory, rd_bytes(mode), mode, seed);
  if (!state) {
    return 1;
  }
  rd_params(state, feeds[preset], kills[preset], RD_Q15_ONE, RD_Q15_ONE / 2,
            RD_Q15_ONE);
  clock_t start = clock();
  rd_step(state, count);
  double ms = 1000. * (clock() - start) / CLOCKS_PER_SEC / count;
  int width = mode == 0 ? RD_DISPLAY_WIDTH : RD_DISPLAY_WIDTH / 2;
  int height = width * RD_DISPLAY_HEIGHT / RD_DISPLAY_WIDTH;
  double sum = 0, sum_squares = 0;
  int changed = 0, changed_pixels = 0;
  int *previous = malloc(width * height * sizeof(int));
  unsigned char *colors = malloc(RD_DISPLAY_WIDTH * RD_DISPLAY_HEIGHT * 4);
  FILE *ppm = fopen(argv[5], "wb");
  if (!ppm) {
    return 1;
  }
  fprintf(ppm, "P6\n%d %d\n255\n", RD_DISPLAY_WIDTH, RD_DISPLAY_HEIGHT);
  for (int y = 0; y < RD_DISPLAY_HEIGHT; y++) {
    unsigned char *row = rd_row(state, y, RD_PALETTE_LIME, 1);
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
      sum += value / (double)RD_Q15_ONE;
      sum_squares +=
          (value / (double)RD_Q15_ONE) * (value / (double)RD_Q15_ONE);
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
    unsigned char *row = rd_row(state, y, RD_PALETTE_LIME, 1);
    for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
      unsigned char *before = colors + (y * RD_DISPLAY_WIDTH + x) * 4;
      changed_pixels += row[x * 4] != before[0] ||
                        row[x * 4 + 1] != before[1] ||
                        row[x * 4 + 2] != before[2];
    }
  }
  printf("{\"mode\":%d,\"preset\":%d,\"seed\":%u,\"steps\":%d,\"hash\":%u,"
         "\"hostMsPerStep\":%.6f,\"bMean\":%.6f,\"bVariance\":%.6f,"
         "\"changedCellsFraction\":%.6f,\"changedDisplayPixelsFraction\":%.6f,"
         "\"coreBytes\":%zu}",
         mode, preset, seed, count, hash, ms, sum / (width * height),
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
