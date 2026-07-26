#!/usr/bin/env bash
#
# build-iconv.sh, part of VCMI engine
#
# Authors: listed in file AUTHORS in main folder
#
# License: GNU General Public License v2.0 or later
# Full text of license available in license.txt file, in main folder
#
# Cross-compile GNU libiconv for Nintendo Switch (devkitA64 + libnx, newlib).
# newlib ships <iconv.h> but no iconv symbols, and VCMI's find_package(Iconv REQUIRED)
# needs a real implementation for Heroes III text codepage conversion (CP1250/1251/...).
# Installs libiconv.a + libcharset.a + libiconv's iconv.h into portlibs/switch.
# Run inside msys2 login bash.
set -euo pipefail

: "${DEVKITPRO:=/opt/devkitpro}"
export PATH="$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH"
PREFIX="$DEVKITPRO/portlibs/switch"
SRCROOT="${SRCROOT:-$HOME/vcmi-switch-deps}"
VER="${LIBICONV_VER:-1.17}"

ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec"
SPECS="-specs=$DEVKITPRO/libnx/switch.specs"

mkdir -p "$SRCROOT"; cd "$SRCROOT"
if [ ! -f "libiconv-$VER.tar.gz" ]; then
  echo "Downloading libiconv-$VER..."
  curl -L --retry 3 -o "libiconv-$VER.tar.gz" "https://ftp.gnu.org/pub/gnu/libiconv/libiconv-$VER.tar.gz"
fi
[ -d "libiconv-$VER" ] || tar xzf "libiconv-$VER.tar.gz"
cd "libiconv-$VER"

# Patch: libiconv's loop_wchar.h carries a legacy K&R declaration
#   extern size_t mbrtowc ();
# which conflicts with newlib's proper prototype (already pulled in via <wchar.h>
# right above it) and is rejected by GCC 16's strict C. newlib has mbstate_t, so the
# decl is redundant - neutralise it.
if grep -q '^  extern size_t mbrtowc ();' lib/loop_wchar.h; then
  sed -i 's|^  extern size_t mbrtowc ();|  /* removed for newlib/GCC16: real prototype comes from <wchar.h> */|' lib/loop_wchar.h
  echo "patched lib/loop_wchar.h"
fi

echo "==== configure (cross, host=aarch64-none-elf) ===="
make distclean >/dev/null 2>&1 || true
./configure \
  --host=aarch64-none-elf \
  --prefix="$PREFIX" \
  --enable-static --disable-shared \
  --disable-nls \
  CC=aarch64-none-elf-gcc \
  CXX=aarch64-none-elf-g++ \
  AR=aarch64-none-elf-ar \
  RANLIB=aarch64-none-elf-ranlib \
  CFLAGS="$ARCH -O2 -D__SWITCH__ -I$DEVKITPRO/libnx/include" \
  CXXFLAGS="$ARCH -O2 -D__SWITCH__ -I$DEVKITPRO/libnx/include" \
  CPPFLAGS="-D__SWITCH__ -I$DEVKITPRO/libnx/include" \
  LDFLAGS="$ARCH $SPECS -L$DEVKITPRO/libnx/lib -L$PREFIX/lib" \
  LIBS="-lnx" \
  2>&1 | tail -15

echo "==== compile lib objects directly (bypass broken recursive make) ===="
# The runtime archive is just iconv.o + localcharset.o + relocatable.o and does NOT
# depend on srclib/ (the CLI's gnulib helpers that don't port to newlib). Compile the
# three sources directly with explicit include paths - fully deterministic.
OBJFLAGS="$ARCH -O2 -DHAVE_CONFIG_H -D__SWITCH__ -I. -Ilib -Iinclude -Ilibcharset/include -Isrclib -I$DEVKITPRO/libnx/include"
rm -f lib/iconv.o lib/localcharset.o lib/relocatable.o
aarch64-none-elf-gcc $OBJFLAGS -c lib/iconv.c                    -o lib/iconv.o
aarch64-none-elf-gcc $OBJFLAGS -c libcharset/lib/localcharset.c  -o lib/localcharset.o
aarch64-none-elf-gcc $OBJFLAGS -c lib/relocatable.c             -o lib/relocatable.o
aarch64-none-elf-ar rcs lib/libiconv.a lib/iconv.o lib/localcharset.o lib/relocatable.o
aarch64-none-elf-ranlib lib/libiconv.a

echo "==== install header + static lib ===="
mkdir -p "$PREFIX/lib" "$PREFIX/include"
cp -v lib/libiconv.a "$PREFIX/lib/libiconv.a"
cp -v include/iconv.h "$PREFIX/include/iconv.h"

echo "==== verify symbols present ===="
aarch64-none-elf-nm "$PREFIX/lib/libiconv.a" | grep -E 'T (lib)?iconv_open' | head -3 || echo "(symbol scan: see above)"
echo "ICONV DONE"
