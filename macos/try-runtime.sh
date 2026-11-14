#!/usr/bin/env bash
# Inside ssx3-macx: configure + build the xboxrecomp runtime alone for macOS arm64 (diagnostic).
set -uo pipefail
B=/work/ot/build-macos-runtime
export PKG_CONFIG_LIBDIR=/work/deps/prefix-arm64/lib/pkgconfig
cmake -S /work/ot/xboxrecomp -B $B -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/work/deps/macos-arm64.cmake \
  -DSDL2_DIR=/work/deps/cmake/SDL2 \
  -DOPENSSL_ROOT_DIR=/work/deps/brew/openssl@3 -DOPENSSL_USE_STATIC_LIBS=TRUE -DCMAKE_C_FLAGS=-ferror-limit=0 >$B.cfg.log 2>&1 || { tail -30 $B.cfg.log; exit 1; }
ninja -C $B -k 0 "$@" > $B.log 2>&1
echo "exit=$?"
