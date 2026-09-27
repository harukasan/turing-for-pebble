#!/bin/sh
# The fixed-point core must give identical fields at every optimization
# level, and identical to the recorded hashes of the numerical definition in
# tests/golden-hashes.txt (mode, steps, hash per line, optionally followed by
# the clock mask arguments of build/core-test, digital or `analog ...`, with
# seed 42).
set -eu
mkdir -p build
cc -O0 -std=c11 tests/core.c -o build/core-o0
cc -O3 -std=c11 -Werror tests/core.c -o build/core-o3
grep -v '^#' tests/golden-hashes.txt | grep -v '^$' |
  while read -r rd_mode rd_steps rd_golden rd_mask; do
    # rd_mask is empty, seven numbers, or `analog` and seven numbers, split
    # into separate arguments.
    # shellcheck disable=SC2086
    rd_o0=$(build/core-o0 "$rd_mode" "$rd_steps" $rd_mask)
    # shellcheck disable=SC2086
    rd_o3=$(build/core-o3 "$rd_mode" "$rd_steps" $rd_mask)
    if [ "$rd_o0" != "$rd_o3" ]; then
      printf 'Mismatch between -O0 and -O3: mode %s, steps %s %s\n' "$rd_mode" "$rd_steps" "$rd_mask" >&2
      exit 1
    fi
    if [ "$rd_o3" != "$rd_golden" ]; then
      printf 'Mismatch with tests/golden-hashes.txt: mode %s, steps %s %s, got %s, expected %s\n' \
        "$rd_mode" "$rd_steps" "$rd_mask" "$rd_o3" "$rd_golden" >&2
      exit 1
    fi
    printf 'mode=%s steps=%s hash=%s golden %s\n' "$rd_mode" "$rd_steps" "$rd_o3" "$rd_mask"
  done
