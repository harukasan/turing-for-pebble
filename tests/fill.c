/* Growth of the pattern on one grid for the resolution study: run a mode
 * and model with the LECO clock mask installed and print one JSON line per
 * checkpoint, and write the interpolated display as a PPM there. Driven
 * by scripts/fill.py.
 *
 *   build/fill mode model p0 ... p9 seed disks min_radius radius_range
 *              out_prefix checkpoint...
 *
 * model and p0 to p9 are the model and the Q15 parameter vector of a preset
 * (scripts/list-presets.mjs).
 * The resting field is reseeded with `disks` disks placed by the LCG
 * sequence of rd_init, radius min_radius + random_below(radius_range);
 * 24 4 6 is the seeding of rd_init. A FitzHugh-Nagumo broken wave (init 1)
 * is kept as rd_init_model seeds it.
 *
 * Metrics cover only the cells away from the digits (mask level
 * RD_MASK_RAMP): the mean of the displayed species (B, or the stored
 * fraction of FitzHugh-Nagumo u), the fraction of blocks of about a twelfth
 * of the width that contain a visible cell, and the mean width in display
 * pixels of the lit runs along every fourth display row of the
 * interpolated render.
 */
#include "../core/rd.c"
#include "../core/clock_mask.c"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* A cell is visible when its display intensity is above 0.15, B above
 * 0.05 with the lime palette at gain 3. */
#define VISIBLE_INTENSITY (RD_VALUE_ONE / 20 * 3)
/* Green above this is a lit pixel of the lime palette (B above 0.14). */
#define LIT_GREEN 127

int main(int argc, char **argv) {
  /* The arguments after the parameter vector. */
  const int rest = 3 + RD_PARAM_MAX;
  if (argc < rest + 6) {
    return 1;
  }
  int mode = atoi(argv[1]), model = atoi(argv[2]);
  int params[RD_PARAM_MAX];
  for (int i = 0; i < RD_PARAM_MAX; i++) {
    params[i] = atoi(argv[3 + i]);
  }
  uint32_t seed = (uint32_t)strtoul(argv[rest], NULL, 10);
  int disks = atoi(argv[rest + 1]), min_radius = atoi(argv[rest + 2]),
      radius_range = atoi(argv[rest + 3]);
  const char *prefix = argv[rest + 4];
  if (!mode_supported(mode) || !model_supported(model) || disks < 0 ||
      min_radius < 1 || radius_range < 1) {
    return 1;
  }
  void *memory = malloc(rd_bytes(mode));
  State *state = rd_init_model(memory, rd_bytes(mode), mode, model, seed,
                               params, PARAM_COUNT[model]);
  if (!state) {
    return 1;
  }
  int width = state->width, height = state->height;
  if (model != RD_MODEL_FHN || state->params[FHN_INIT] == 0) {
    fill_rest(state);
    uint32_t lcg = seed;
    for (int i = 0; i < disks; i++) {
      int x = random_below(lcg_next(&lcg), RD_DISPLAY_WIDTH);
      int y = random_below(lcg_next(&lcg), RD_DISPLAY_HEIGHT);
      rd_seed(state, x, y,
              min_radius + random_below(lcg_next(&lcg), radius_range));
    }
  }
  uint8_t *mask = malloc(cm_bytes(width, height));
  cm_build(mask, width, height, CM_FONT_LECO, 13, 57, 2046, 8, 29, 1);
  rd_mask(state, mask);
  int block = width / 12 > 4 ? width / 12 : 4;
  int blocks_x = (width + block - 1) / block,
      blocks_y = (height + block - 1) / block;
  int *seen = malloc(sizeof(int) * blocks_x * blocks_y);
  double total_ms = 0;
  for (int arg = rest + 5; arg < argc; arg++) {
    int checkpoint = atoi(argv[arg]);
    int count = checkpoint - (int)rd_steps(state);
    clock_t start = clock();
    rd_step(state, count);
    total_ms += 1000. * (clock() - start) / CLOCKS_PER_SEC;
    double sum = 0;
    int cells = 0;
    for (int i = 0; i < blocks_x * blocks_y; i++) {
      seen[i] = 0; /* 0 masked only, 1 open, 2 open with visible B */
    }
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        if (rd_mask_level(state, x, y) < RD_MASK_RAMP) {
          continue;
        }
        int b = rd_get(state, x, y, RD_SPECIES_B);
        sum += b / (double)RD_VALUE_ONE;
        cells++;
        int *entry = &seen[(y / block) * blocks_x + x / block];
        if ((int64_t)(b - DISPLAY_OFFSET[model]) * DISPLAY_GAIN[model] >
            VISIBLE_INTENSITY) {
          *entry = 2;
        } else if (*entry == 0) {
          *entry = 1;
        }
      }
    }
    int open = 0, lit = 0;
    for (int i = 0; i < blocks_x * blocks_y; i++) {
      open += seen[i] > 0;
      lit += seen[i] == 2;
    }
    char path[512];
    snprintf(path, sizeof path, "%s-%d.ppm", prefix, checkpoint);
    FILE *ppm = fopen(path, "wb");
    if (!ppm) {
      return 1;
    }
    fprintf(ppm, "P6\n%d %d\n255\n", RD_DISPLAY_WIDTH, RD_DISPLAY_HEIGHT);
    long run_pixels = 0, runs = 0;
    for (int y = 0; y < RD_DISPLAY_HEIGHT; y++) {
      const uint8_t *row =
          rd_row(state, y, RD_PALETTE_LIME, RD_ROW_QUANTIZE | RD_ROW_BILINEAR);
      for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
        fwrite(row + x * 4, 1, 3, ppm);
      }
      if (y % 4) {
        continue;
      }
      row = rd_row(state, y, RD_PALETTE_LIME, RD_ROW_BILINEAR);
      int grid_y = y * height / RD_DISPLAY_HEIGHT, run = 0, clean = 0;
      for (int x = 0; x < RD_DISPLAY_WIDTH; x++) {
        int open_pixel = rd_mask_level(state, x * width / RD_DISPLAY_WIDTH,
                                       grid_y) == RD_MASK_RAMP;
        if (open_pixel && row[x * 4 + 1] > LIT_GREEN) {
          if (run == 0) {
            clean = x > 0;
          }
          run++;
          continue;
        }
        /* A run counts when it starts after and ends before an open
         * unlit pixel, so it lies wholly inside the row. */
        if (run && clean && open_pixel) {
          run_pixels += run;
          runs++;
        }
        run = 0;
      }
    }
    fclose(ppm);
    printf("{\"mode\": %d, \"width\": %d, \"height\": %d, \"model\": %d, "
           "\"seed\": %u, \"disks\": %d, \"minRadius\": %d, "
           "\"radiusRange\": %d, \"step\": %d, \"hostMsPerStep\": %.3f, "
           "\"bMean\": %.5f, \"blocks\": %.4f, \"stripeWidthPx\": %.3f}\n",
           mode, width, height, model, seed, disks, min_radius, radius_range,
           checkpoint, total_ms / checkpoint, cells ? sum / cells : 0,
           open ? (double)lit / open : 0, runs ? (double)run_pixels / runs : 0);
    fflush(stdout);
  }
  free(seen);
  free(mask);
  free(memory);
  return 0;
}
