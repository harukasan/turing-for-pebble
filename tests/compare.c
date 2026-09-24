#include "../core/rd.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
int main(int argc, char **argv) {
  if (argc != 6)
    return 1;
  int m = atoi(argv[1]), preset = atoi(argv[2]), count = atoi(argv[4]);
  uint32_t seed = (uint32_t)strtoul(argv[3], NULL, 10);
  const int feeds[] = {950, 1786, 1203, 1147},
            kills[] = {1868, 2032, 2127, 2130};
  if (m < 0 || m > 2 || preset < 0 || preset > 3 || count < 1)
    return 1;
  void *memory = malloc(rd_bytes(m));
  void *s = rd_init(memory, rd_bytes(m), m, seed);
  if (!s)
    return 1;
  rd_params(s, feeds[preset], kills[preset], 32768, 16384, 32768);
  clock_t start = clock();
  rd_step(s, count);
  double ms = 1000. * (clock() - start) / CLOCKS_PER_SEC / count;
  int w = m == 0 ? 200 : 100, h = w * 228 / 200;
  double sum = 0, sq = 0;
  int changed = 0, changed_pixels = 0;
  int *old = malloc(w * h * sizeof(int));
  unsigned char *colors = malloc(200 * 228 * 4);
  FILE *ppm = fopen(argv[5], "wb");
  if (!ppm)
    return 1;
  fprintf(ppm, "P6\n200 228\n255\n");
  for (int y = 0; y < 228; y++) {
    unsigned char *row = rd_row(s, y, 0, 1);
    for (int x = 0; x < 200; x++) {
      fwrite(row + x * 4, 1, 3, ppm);
      for (int c = 0; c < 4; c++)
        colors[(y * 200 + x) * 4 + c] = row[x * 4 + c];
    }
  }
  fclose(ppm);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      int v = rd_get(s, x, y, 1);
      old[y * w + x] = v;
      sum += v / 32768.;
      sq += (v / 32768.) * (v / 32768.);
    }
  uint32_t hash = rd_hash(s);
  rd_step(s, 1);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      changed += old[y * w + x] != rd_get(s, x, y, 1);
  for (int y = 0; y < 228; y++) {
    unsigned char *row = rd_row(s, y, 0, 1);
    for (int x = 0; x < 200; x++)
      changed_pixels += row[x * 4] != colors[(y * 200 + x) * 4] ||
                        row[x * 4 + 1] != colors[(y * 200 + x) * 4 + 1] ||
                        row[x * 4 + 2] != colors[(y * 200 + x) * 4 + 2];
  }
  printf("{\"mode\":%d,\"preset\":%d,\"seed\":%u,\"steps\":%d,\"hash\":%u,"
         "\"hostMsPerStep\":%.6f,\"bMean\":%.6f,\"bVariance\":%.6f,"
         "\"changedCellsFraction\":%.6f,\"changedDisplayPixelsFraction\":%.6f,"
         "\"coreBytes\":%zu}",
         m, preset, seed, count, hash, ms, sum / (w * h),
         sq / (w * h) - sum * sum / ((double)w * h * w * h),
         (double)changed / (w * h), (double)changed_pixels / (200 * 228),
         rd_bytes(m));
  free(colors);
  free(old);
  free(memory);
  return 0;
}
