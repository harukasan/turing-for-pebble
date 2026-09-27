#!/bin/sh
# Build the watchface modes 0, 1, and 3, which hold both clock faces, and
# mode 3 with the analog face fixed, with the Pebble SDK and keep their .pbw,
# .elf, and stack-usage files under build/pebble/ as mode-0, mode-1, mode-3,
# and mode-3-analog. RD_BUILD_OPT (default -O3), RD_BUILD_PROFILE,
# RD_BUILD_FONT, RD_BUILD_RENDER, RD_BUILD_DEFINES, RD_BUILD_LOG, and
# RD_BUILD_MODEL pass through to pebble/wscript, and each variant sets its
# own RD_BUILD_MODE and RD_BUILD_FACE. The builds include both models unless
# RD_BUILD_MODEL is set, and RD_BUILD_MODEL=1 builds only modes 1 and 3,
# because the packed mode 0 runs only Gray-Scott. RD_BUILD_REFERENCE=1 first
# builds mode 3 folded to each model and prints their load sizes, without
# keeping those builds. The settings page and the phone script are built
# first, and RD_BUILD_CONFIG_URL makes the phone open a hosted copy of the
# page instead of the embedded one. The load size of each ELF (text + data +
# bss, which all live in the 128 KiB app region next to the core allocation)
# must stay within LOAD_LIMIT bytes, provisionally 22 KiB: with both models
# and both faces mode 3 loads about 21.3 KB, past the earlier 20,480 B, and
# the minimum free heap on the watch decides whether that holds
# (docs/validation.md). A measurement build with RD_BUILD_LOG=1 carries
# about 3.4 KB of logs and may raise the limit with RD_BUILD_LOAD_LIMIT.
# Production builds keep the default.
set -eu
LOAD_LIMIT=${RD_BUILD_LOAD_LIMIT:-22528}
size_tool=$(command -v arm-none-eabi-size || true)
if [ -z "$size_tool" ]; then
  size_tool=.local/share/pebble-sdk/SDKs/4.33.1/toolchain/arm-none-eabi/bin/arm-none-eabi-size
fi
pnpm run build:settings
mkdir -p build/pebble

# build_variant NAME MODE [FACE]: build one watchface and keep it as
# build/pebble/mode-NAME.*. Without FACE it holds both faces.
build_variant() {
  name=$1
  export RD_BUILD_MODE="$2" RD_BUILD_FACE="${3:-}"
  (cd pebble && pebble clean --sdk 4.33.1 && pebble build --sdk 4.33.1)
  cp pebble/build/pebble.pbw "build/pebble/mode-$name.pbw"
  cp pebble/build/emery/pebble-app.elf "build/pebble/mode-$name.elf"
  mkdir -p "build/pebble/stack-$name"
  cp pebble/build/src/c/*.su "build/pebble/stack-$name/"
  load=$("$size_tool" "build/pebble/mode-$name.elf" | awk 'NR == 2 { print $4 }')
  printf 'mode %s: load %s bytes\n' "$name" "$load"
  if [ "$load" -gt "$LOAD_LIMIT" ]; then
    printf 'mode %s exceeds the %s byte load limit\n' "$name" "$LOAD_LIMIT" >&2
    exit 1
  fi
}

if [ "${RD_BUILD_REFERENCE:-0}" = 1 ]; then
  for model in 0 1; do
    (cd pebble && RD_BUILD_MODE=3 RD_BUILD_FACE='' RD_BUILD_MODEL=$model pebble clean --sdk 4.33.1 &&
      RD_BUILD_MODE=3 RD_BUILD_FACE='' RD_BUILD_MODEL=$model pebble build --sdk 4.33.1)
    load=$("$size_tool" pebble/build/emery/pebble-app.elf | awk 'NR == 2 { print $4 }')
    printf 'mode 3 folded to model %s (reference, not kept): load %s bytes\n' "$model" "$load"
  done
fi
modes="0 1 3"
if [ "${RD_BUILD_MODEL:-}" = 1 ]; then
  modes="1 3"
fi
# shellcheck disable=SC2086 # $modes is a list of words
for mode in $modes; do
  build_variant "$mode" "$mode"
done
build_variant 3-analog 3 1
