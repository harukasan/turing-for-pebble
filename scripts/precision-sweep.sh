#!/bin/sh
# Run the storage precision experiment (tests/precision.c) for every
# candidate in scripts/precision-configs.txt, five presets, and two seeds.
# Results go to build/precision.jsonl, one JSON line per checkpoint;
# scripts/precision-aggregate.mjs summarizes them.
#
# usage: sh scripts/precision-sweep.sh [checkpoints...]   (default: 100 1000)
# PRECISION_CONFIGS names another candidate list, PRECISION_OUT another
# result file (default build/precision.jsonl).
set -eu
mkdir -p build
cc -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Werror tests/precision.c \
  -o build/precision -lm
checkpoints=${*:-100 1000}
jobs=$(nproc 2>/dev/null || echo 4)
# One job per line: config, preset, seed, checkpoints. xargs splits each
# line into the arguments of one build/precision run.
configs=${PRECISION_CONFIGS:-scripts/precision-configs.txt}
out=${PRECISION_OUT:-build/precision.jsonl}
grep -v '^#' "$configs" | grep -v '^$' |
  while read -r config; do
    for preset in 0 1 2 3 4; do
      for seed in 42 1234; do
        printf '%s %s %s %s\n' "$config" "$preset" "$seed" "$checkpoints"
      done
    done
  done >build/precision-jobs.txt
xargs -P "$jobs" -L 1 build/precision \
  <build/precision-jobs.txt >"$out"
node scripts/precision-aggregate.mjs "$out"
