#ifndef RD_H
#define RD_H
#include <stddef.h>
#include <stdint.h>

#define RD_VERSION 5

/* Coefficients (rd_params, rd_set_params) are Q15 fixed point: RD_Q15_ONE
 * represents 1.0. */
#define RD_Q15_ONE 32768

/* Models, and the length of each model's parameter vector (rd_init_model,
 * rd_set_params):
 *   Gray-Scott:       feed, kill, da, db, dt, each in [0, RD_Q15_ONE];
 *   FitzHugh-Nagumo:  du, dv, ru, rv, av, k, dt, rest, init, where k and
 *                     rest are in [-RD_Q15_ONE, RD_Q15_ONE], ru in
 *                     [0, RD_FHN_RU_MAX], init is 0 (disks) or 1 (a broken
 *                     wave), and the others in [0, RD_Q15_ONE].
 * FitzHugh-Nagumo runs only in the Q15 modes 1 and 3. A build that defines
 * RD_MODEL compiles only that model. */
#define RD_MODEL_GRAY_SCOTT 0
#define RD_MODEL_FHN 1
#define RD_MODEL_COUNT 2
#define RD_PARAM_MAX 10
#define RD_GRAY_SCOTT_PARAMS 5
#define RD_FHN_PARAMS 9
#define RD_FHN_RU_MAX 8192 /* 0.25 */

/* Concentrations (rd_get) are Q24 fixed point: RD_VALUE_ONE represents 1.0.
 * A FitzHugh-Nagumo value x in [-2, 2] is stored as the fraction
 * (x + 2) / 4, which rd_get returns. */
#define RD_VALUE_BITS 24
#define RD_VALUE_ONE (1 << RD_VALUE_BITS)

/* Display coordinate system shared by every mode. */
#define RD_DISPLAY_WIDTH 200
#define RD_DISPLAY_HEIGHT 228
/* Size of the RGBA row returned by rd_row. */
#define RD_ROW_BYTES (RD_DISPLAY_WIDTH * 4)

/* Species indices for rd_get: A and B of Gray-Scott, v and u of
 * FitzHugh-Nagumo. B and u are the displayed and masked species. */
#define RD_SPECIES_A 0
#define RD_SPECIES_B 1

/* Components of the caller allocation, for rd_memory. */
enum {
  RD_COMPONENT_FIELDS,
  RD_COMPONENT_ROW_BUFFERS,
  RD_COMPONENT_CONTROL,
  RD_COMPONENT_OUTPUT_ROW,
  RD_COMPONENT_ALIGNMENT,
  RD_COMPONENT_MASK,
  RD_COMPONENT_COUNT
};

/* Palettes for rd_row. */
enum { RD_PALETTE_LIME, RD_PALETTE_CYAN, RD_PALETTE_MONO };

/* mode 0: 200x228, one 16-bit word per cell (A 7-bit linear, B 9-bit
 * square-root companded); mode 1: 100x114, one Q15 word per species;
 * mode 2: diagnostic 100x114 with the packed 16-bit cells of mode 0;
 * mode 3: 120x136, one Q15 word per species, meant to be shown with
 * RD_ROW_BILINEAR. */
size_t rd_bytes(int mode);
size_t rd_memory(int mode, int component);
/* Initialize a field of a model with `count` parameters of its vector (NULL
 * for the model's defaults) and seed it. Returns the aligned state, or NULL
 * for invalid arguments without writing. */
void *rd_init_model(void *memory, size_t bytes, int mode, int model,
                    uint32_t seed, const int *params, int count);
/* rd_init_model with Gray-Scott and its default parameters. */
void *rd_init(void *memory, size_t bytes, int mode, uint32_t seed);
/* Replace the parameters of the handle's model (count must be its vector
 * length) without touching the field. 0, or -1 without writing. */
int rd_set_params(void *handle, const int *values, int count);
/* rd_set_params of a Gray-Scott handle; -1 for another model. */
int rd_params(void *handle, int feed, int kill, int da, int db, int dt);
/* The model of the handle, or -1. */
int rd_model(void *handle);
int rd_seed(void *handle, int x, int y, int radius);
int rd_step(void *handle, int count);
int rd_get(void *handle, int x, int y, int species);
uint32_t rd_steps(void *handle);
uint32_t rd_hash(void *handle);
/* Flags of rd_row and rd_row_rgb2: RD_ROW_QUANTIZE rounds each channel to
 * the RGB2 levels (rd_row_rgb2 always does), and RD_ROW_BILINEAR
 * interpolates B at each pixel center between the four nearest cells,
 * periodic like the field, before coloring. Without it each pixel shows
 * the cell that covers it. */
#define RD_ROW_QUANTIZE 1
#define RD_ROW_BILINEAR 2
uint8_t *rd_row(void *handle, int y, int palette, int flags);
/* Display row y as RD_DISPLAY_WIDTH opaque ARGB8 bytes (2 bits per
 * channel), the quantized rd_row colors in the watch framebuffer format. */
uint8_t *rd_row_rgb2(void *handle, int y, int palette, int flags);
/* rd_row_rgb2 written into dst[first..last] (0 <= first <= last < 200)
 * instead of the shared row, for example straight into a framebuffer row.
 * Returns 0, or -1 for invalid arguments without writing. */
int rd_row_rgb2_into(void *handle, int y, int palette, int flags, uint8_t *dst,
                     int first, int last);
/* Grid size of the handle's mode. */
int rd_width(void *handle);
int rd_height(void *handle);
/* Cell mask: one bit per grid cell, rows of (width + 7) / 8 bytes, cell x
 * in bit x % 8 of byte x / 8 (the format of cm_build). rd_mask derives the
 * mask level of every cell (NULL clears the mask): 0 in a masked cell, else
 * the chessboard distance in cells to the nearest masked cell, capped at
 * RD_MASK_RAMP. The displayed species is held at its resting value in
 * masked cells from rd_mask on (Gray-Scott B at 0, FitzHugh-Nagumo u at
 * rest), and the pattern fades out around the mask: the Gray-Scott kill
 * rate rises linearly from the parameter kill at level RD_MASK_RAMP to
 * RD_MASK_KILL at level 0, and FitzHugh-Nagumo u is pulled toward rest
 * with a rate rising from 0 to RD_MASK_PULL. rd_seed skips masked cells. */
#define RD_MASK_RAMP 5
#define RD_MASK_KILL 2458 /* 0.075 in Q15 */
#define RD_MASK_PULL 4096 /* 0.125 in Q15 */
int rd_mask(void *handle, const uint8_t *mask);
int rd_mask_level(void *handle, int x, int y);
#endif
