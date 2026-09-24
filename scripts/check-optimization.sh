#!/bin/sh
set -eu
mkdir -p build
cc -O0 -std=c11 tests/core.c -o build/core-o0
cc -O3 -std=c11 -Werror tests/core.c -o build/core-o3
for rd_mode in 0 1 2; do
  for rd_steps in 1 100 1000; do
    rd_o0=$(build/core-o0 "$rd_mode" "$rd_steps")
    rd_o3=$(build/core-o3 "$rd_mode" "$rd_steps")
    if [ "$rd_o0" != "$rd_o3" ]; then
      printf 'Mismatch: mode %s, steps %s\n' "$rd_mode" "$rd_steps" >&2
      exit 1
    fi
    printf 'mode=%s steps=%s hash=%s\n' "$rd_mode" "$rd_steps" "$rd_o3"
  done
done
