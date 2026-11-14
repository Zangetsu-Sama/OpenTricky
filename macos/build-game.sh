#!/usr/bin/env bash
# Inside opentricky-macx: configure + build the game for macOS arm64.
#   docker run --rm -v "$PWD":/work/ot -v <SSX3>/deps:/work/deps:ro opentricky-macx /work/ot/macos/build-game.sh [targets]
set -uo pipefail
B=/work/ot/port/build-macos
export PKG_CONFIG_LIBDIR=/work/deps/prefix-arm64/lib/pkgconfig
if [ ! -f $B/build.ninja ] || [ "${RECONF:-0}" = 1 ]; then
cmake -S /work/ot/port -B $B -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/work/deps/macos-arm64.cmake \
  -DCMAKE_C_FLAGS_RELEASE="-O3 -g -DNDEBUG" \
  -DCMAKE_C_FLAGS="-Wno-error=int-conversion -Wno-parentheses-equality -ffile-prefix-map=/work/ot/= ${XCFLAGS:-}" \
  -DSDL2_DIR=/work/deps/cmake/SDL2 \
  -DOPENSSL_ROOT_DIR=/work/deps/brew/openssl@3 -DOPENSSL_USE_STATIC_LIBS=TRUE \
  -DVulkan_INCLUDE_DIR=/work/deps/MoltenVK/MoltenVK/include \
  -DVulkan_LIBRARY=/work/deps/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib \
  -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld -Wl,-rpath,@executable_path -Wl,--error-limit=0" -DGLSLC=/usr/bin/glslc -DPython3_EXECUTABLE=/usr/bin/python3 || exit 1
fi
ninja -C $B -k 0 "$@"
