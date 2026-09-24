#!/bin/sh
# Formatting and warning checks for the C sources. main.c needs the Pebble
# SDK include tree, so it is only checked by build-pebble.
set -eu
clang-format --dry-run --Werror core/*.c core/*.h pebble/src/c/*.c pebble/src/c/*.h tests/*.c
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only tests/core.c
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only tests/precision.c
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only core/rd.c tests/compare.c
for mode in 0 1 2; do
  cc -std=c99 -Wall -Wextra -Werror -DRD_MODE="$mode" -fsyntax-only pebble/src/c/core.c
done
