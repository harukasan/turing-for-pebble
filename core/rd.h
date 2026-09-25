#ifndef RD_H
#define RD_H
#include <stddef.h>
#include <stdint.h>

#define RD_VERSION 3

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
  RD_COMPONENT_COUNT
};

/* Palettes for rd_row. */
enum { RD_PALETTE_LIME, RD_PALETTE_CYAN, RD_PALETTE_MONO };

/* mode 0: 200x228, one 16-bit word per cell (A 7-bit linear, B 9-bit
 * square-root companded); mode 1: 100x114, one Q15 word per species;
 * mode 2: diagnostic 100x114 with the packed 16-bit cells of mode 0. */
size_t rd_bytes(int mode);
size_t rd_memory(int mode, int component);
void *rd_init(void *memory, size_t bytes, int mode, uint32_t seed);
int rd_params(void *handle, int feed, int kill, int da, int db, int dt);
int rd_seed(void *handle, int x, int y, int radius);
int rd_step(void *handle, int count);
int rd_get(void *handle, int x, int y, int species);
uint32_t rd_steps(void *handle);
uint32_t rd_hash(void *handle);
uint8_t *rd_row(void *handle, int y, int palette, int quantize);
/* Display row y as RD_DISPLAY_WIDTH opaque ARGB8 bytes (2 bits per
 * channel), the quantized rd_row colors in the watch framebuffer format. */
uint8_t *rd_row_rgb2(void *handle, int y, int palette);
#endif
