#!/bin/sh
set -eu
mkdir -p build
node --experimental-transform-types scripts/gen-palettes.mjs --check
cc -std=c11 -O2 -Wall -Wextra -Werror tests/core.c -o build/core-test
build/core-test
cc -std=c11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/core.c -o build/core-sanitize
ASAN_OPTIONS=detect_leaks=1 build/core-sanitize
node tests/wasm.mjs
node --experimental-transform-types tests/adapter.mjs
