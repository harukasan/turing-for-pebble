#!/bin/sh
# Formatting and warning checks for the C sources. main.c and settings.c
# need the Pebble SDK include tree, so they are only checked by build-pebble.
set -eu
clang-format --dry-run --Werror core/*.c core/*.h pebble/src/c/*.c pebble/src/c/*.h tests/*.c
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only tests/core.c
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only tests/bench.c tests/fill.c
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only core/rd.c core/clock_mask.c tests/compare.c
# The watch core with both models (RD_MODEL undefined) and folded to each
# model. Both clock font sets, the default, then each one alone as RD_BUILD_FONT
# builds it, each with both faces and each face alone as RD_BUILD_FACE builds
# it, with both models and each model alone as RD_BUILD_MODEL builds it, and
# nearest rendering as RD_BUILD_RENDER=0 builds it.
cc -std=c99 -Wall -Wextra -Werror -fsyntax-only pebble/src/c/core.c
cc -std=c99 -Wall -Wextra -Werror -DRD_RENDER_FLAGS=0 -fsyntax-only pebble/src/c/core.c
for font in 0 1; do
  cc -std=c99 -Wall -Wextra -Werror -DRD_FONT="$font" -fsyntax-only pebble/src/c/core.c
  for face in 0 1; do
    for model in both 0 1; do
      model_define=""
      if [ "$model" != both ]; then
        model_define="-DRD_MODEL=$model"
      fi
      # shellcheck disable=SC2086
      cc -std=c99 -Wall -Wextra -Werror -DRD_FONT="$font" -DRD_FACE="$face" \
        $model_define -fsyntax-only pebble/src/c/core.c
    done
  done
done
