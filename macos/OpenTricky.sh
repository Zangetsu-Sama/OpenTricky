#!/bin/bash
# The app's entry (CFBundleExecutable). Everything it writes stays in one
# folder beside the app, "SSX Tricky data" (settings.ini, saves, logs):
# nothing goes into the system. It starts the launcher (OpenTricky-launcher,
# beside this script), where the player chooses the disc image and the
# settings; the launcher's PLAY starts "SSX Tricky" (beside it too), which
# reads the same settings.ini through OPENTRICKY_DATA_DIR.
HERE="$(cd "$(dirname "$0")" && pwd)"
APPDIR="$(cd "$HERE/../../.." && pwd)"
DATA="$APPDIR/SSX Tricky data"
mkdir -p "$DATA/saves" "$DATA/logs"
export OPENTRICKY_DATA_DIR="$DATA" OPENTRICKY_SAVE_DIR="$DATA/saves"
ARGS=()
# An earlier app kept the disc image's path in disc-image.txt: taken once,
# while there is no settings.ini yet (the launcher then keeps it there).
if [ ! -f "$DATA/settings.ini" ] && [ -f "$DATA/disc-image.txt" ]; then
    ISO="$(cat "$DATA/disc-image.txt")"
    [ -n "$ISO" ] && [ -f "$ISO" ] && ARGS=(--iso "$ISO")
fi
LOG="$DATA/logs/launcher-$(date +%Y%m%d-%H%M%S).log"
cd "$DATA" && exec "$HERE/OpenTricky-launcher" "${ARGS[@]}" >"$LOG" 2>&1
