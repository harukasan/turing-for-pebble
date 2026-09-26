#!/bin/sh
# Formatting and warning checks for the C sources. main.c needs the Pebble
# SDK include tree, so it is only checked by build-pebble.
set -eu
clang-format --dry-run --Werror core/*.c core/*.h pebble/src/c/*.c pebble/src/c/*.h tests/*.c
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only tests/core.c
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only tests/precision.c
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only tests/bench.c tests/fill.c
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only core/rd.c core/clock_mask.c tests/compare.c
# The watch core with both models (RD_MODEL undefined) and folded to each
# model. FitzHugh-Nagumo (RD_MODEL 1) is an #error in the packed modes 0
# and 2, so those combinations are left out.
for mode in 0 1 2 3; do
  for font in 0 1; do
    for model in both 0 1; do
      if [ "$model" = 1 ] && { [ "$mode" = 0 ] || [ "$mode" = 2 ]; }; then
        continue
      fi
      model_define=""
      if [ "$model" != both ]; then
        model_define="-DRD_MODEL=$model"
      fi
      # shellcheck disable=SC2086
      cc -std=c99 -Wall -Wextra -Werror -DRD_MODE="$mode" -DRD_FONT="$font" \
        $model_define -fsyntax-only pebble/src/c/core.c
    done
  done
done
