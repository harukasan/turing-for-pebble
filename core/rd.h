#ifndef RD_H
#define RD_H
#include <stddef.h>
#include <stdint.h>

#define RD_VERSION 1

/* Q15 fixed point: RD_Q15_ONE represents 1.0. */
#define RD_Q15_ONE 32768

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

/* mode 0: 200x228/u8, mode 1: 100x114/u16, mode 2: diagnostic 100x114/u8 */
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
#endif
