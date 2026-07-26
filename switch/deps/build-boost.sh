#!/usr/bin/env bash
#
# build-boost.sh, part of VCMI engine
#
# Authors: listed in file AUTHORS in main folder
#
# License: GNU General Public License v2.0 or later
# Full text of license available in license.txt file, in main folder
#
# Cross-compiles the Boost libs VCMI needs for Switch (devkitA64 + libnx): builds the
# compiled libraries via Boost's CMake superproject, then overlays the full classic
# header tree (for header-only libs), into devkitPro's portlibs/switch prefix.
set -euo pipefail

: "${DEVKITPRO:=/opt/devkitpro}"
export PATH="$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$DEVKITPRO/portlibs/switch/bin:$PATH"
CMAKE=cmake

BOOST_VER="${BOOST_VER:-1.86.0}"
SRCROOT="${SRCROOT:-$HOME/vcmi-switch-deps}"
BUILD="${BUILD:-$SRCROOT/build/boost}"
PREFIX="$DEVKITPRO/portlibs/switch"
CMAKESRC="$SRCROOT/boost-$BOOST_VER"
# the CMake superproject archive has no unified boost/ header tree (needed for
# header-only libs like boost::format); fetch the classic full release for that
B2ARCHIVE="$SRCROOT/boost_${BOOST_VER//./_}.tar.gz"
B2SRC="$SRCROOT/boost_${BOOST_VER//./_}"
B2HEADERS="$B2SRC/boost"

echo "==== [1/5] Download + extract Boost $BOOST_VER (CMake release archive) ===="
mkdir -p "$SRCROOT"; cd "$SRCROOT"
if [ ! -f "boost-$BOOST_VER-cmake.tar.gz" ]; then
  curl -L --retry 3 -o "boost-$BOOST_VER-cmake.tar.gz" \
    "https://github.com/boostorg/boost/releases/download/boost-$BOOST_VER/boost-$BOOST_VER-cmake.tar.gz"
fi
[ -f "$CMAKESRC/CMakeLists.txt" ] || tar xzf "boost-$BOOST_VER-cmake.tar.gz"
ls "$CMAKESRC/CMakeLists.txt" && echo "superproject root present"

if [ ! -d "$B2HEADERS" ]; then
  [ -f "$B2ARCHIVE" ] || curl -L --retry 3 -o "$B2ARCHIVE" \
    "https://archives.boost.io/release/$BOOST_VER/source/boost_${BOOST_VER//./_}.tar.gz"
  tar xzf "$B2ARCHIVE" -C "$SRCROOT" "boost_${BOOST_VER//./_}/boost"
fi

echo "==== [1a/5] Patch boost/config/user.hpp: capability macros for newlib/libnx ===="
# Boost.Config can't auto-detect this target and wrongly selects Win32 implementations;
# force the POSIX capability macros in both the build-time config and installed headers
patch_user_hpp() {
  local f="$1"
  [ -f "$f" ] || return 0
  if ! grep -q 'VCMI_SWITCH_BOOST_CONFIG' "$f"; then
    cat >> "$f" <<'EOF'

// devkitA64/libnx provides POSIX pthreads/clocks that Boost.Config can't auto-detect
#if defined(__SWITCH__)
#  ifndef BOOST_HAS_PTHREADS
#    define BOOST_HAS_PTHREADS
#  endif
#  ifndef BOOST_HAS_UNISTD_H
#    define BOOST_HAS_UNISTD_H
#  endif
#  ifndef BOOST_HAS_SCHED_YIELD
#    define BOOST_HAS_SCHED_YIELD
#  endif
#  ifndef BOOST_HAS_GETTIMEOFDAY
#    define BOOST_HAS_GETTIMEOFDAY
#  endif
#  ifndef BOOST_HAS_NANOSLEEP
#    define BOOST_HAS_NANOSLEEP
#  endif
#  ifndef BOOST_HAS_CLOCK_GETTIME
#    define BOOST_HAS_CLOCK_GETTIME
#  endif
#endif
// --------------------------------------------------------------------------
EOF
    echo "patched $f"
  else
    echo "already patched $f"
  fi
}
patch_user_hpp "$CMAKESRC/libs/config/include/boost/config/user.hpp"
patch_user_hpp "$B2HEADERS/config/user.hpp"

echo "==== [1b/5] Patch boost::container dlmalloc for newlib (no sys/mman.h) ===="
DLMALLOC="$CMAKESRC/libs/container/src/dlmalloc_2_8_6.c"
if ! grep -q 'VCMI_SWITCH_NO_MMAP' "$DLMALLOC"; then
  # newlib has no <sys/mman.h>; force the sbrk-based path (dlmalloc is dormant at
  # runtime in VCMI, so this only needs to compile and link)
  printf '%s\n' \
    '/* VCMI_SWITCH_NO_MMAP: newlib has no sys/mman.h - disable mmap in dlmalloc */' \
    '#if defined(__SWITCH__) && !defined(HAVE_MMAP)' \
    '#define HAVE_MMAP 0' \
    '#endif' \
    > "$DLMALLOC.patched"
  cat "$DLMALLOC" >> "$DLMALLOC.patched"
  mv "$DLMALLOC.patched" "$DLMALLOC"
  echo "patched $DLMALLOC"
else
  echo "already patched"
fi

echo "==== [2/5] Configure (Switch toolchain, Ninja) ===="
rm -rf "$BUILD"
"$CMAKE" -G Ninja -S "$CMAKESRC" -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=OFF \
  -DBOOST_INSTALL_LAYOUT=system \
  -DBOOST_INCLUDE_LIBRARIES="filesystem;program_options;date_time" \
  2>&1 | tail -25
echo "---- compiled boost targets that will be built: ----"
ninja -C "$BUILD" -t targets all 2>/dev/null | grep -oE 'libs/[a-z_]+/CMakeFiles/boost_[a-z_]+\.dir' | sort -u | head -40 || true

echo "==== [3/5] Build ===="
"$CMAKE" --build "$BUILD" -j4 2>&1 | tail -40

echo "==== [4/5] Install compiled libs + config ===="
"$CMAKE" --install "$BUILD" 2>&1 | tail -15

echo "==== [5/5] Overlay FULL unified header tree (covers header-only libs) ===="
rm -rf "$PREFIX/include/boost"
cp -r "$B2HEADERS" "$PREFIX/include/"
echo "boost headers count:"; find "$PREFIX/include/boost" -name '*.hpp' | wc -l
echo "installed boost static libs:"; ls -1 "$PREFIX/lib/" | grep -i boost || echo "(NONE - build may have failed)"
echo "DONE"
