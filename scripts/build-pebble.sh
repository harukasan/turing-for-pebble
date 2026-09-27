#!/bin/sh
# Build the watchface modes 0, 1, and 3, which hold both clock faces, and
# mode 3 with the analog face fixed, with the Pebble SDK and keep their .pbw,
# .elf, and stack-usage files under build/pebble/ as mode-0, mode-1, mode-3,
# and mode-3-analog. RD_BUILD_OPT (default -O3), RD_BUILD_PROFILE,
# RD_BUILD_FONT, RD_BUILD_RENDER, RD_BUILD_DEFINES, and RD_BUILD_LOG pass
# through to pebble/wscript, and each variant sets its own RD_BUILD_MODE and
# RD_BUILD_FACE. The settings page and the phone script are built first, and
# RD_BUILD_CONFIG_URL makes the phone open a hosted copy of the page instead
# of the embedded one. The load size of each ELF (text + data + bss, which
# all live in the 128 KiB app region next to the core allocation) must stay
# within LOAD_LIMIT bytes. A measurement build with RD_BUILD_LOG=1 carries
# about 2.9 KB of logs and may raise the limit with RD_BUILD_LOAD_LIMIT.
# Production builds keep the default.
set -eu
LOAD_LIMIT=${RD_BUILD_LOAD_LIMIT:-20480}
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

for mode in 0 1 3; do
  build_variant "$mode" "$mode"
done
build_variant 3-analog 3 1
