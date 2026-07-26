# VCMI CMake toolchain for Switch (devkitA64 + libnx): thin wrapper around
# devkitPro's own $DEVKITPRO/cmake/Switch.cmake that additionally exposes a
# SWITCH variable for the VCMI build branches (like ANDROID/IOS).

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
set(SWITCH 1 CACHE INTERNAL "Building VCMI for Switch")

# NOTE: devkitPro's platform module (re)sets CMAKE_*_FLAGS_INIT after this file
# runs, clobbering flag adjustments made here - Switch-specific flag handling
# therefore lives in the top-level CMakeLists.txt instead.
