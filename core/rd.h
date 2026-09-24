#ifndef RD_H
#define RD_H
#include <stddef.h>
#include <stdint.h>
#define RD_VERSION 1
/* mode 0: 200x228/u8, mode 1: 100x114/u16, mode 2: diagnostic 100x114/u8 */
size_t rd_bytes(int mode);
size_t rd_memory(int mode, int component);
void *rd_init(void *memory, size_t bytes, int mode, uint32_t seed);
int rd_params(void *state, int f, int k, int da, int db, int dt);
int rd_seed(void *state, int x, int y, int radius);
int rd_step(void *state, int count);
int rd_get(void *state, int x, int y, int species);
uint32_t rd_steps(void *state);
uint32_t rd_hash(void *state);
uint8_t *rd_row(void *state, int y, int palette, int quantize);
#endif
