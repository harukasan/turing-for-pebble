#!/bin/sh
# Build the watchface modes 0, 1, and 3 with the Pebble SDK and keep their .pbw, .elf,
# and stack-usage files under build/pebble/. RD_BUILD_OPT (default -O3),
# RD_BUILD_PROFILE, RD_BUILD_FONT, RD_BUILD_RENDER, RD_BUILD_DEFINES, and
# RD_BUILD_LOG pass through to pebble/wscript. The load size of
# each ELF (text + data + bss, which all live in the 128 KiB app region next
# to the core allocation) must stay within LOAD_LIMIT bytes.
set -eu
LOAD_LIMIT=20480
size_tool=$(command -v arm-none-eabi-size || true)
if [ -z "$size_tool" ]; then
  size_tool=.local/share/pebble-sdk/SDKs/4.33.1/toolchain/arm-none-eabi/bin/arm-none-eabi-size
fi
mkdir -p build/pebble
for mode in 0 1 3; do
  (cd pebble && RD_BUILD_MODE=$mode pebble clean --sdk 4.33.1 && RD_BUILD_MODE=$mode pebble build --sdk 4.33.1)
  cp pebble/build/pebble.pbw build/pebble/mode-$mode.pbw
  cp pebble/build/emery/pebble-app.elf build/pebble/mode-$mode.elf
  mkdir -p build/pebble/stack-$mode
  cp pebble/build/src/c/*.su build/pebble/stack-$mode/
  load=$("$size_tool" build/pebble/mode-$mode.elf | awk 'NR == 2 { print $4 }')
  printf 'mode %s: load %s bytes\n' "$mode" "$load"
  if [ "$load" -gt "$LOAD_LIMIT" ]; then
    printf 'mode %s exceeds the %s byte load limit\n' "$mode" "$LOAD_LIMIT" >&2
    exit 1
  fi
done
