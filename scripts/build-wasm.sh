#!/bin/sh
set -eu
mkdir -p public/wasm
emcc core/rd.c core/clock_mask.c -O3 -std=c11 -Wall -Wextra --no-entry -s STANDALONE_WASM=1 -s ALLOW_MEMORY_GROWTH=0 -s INITIAL_MEMORY=1048576 -s STACK_SIZE=65536 -s EXPORTED_FUNCTIONS='["_malloc","_free","_rd_bytes","_rd_memory","_rd_init","_rd_params","_rd_palette","_rd_seed","_rd_step","_rd_get","_rd_steps","_rd_hash","_rd_row","_rd_row_rgb2_into","_rd_width","_rd_height","_rd_mask","_rd_mask_level","_cm_bytes","_cm_build","_cm_font_available"]' -o public/wasm/rd.wasm
