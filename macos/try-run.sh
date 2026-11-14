#!/usr/bin/env bash
# Run the build for N seconds (default 30) with the user's ISO; log in dist-macos/logs/<name>.log
HERE="$(cd "$(dirname "$0")/.." && pwd)"; N="${1:-30}"; NAME="${2:-run}"
mkdir -p "$HERE/dist-macos/run" "$HERE/dist-macos/logs"
cp "$HERE/port/build-macos/SSX Tricky" "$HERE/dist-macos/run/"
cp "${SSX3_DEPS:-$HERE/../../SSX3/deps}/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib" "$HERE/dist-macos/run/"
xattr -cr "$HERE/dist-macos/run"; codesign --force --sign - "$HERE/dist-macos/run/libMoltenVK.dylib" "$HERE/dist-macos/run/SSX Tricky" 2>/dev/null
cd "$HERE/dist-macos/run"
# everything stays in the project: settings in dist-macos/data, saves in dist-macos/run/hdd
export OT_SHOT_DIR="$HERE/dist-macos/shots/$NAME"; rm -rf "$OT_SHOT_DIR"; mkdir -p "$OT_SHOT_DIR"
export OT_MUTE="${OT_MUTE:-1}"   # the test runs are silent
export OPENTRICKY_DATA_DIR="$HERE/dist-macos/data"; mkdir -p "$OPENTRICKY_DATA_DIR"
"./SSX Tricky" --direct "$HOME/Downloads/SSX Tricky (USA).iso" > "$HERE/dist-macos/logs/$NAME.log" 2>&1 &
P=$!; for i in $(seq 1 "$N"); do sleep 1; kill -0 $P 2>/dev/null || break; done
kill -0 $P 2>/dev/null && { kill $P; echo "(still running after ${N}s: stopped)"; } || { wait $P; echo "exited: $?"; }
