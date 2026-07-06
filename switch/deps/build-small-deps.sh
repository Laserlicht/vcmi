#!/usr/bin/env bash
# Cross-compile the small from-source dependencies VCMI needs for Nintendo Switch:
#   - Lua 5.4.7  (plain Lua; LuaJIT is impossible on Switch - no JIT/W^X. VCMI's
#                 luascript needs the 5.2+ API, so 5.4 rather than 5.1)
#   - libsquish  (DXT/S3TC codec, pure C++, no OS deps)
# Installs static libs + headers into the devkitPro portlibs/switch prefix that the
# Switch CMake toolchain already searches. Run inside msys2 login bash.
set -euo pipefail

: "${DEVKITPRO:=/opt/devkitpro}"
export PATH="$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH"
PREFIX="$DEVKITPRO/portlibs/switch"
SRCROOT="${SRCROOT:-$HOME/vcmi-switch-deps}"
LUA_VER="${LUA_VER:-5.4.7}"
SQUISH_VER="${SQUISH_VER:-1.15}"

CC=aarch64-none-elf-gcc
CXX=aarch64-none-elf-g++
AR=aarch64-none-elf-ar
RANLIB=aarch64-none-elf-ranlib
ARCH_FLAGS="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec -O2 -ffunction-sections -fdata-sections -D__SWITCH__ -I$DEVKITPRO/libnx/include"

echo "############ Lua 5.4.7 ############"
# VCMI's luascript targets the Lua 5.2+ API (LUA_OK, LUA_RIDX_GLOBALS, lua_rawlen)
# in addition to the LuaJIT/5.1 setfenv path (which is #if LUA_VERSION_NUM==501 guarded).
# Plain Lua 5.1 hits the unguarded LUA_OK, so we use 5.4 (all those APIs are native;
# only the 5.1-only setfenv path is compiled out, as intended).
mkdir -p "$SRCROOT"; cd "$SRCROOT"
[ -f "lua-$LUA_VER.tar.gz" ] || curl -L --retry 3 -o "lua-$LUA_VER.tar.gz" "https://www.lua.org/ftp/lua-$LUA_VER.tar.gz"
[ -d "lua-$LUA_VER" ] || tar xzf "lua-$LUA_VER.tar.gz"
cd "lua-$LUA_VER/src"
# Build the Lua core + lib as a static archive for aarch64. Generic C config
# (no LUA_USE_*), which is correct for newlib.
rm -f ./*.o
LUA_OBJS="lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes \
lparser lstate lstring ltable ltm lundump lvm lzio \
lauxlib lbaselib lcorolib ldblib liolib lmathlib loadlib loslib lstrlib ltablib lutf8lib linit"
for o in $LUA_OBJS; do
  $CC $ARCH_FLAGS -c "$o.c" -o "$o.o"
done
$AR rcs liblua.a $(for o in $LUA_OBJS; do echo "$o.o"; done)
$RANLIB liblua.a
mkdir -p "$PREFIX/include" "$PREFIX/lib"
cp liblua.a "$PREFIX/lib/"
cp lua.h luaconf.h lualib.h lauxlib.h "$PREFIX/include/"
# Lua's C++-facing umbrella header
cat > "$PREFIX/include/lua.hpp" <<'EOF'
// lua.hpp - C++ wrapper for Lua headers (provided for VCMI on Switch)
extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}
EOF
echo "Lua installed: $(ls -l $PREFIX/lib/liblua.a)"

echo "############ libsquish ############"
cd "$SRCROOT"
[ -f "libsquish-$SQUISH_VER.tgz" ] || curl -L --retry 3 -o "libsquish-$SQUISH_VER.tgz" "https://downloads.sourceforge.net/libsquish/libsquish-$SQUISH_VER.tgz"
[ -d "libsquish-$SQUISH_VER" ] || { mkdir -p "libsquish-$SQUISH_VER"; tar xzf "libsquish-$SQUISH_VER.tgz" -C "libsquish-$SQUISH_VER"; }
cd "libsquish-$SQUISH_VER"
rm -f ./*.o
SQUISH_SRC="alpha clusterfit colourblock colourfit colourset maths rangefit singlecolourfit squish"
for s in $SQUISH_SRC; do
  $CXX $ARCH_FLAGS -I. -std=gnu++20 -c "$s.cpp" -o "$s.o"
done
$AR rcs libsquish.a $(for s in $SQUISH_SRC; do echo "$s.o"; done)
$RANLIB libsquish.a
cp libsquish.a "$PREFIX/lib/"
cp squish.h "$PREFIX/include/"
echo "libsquish installed: $(ls -l $PREFIX/lib/libsquish.a)"

echo "ALL SMALL DEPS DONE"
