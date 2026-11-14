#!/usr/bin/env bash
# Inside opentricky-macx: configure + build the launcher (port/launcher/app)
# for macOS arm64, in port/build-launcher-macos.
#   docker run --rm -v "$PWD":/work/ot -v <SSX3>/deps:/work/deps:ro opentricky-macx /work/ot/macos/build-launcher.sh [targets]
# Output: port/build-launcher-macos/OpenTricky, ui/, THIRD-PARTY-LICENSES.txt
# (macos/package.sh puts them in the app).
set -uo pipefail
B=/work/ot/port/build-launcher-macos
if [ ! -f $B/build.ninja ] || [ "${RECONF:-0}" = 1 ]; then
cmake -S /work/ot/port/launcher/app -B $B -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/work/deps/macos-arm64.cmake \
  -DCMAKE_C_FLAGS="-ffile-prefix-map=/work/ot/= ${XCFLAGS:-}" \
  -DCMAKE_CXX_FLAGS="-ffile-prefix-map=/work/ot/= ${XCFLAGS:-}" \
  -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld -Wl,-rpath,@executable_path" \
  -DPython3_EXECUTABLE=/usr/bin/python3 || exit 1
fi
ninja -C $B -k 0 "$@"
