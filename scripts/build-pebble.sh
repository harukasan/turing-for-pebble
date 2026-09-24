#!/bin/sh
set -eu
mkdir -p build/pebble
for mode in 0 1; do
  (cd pebble && RD_BUILD_MODE=$mode pebble clean --sdk 4.33.1 && RD_BUILD_MODE=$mode pebble build --sdk 4.33.1)
  cp pebble/build/pebble.pbw build/pebble/mode-$mode.pbw
  cp pebble/build/emery/pebble-app.elf build/pebble/mode-$mode.elf
  mkdir -p build/pebble/stack-$mode
  cp pebble/build/src/c/*.su build/pebble/stack-$mode/
done
