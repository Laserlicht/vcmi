# VCMI CMake toolchain for PS Vita (VitaSDK, arm-vita-eabi, armv7-a).
#
# Usage:
#   cmake --preset vita-release
#   cmake --build --preset vita-release
#
# This is a thin wrapper around the toolchain VitaSDK ships at
# $VITASDK/share/vita.toolchain.cmake. That file configures the cross compiler, sets
# CMAKE_SYSTEM_NAME to "Generic" (there is no CMake-recognized OS name for Vita) and
# CMAKE_FIND_ROOT_PATH against the arm-vita-eabi sysroot + vdpm-installed portlibs, and
# defines a plain (non-cached) `VITA` variable that survives into the rest of the
# configure - which is what the VCMI CMakeLists branches test, mirroring how the
# Android NDK toolchain exposes `ANDROID`.

if(NOT DEFINED VITASDK)
	if(DEFINED ENV{VITASDK})
		set(VITASDK $ENV{VITASDK})
	else()
		message(FATAL_ERROR
			"VITASDK is not set. Install VitaSDK (see https://vitasdk.org/) and either "
			"export VITASDK=/path/to/vitasdk, or pass -DVITASDK=<path> on the command line.")
	endif()
endif()

if(NOT EXISTS "${VITASDK}/share/vita.toolchain.cmake")
	message(FATAL_ERROR "Could not find ${VITASDK}/share/vita.toolchain.cmake - is VitaSDK installed at VITASDK=${VITASDK}?")
endif()

include("${VITASDK}/share/vita.toolchain.cmake")

# The 32-bit ARM EABI (AAPCS) specifies "short enums" by default: an enum's underlying
# type is the smallest integer type that fits its enumerators, not always `int`. VCMI's
# entity ID types (lib/constants/IdentifierBase.h) static_assert that their underlying
# type is exactly int32_t, which this default silently violates on Vita's armv7-a
# (AArch64 targets like Switch, and Android's ARM Linux ABI, don't have this rule, which
# is why this only surfaces here). Force the conventional `int` sizing VCMI assumes
# throughout, applied at the toolchain level so every translation unit agrees.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-short-enums")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-short-enums")

# vita-elf-create needs to inject its SCE module_info metadata into the gap right after
# the text (RE) segment's end, before the data (RW) segment begins. VCMI's text segment
# (~21MB - a full game engine, not a small homebrew app) leaves too little of that gap
# for this particular build's ~61KB of metadata ("Cannot allocate N bytes for SCE data
# ...; segment 1 overlaps"). `-Wl,-Tdata=<addr>` does NOT fix this: vitasdk's default
# script positions the data segment via SEGMENT_START("ldata-segment", .), which doesn't
# consult that option. vita/link/vita-vcmi.ld is that same default script (dumped via
# `arm-vita-eabi-ld --verbose`) with one added line forcing extra padding there instead.
#
# CMAKE_TOOLCHAIN_FILE is included more than once per configure (e.g. once during the
# initial CMAKE_C_COMPILER_WORKS/CMAKE_CXX_COMPILER_WORKS try_compile checks, and again
# for the main project) - CMAKE_EXE_LINKER_FLAGS is a CACHE variable, so appending
# unconditionally here duplicates the -Wl,-T flag on the second pass, which ld then
# rejects ("linker script file ... appears multiple times"). Guard against reprocessing.
if(NOT VCMI_VITA_LINKER_SCRIPT_APPLIED)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-T,${CMAKE_SOURCE_DIR}/vita/link/vita-vcmi.ld")
	set(VCMI_VITA_LINKER_SCRIPT_APPLIED TRUE CACHE INTERNAL "")
endif()
