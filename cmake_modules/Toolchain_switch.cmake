# VCMI CMake toolchain for Nintendo Switch (devkitA64 + libnx, aarch64).
#
# Usage (inside the devkitPro msys2 environment, so that DEVKITPRO is set):
#   cmake -G Ninja -B build/switch \
#         -DCMAKE_TOOLCHAIN_FILE=cmake_modules/Toolchain_switch.cmake \
#         -DCMAKE_BUILD_TYPE=Release
#
# This file is a thin wrapper around the toolchain that ships with devkitPro
# ($DEVKITPRO/cmake/Switch.cmake). That file sets up the cross compiler, the
# Horizon system name, the portlibs/switch + libnx find roots and the Switch
# pkg-config. On top of it we expose a `SWITCH` variable that the VCMI build
# branches test, mirroring how the Android NDK toolchain exposes `ANDROID`.

if(NOT DEFINED DEVKITPRO)
	if(DEFINED ENV{DEVKITPRO})
		set(DEVKITPRO $ENV{DEVKITPRO})
	else()
		message(FATAL_ERROR
			"DEVKITPRO is not set. Configure VCMI for Switch from within the "
			"devkitPro msys2 shell (where DEVKITPRO=/opt/devkitpro), or pass "
			"-DDEVKITPRO=<path> on the command line.")
	endif()
endif()

if(NOT EXISTS "${DEVKITPRO}/cmake/Switch.cmake")
	message(FATAL_ERROR "Could not find ${DEVKITPRO}/cmake/Switch.cmake - is the Switch toolchain (switch-cmake) installed?")
endif()

# Pull in the real devkitPro Switch toolchain.
include("${DEVKITPRO}/cmake/Switch.cmake")

# Flag this as a Switch build for the VCMI CMakeLists branches. Cached so it
# survives the toolchain re-inclusion that happens inside try_compile().
set(SWITCH 1 CACHE INTERNAL "Building VCMI for Nintendo Switch")
set(NINTENDO_SWITCH 1 CACHE INTERNAL "Building VCMI for Nintendo Switch")
