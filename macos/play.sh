#!/usr/bin/env bash
# Try the macOS build straight from the build folder (no app bundle):
#   macos/play.sh "/path/to/SSX Tricky (USA).iso" [more game options]
# Log: dist-macos/logs/game-<time>.log. Saves and settings:
# ~/Library/Application Support/OpenTricky.
set -uo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
B="$HERE/port/build-macos"
OUT="$HERE/dist-macos/run"
ISO="${1:?usage: macos/play.sh /path/to/SSX\ Tricky\ \(USA\).iso}"; shift
mkdir -p "$OUT" "$HERE/dist-macos/logs"
cp "$B/SSX Tricky" "$OUT/"
cp "${SSX3_DEPS:-$HERE/../../SSX3/deps}/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib" "$OUT/"
xattr -cr "$OUT" 2>/dev/null
codesign --force --sign - "$OUT/libMoltenVK.dylib" "$OUT/SSX Tricky" 2>/dev/null
LOG="$HERE/dist-macos/logs/game-$(date +%Y%m%d-%H%M%S).log"
echo "log: $LOG"
cd "$OUT" && exec "./SSX Tricky" --direct "$ISO" "$@" 2>&1 | tee "$LOG"
