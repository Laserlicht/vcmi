#!/usr/bin/env bash
# Cross-compile the Boost libraries VCMI needs for Nintendo Switch (devkitA64 + libnx).
# MUST be run inside the msys2 login environment (bash.exe -lc) so that
# DEVKITPRO=/opt/devkitpro and the devkitPro mounts resolve correctly.
#
# Strategy: use the Boost CMake superproject to compile only the required compiled
# libraries (+ their transitive deps) for aarch64-none-elf, then overlay the FULL
# unified header tree (from the b2 archive) so all header-only Boost libs VCMI uses
# are available. Installs into the devkitPro portlibs/switch prefix that the Switch
# CMake toolchain already searches.
set -euo pipefail

: "${DEVKITPRO:=/opt/devkitpro}"
export PATH="$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$DEVKITPRO/portlibs/switch/bin:$PATH"
CMAKE=cmake

BOOST_VER="${BOOST_VER:-1.86.0}"
SRCROOT="${SRCROOT:-$HOME/vcmi-switch-deps}"
BUILD="${BUILD:-$SRCROOT/build/boost}"
PREFIX="$DEVKITPRO/portlibs/switch"
CMAKESRC="$SRCROOT/boost-$BOOST_VER"
B2HEADERS="$CMAKESRC/boost"      # full unified header tree, included in the release archive

echo "==== [1/5] Download + extract Boost $BOOST_VER (CMake release archive) ===="
mkdir -p "$SRCROOT"; cd "$SRCROOT"
if [ ! -f "boost-$BOOST_VER-cmake.tar.gz" ]; then
  curl -L --retry 3 -o "boost-$BOOST_VER-cmake.tar.gz" \n    "https://github.com/boostorg/boost/releases/download/boost-$BOOST_VER/boost-$BOOST_VER-cmake.tar.gz"
fi
[ -f "$CMAKESRC/CMakeLists.txt" ] || tar xzf "boost-$BOOST_VER-cmake.tar.gz"
ls "$CMAKESRC/CMakeLists.txt" && echo "superproject root present"

echo "==== [1a/5] Patch boost/config/user.hpp: capability macros for newlib/libnx ===="
# Boost.Config does not auto-detect the aarch64-none-elf / newlib / Horizon target,
# so BOOST_HAS_PTHREADS (and friends) are left undefined and boost wrongly selects
# Win32 implementations. Inject the correct capability macros. This must be applied
# to BOTH the build-time config AND the headers we install for VCMI to consume.
patch_user_hpp() {
  local f="$1"
  [ -f "$f" ] || return 0
  if ! grep -q 'VCMI_SWITCH_BOOST_CONFIG' "$f"; then
    cat >> "$f" <<'EOF'

// --- VCMI_SWITCH_BOOST_CONFIG ---------------------------------------------
// Nintendo Switch (devkitA64 + libnx, newlib) provides POSIX pthreads and the
// usual POSIX clocks, but Boost.Config cannot auto-detect this target. Force the
// capability macros so boost selects the pthread / POSIX implementations.
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
  # newlib/libnx provides no <sys/mman.h>; force dlmalloc to the sbrk-based MORECORE
  # path. boost::container's dlmalloc is dormant at runtime in VCMI (only pmr users
  # touch it), so this only needs to compile and link.
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
cp -r "$B2HEADERS" "$PREFIX/include/"
echo "boost headers count:"; find "$PREFIX/include/boost" -name '*.hpp' | wc -l
echo "installed boost static libs:"; ls -1 "$PREFIX/lib/" | grep -i boost || echo "(NONE - build may have failed)"
echo "DONE"
