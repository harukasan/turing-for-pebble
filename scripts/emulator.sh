#!/bin/sh
# Try a built watchface in the Emery emulator. Every emulator command uses
# --vnc, because changing launch options restarts the emulator.
#
# usage: sh scripts/emulator.sh install [mode]        build if needed, install
#        sh scripts/emulator.sh screenshot [png]      default build/emery.png
#        sh scripts/emulator.sh logs                  stream app logs (Ctrl-C)
#        sh scripts/emulator.sh kill                  stop the emulator
set -eu
# The pebble tool lives in the mise-managed virtualenv; re-run under mise
# when it is not already on PATH.
if ! command -v pebble >/dev/null 2>&1; then
  exec mise exec -- sh "$0" "$@"
fi
command=${1:-install}
case "$command" in
  install)
    mode=${2:-0}
    pbw=build/pebble/mode-$mode.pbw
    if [ ! -f "$pbw" ]; then
      echo "Building $pbw" >&2
      sh scripts/build-pebble.sh
    fi
    pebble install --emulator emery --vnc "$pbw"
    ;;
  screenshot)
    pebble screenshot --emulator emery --vnc --no-open "${2:-build/emery.png}"
    ;;
  logs)
    pebble logs --emulator emery --vnc
    ;;
  kill)
    pebble kill
    ;;
  *)
    echo "usage: $0 install [mode] | screenshot [png] | logs | kill" >&2
    exit 2
    ;;
esac
