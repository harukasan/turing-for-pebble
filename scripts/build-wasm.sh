#!/bin/sh
set -eu
mkdir -p public/wasm
emcc core/rd.c -O3 -std=c11 --no-entry -s STANDALONE_WASM=1 -s ALLOW_MEMORY_GROWTH=0 -s INITIAL_MEMORY=1048576 -s STACK_SIZE=65536 -s EXPORTED_FUNCTIONS='["_malloc","_free","_rd_bytes","_rd_memory","_rd_init","_rd_params","_rd_seed","_rd_step","_rd_get","_rd_steps","_rd_hash","_rd_row"]' -o public/wasm/rd.wasm
