#!/usr/bin/env bash
# Configure & build VCMI for Nintendo Switch (devkitA64 + libnx).
# Run inside the devkitPro msys2 login shell:
#   /c/msys64/usr/bin/bash.exe -lc 'bash .../switch/build-vcmi-switch.sh'
#
# Prerequisites (all produced by the scripts in switch/deps/):
#   - Boost, libiconv, Lua 5.4, libsquish installed into $DEVKITPRO/portlibs/switch
#   - SDL2 + SDL2_image/mixer/ttf, ffmpeg, zlib, minizip from devkitPro pacman
#   - The TBB sequential shim lives in-tree at vcmi/switch/tbb-shim
set -euo pipefail

: "${DEVKITPRO:=/opt/devkitpro}"
export PATH="$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$DEVKITPRO/portlibs/switch/bin:$PATH"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VCMI_SRC="${VCMI_SRC:-$(cd "$SCRIPT_DIR/.." && pwd)}" # repo root (parent of switch/)
BUILD="${1:-$VCMI_SRC/build-switch}"
JOBS="${JOBS:-4}"

echo "==== Configure VCMI (Switch toolchain) ===="
cmake -G Ninja -S "$VCMI_SRC" -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$VCMI_SRC/cmake_modules/Toolchain_switch.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_CLIENT=ON \
  -DENABLE_SERVER=OFF \
  -DENABLE_LAUNCHER=OFF \
  -DENABLE_EDITOR=OFF \
  -DENABLE_TEST=OFF \
  -DENABLE_LOBBY=OFF \
  -DENABLE_VIDEO=ON \
  -DENABLE_MMAI=OFF \
  -DENABLE_INNOEXTRACT=OFF \
  -DENABLE_TRANSLATIONS=OFF \
  -DENABLE_PCH=ON \
  "${@:2}"

echo "==== Build ===="
cmake --build "$BUILD" -j"$JOBS"

echo "==== Result ===="
find "$BUILD" -name 'vcmiclient.nro' -o -name 'vcmiclient.elf' 2>/dev/null | sed 's/^/  /'
echo "DONE"
