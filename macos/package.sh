#!/usr/bin/env bash
# On the Mac, after the Docker builds (macos/build-game.sh, macos/build-launcher.sh):
# dist-macos/OpenTricky.app
# and OpenTricky-macos-arm64.dmg. Uses only the system's own tools (codesign,
# hdiutil); nothing is installed. Please don't share your build: it contains
# code translated from your own game disc.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
B="$HERE/port/build-macos"; LB="$HERE/port/build-launcher-macos"; OUT="$HERE/dist-macos"
DEPS="${SSX3_DEPS:-$HERE/../../SSX3/deps}"   # the Docker deps folder (MoltenVK); SSX3_DEPS to point elsewhere
VER="$(sed -n 's/^#define OT_VERSION_STR *"\(.*\)"/\1/p' "$HERE/port/src/version.h" | head -1)"; VER="${VER:-0.1.5}-macos"
for f in "$B/SSX Tricky" "$LB/OpenTricky" "$LB/ui/home.rml" "$LB/THIRD-PARTY-LICENSES.txt"; do
    [ -e "$f" ] || { echo "missing: $f (build the game and the launcher first)" >&2; exit 1; }
done
WORK="$(mktemp -d)"; APP="$WORK/OpenTricky.app"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT" "$APP/Contents/MacOS" "$APP/Contents/Resources/licenses"
cp "$B/SSX Tricky" "$DEPS/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib" "$APP/Contents/MacOS/"
cp "$HERE/macos/OpenTricky.sh" "$APP/Contents/MacOS/OpenTricky"
chmod +x "$APP/Contents/MacOS/OpenTricky"
# The launcher beside the game (it starts "SSX Tricky" from its own folder);
# its interface and licences in Resources, where it finds them (SDL_GetBasePath).
cp "$LB/OpenTricky" "$APP/Contents/MacOS/OpenTricky-launcher"
cp -R "$LB/ui" "$APP/Contents/Resources/ui"
cp "$LB/THIRD-PARTY-LICENSES.txt" "$APP/Contents/Resources/"
# The HD HUD art of the Windows release (not in the repository), when it is
# put in macos/hd-hud: beside the game, where XBOX_HD_HUD=1 looks.
[ -d "$HERE/macos/hd-hud" ] && cp -R "$HERE/macos/hd-hud" "$APP/Contents/MacOS/hd-hud"
sed "s/@VER@/$VER/" "$HERE/macos/Info.plist.in" > "$APP/Contents/Info.plist"
L="$APP/Contents/Resources/licenses"
cp "$HERE/LICENSE" "$L/OpenTricky-GPL-3.0.txt"
cp "$DEPS/MoltenVK/LICENSE" "$L/MoltenVK-Apache-2.0.txt"
cp "$HERE/port/third_party/sdl3/LICENSE.txt" "$L/SDL3-zlib.txt" 2>/dev/null || true
xattr -cr "$APP"
codesign --force --deep --sign - "$APP"
codesign -v --deep "$APP"
STAGE="$(mktemp -d)/SSX Tricky"; mkdir -p "$STAGE"
ditto "$APP" "$STAGE/OpenTricky.app"
ln -s /Applications "$STAGE/Applications"
hdiutil create -volname "SSX Tricky" -srcfolder "$STAGE" -fs HFS+ -format UDZO -ov "$WORK/OpenTricky-macos-arm64.dmg" >/dev/null
rm -rf "$OUT/OpenTricky.app"
ditto "$APP" "$OUT/OpenTricky.app"
cp "$WORK/OpenTricky-macos-arm64.dmg" "$OUT/"
ls -la "$OUT"
