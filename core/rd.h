#ifndef RD_H
#define RD_H
#include <stddef.h>
#include <stdint.h>

#define RD_VERSION 4

/* Coefficients (rd_params) are Q15 fixed point: RD_Q15_ONE represents 1.0. */
#define RD_Q15_ONE 32768

/* Concentrations (rd_get) are Q24 fixed point: RD_VALUE_ONE represents 1.0. */
#define RD_VALUE_BITS 24
#define RD_VALUE_ONE (1 << RD_VALUE_BITS)

/* Display coordinate system shared by every mode. */
#define RD_DISPLAY_WIDTH 200
#define RD_DISPLAY_HEIGHT 228
/* Size of the RGBA row returned by rd_row. */
#define RD_ROW_BYTES (RD_DISPLAY_WIDTH * 4)

/* Species indices for rd_get. */
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

/* Palettes for rd_row: the built-in palettes of lib/palettes.ts, then the
 * custom palette whose stops rd_palette sets. A palette maps the display
 * intensity, B times 3 clamped to 1.0, through equally spaced RGB stops,
 * except the monochrome threshold. */
enum {
  RD_PALETTE_LIME,
  RD_PALETTE_CYAN,
  RD_PALETTE_MONO,
  RD_PALETTE_VIRIDIS,
  RD_PALETTE_MAGMA,
  RD_PALETTE_PLASMA,
  RD_PALETTE_INFERNO,
  RD_PALETTE_CIVIDIS,
  RD_PALETTE_TURBO,
  RD_PALETTE_CUSTOM,
  RD_PALETTE_COUNT
};

/* Most stops of a palette. */
#define RD_PALETTE_MAX_STOPS 8

/* mode 0: 200x228, one 16-bit word per cell (A 7-bit linear, B 9-bit
 * square-root companded); mode 1: 100x114, one Q15 word per species;
 * mode 2: diagnostic 100x114 with the packed 16-bit cells of mode 0;
 * mode 3: 120x136, one Q15 word per species, meant to be shown with
 * RD_ROW_BILINEAR. */
size_t rd_bytes(int mode);
size_t rd_memory(int mode, int component);
void *rd_init(void *memory, size_t bytes, int mode, uint32_t seed);
int rd_params(void *handle, int feed, int kill, int da, int db, int dt);
/* Stops of RD_PALETTE_CUSTOM: count RGB triples, 2 to RD_PALETTE_MAX_STOPS,
 * equally spaced from intensity 0 to 1.0. rd_init sets the stops of
 * RD_PALETTE_LIME. Returns 0, or -1 without change for invalid arguments. */
int rd_palette(void *handle, const uint8_t *rgb, int count);
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
 * RD_MASK_RAMP. B is held at 0 in masked cells from rd_mask on, and the
 * kill rate rises linearly from the rd_params kill at level RD_MASK_RAMP to
 * RD_MASK_KILL at level 0, so the pattern fades out around the mask.
 * rd_seed skips masked cells. */
#define RD_MASK_RAMP 5
#define RD_MASK_KILL 2458 /* 0.075 in Q15 */
int rd_mask(void *handle, const uint8_t *mask);
int rd_mask_level(void *handle, int x, int y);
#endif
