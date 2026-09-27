#!/bin/sh
# Formatting and warning checks for the C sources. main.c and settings.c
# need the Pebble SDK include tree, so they are only checked by build-pebble.
set -eu
clang-format --dry-run --Werror core/*.c core/*.h pebble/src/c/*.c pebble/src/c/*.h tests/*.c
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only tests/core.c
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only tests/precision.c
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only tests/bench.c tests/fill.c
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only core/rd.c core/clock_mask.c tests/compare.c
# Both clock font sets, the default, then each one alone as RD_BUILD_FONT
# builds it.
for mode in 0 1 2 3; do
  cc -std=c99 -Wall -Wextra -Werror -DRD_MODE="$mode" -fsyntax-only pebble/src/c/core.c
  for font in 0 1; do
    cc -std=c99 -Wall -Wextra -Werror -DRD_MODE="$mode" -DRD_FONT="$font" -fsyntax-only pebble/src/c/core.c
  done
done
