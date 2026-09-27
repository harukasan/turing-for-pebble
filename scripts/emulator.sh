#!/bin/sh
# Try a built watchface in the Emery emulator or on a physical watch. Every
# emulator command uses --vnc, because changing launch options restarts the
# emulator. Physical-watch commands go through the phone's Pebble app
# developer connection at PEBBLE_PHONE (an IP address, optionally :port).
#
# usage: sh scripts/emulator.sh install [build]        build if needed, install
#        sh scripts/emulator.sh screenshot [png]       default build/emery.png
#        sh scripts/emulator.sh logs                   stream app logs (Ctrl-C)
#        sh scripts/emulator.sh kill                   stop the emulator
#        sh scripts/emulator.sh config                 open the installed
#                                                      face's settings page
#        sh scripts/emulator.sh device-install [build] install on the watch and
#                                                      stream its logs
#        sh scripts/emulator.sh device-logs            stream watch logs
#        sh scripts/emulator.sh device-screenshot [png] default build/watch.png
set -eu
# The pebble tool lives in the mise-managed virtualenv; re-run under mise
# when it is not already on PATH.
if ! command -v pebble >/dev/null 2>&1; then
  exec mise exec -- sh "$0" "$@"
fi
command=${1:-install}

# Build the watchfaces unless the .pbw of build $1 (watchface, the default,
# or watchface-analog) already exists, printing its path.
built_pbw() {
  pbw=build/pebble/${1:-watchface}.pbw
  if [ ! -f "$pbw" ]; then
    echo "Building $pbw" >&2
    sh scripts/build-pebble.sh
  fi
  echo "$pbw"
}

phone() {
  echo "${PEBBLE_PHONE:?set PEBBLE_PHONE to the phone IP shown by the Pebble app developer connection}"
}

case "$command" in
  install)
    pebble install --emulator emery --vnc "$(built_pbw "${2:-}")"
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
  config)
    pebble emu-app-config --emulator emery --vnc
    ;;
  device-install)
    # --logs keeps streaming after the install, so the startup summary that
    # the watchface logs when its 30 s startup finishes is captured.
    pebble install --phone "$(phone)" --logs "$(built_pbw "${2:-}")"
    ;;
  device-logs)
    pebble logs --phone "$(phone)"
    ;;
  device-screenshot)
    pebble screenshot --phone "$(phone)" --no-open "${2:-build/watch.png}"
    ;;
  *)
    echo "usage: $0 install [build] | screenshot [png] | logs | kill | config |" \
      "device-install [build] | device-logs | device-screenshot [png]" >&2
    exit 2
    ;;
esac
