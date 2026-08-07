#!/usr/bin/env bash

# Checks for SDL3 and its satellite libraries and builds/installs the missing
# ones from the upstream release tarballs. No distribution ships a new enough
# SDL3 stack yet, so building from source is the portable option for CI.
#
# Usage:
#   linux_sdl3.sh [--static|--shared] [--prefix DIR] [--check] [--force]
#
#   --check         only report what is present, install nothing (exit 1 if incomplete)
#   --force         rebuild everything even if a suitable version is installed
#   --install-deps  apt-install the system packages SDL3 builds against first
#
# Environment overrides:
#   SDL3_PREFIX     install prefix (default: /usr/local as root, else ~/.local)
#   SDL3_LINKAGE    "shared" (default) or "static"
#   SDL3_BUILD_DIR  scratch directory for sources (default: $TMPDIR/vcmi-sdl3)
#   SDL3_JOBS       parallel build jobs (default: nproc)
#   SDL3_VERSION SDL3_IMAGE_VERSION SDL3_MIXER_VERSION SDL3_TTF_VERSION

set -euo pipefail

SDL3_VERSION="${SDL3_VERSION:-3.4.14}"
SDL3_IMAGE_VERSION="${SDL3_IMAGE_VERSION:-3.4.4}"
SDL3_MIXER_VERSION="${SDL3_MIXER_VERSION:-3.2.4}"
SDL3_TTF_VERSION="${SDL3_TTF_VERSION:-3.2.2}"

# Minimum versions VCMI needs. SDL_mixer 3.2 is the first release with the
# reworked MIX_* API that the client is written against.
SDL3_MIN_VERSION="3.2.0"
SDL3_IMAGE_MIN_VERSION="3.2.0"
SDL3_MIXER_MIN_VERSION="3.2.0"
SDL3_TTF_MIN_VERSION="3.2.0"

SDL3_LINKAGE="${SDL3_LINKAGE:-shared}"
SDL3_JOBS="${SDL3_JOBS:-$(nproc 2>/dev/null || echo 4)}"
SDL3_BUILD_DIR="${SDL3_BUILD_DIR:-${TMPDIR:-/tmp}/vcmi-sdl3}"
MODE=install
INSTALL_DEPS=0

if [ -n "${SDL3_PREFIX:-}" ]; then
	PREFIX="$SDL3_PREFIX"
elif [ "$(id -u)" = 0 ]; then
	PREFIX=/usr/local
else
	PREFIX="$HOME/.local"
fi

while [ $# -gt 0 ]; do
	case "$1" in
		--static) SDL3_LINKAGE=static ;;
		--shared) SDL3_LINKAGE=shared ;;
		--prefix) PREFIX="$2"; shift ;;
		--prefix=*) PREFIX="${1#*=}" ;;
		--check) MODE=check ;;
		--force) MODE=force ;;
		--install-deps) INSTALL_DEPS=1 ;;
		-h|--help) sed -n '3,20p' "$0"; exit 0 ;;
		*) echo "unknown argument: $1" >&2; exit 1 ;;
	esac
	shift
done

export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig:${PKG_CONFIG_PATH:-}"

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

# True when version $1 is at least version $2.
version_ge() {
	[ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" = "$2" ]
}

have_component() {
	local found
	command -v pkg-config >/dev/null || return 1
	found="$(pkg-config --modversion "$1" 2>/dev/null)" || return 1
	version_ge "$found" "$2"
}

report_component() {
	if have_component "$1" "$2"; then
		printf '  %-12s %s (>= %s)\n' "$1" "$(pkg-config --modversion "$1")" "$2"
	else
		printf '  %-12s missing or older than %s\n' "$1" "$2"
		return 1
	fi
}

check_all() {
	local status=0
	report_component sdl3 "$SDL3_MIN_VERSION" || status=1
	report_component sdl3-image "$SDL3_IMAGE_MIN_VERSION" || status=1
	report_component sdl3-mixer "$SDL3_MIXER_MIN_VERSION" || status=1
	report_component sdl3-ttf "$SDL3_TTF_MIN_VERSION" || status=1
	return $status
}

# Makes the prefix discoverable by the remaining steps of a CI job. Harmless when
# installing into a system prefix, and a no-op outside of GitHub Actions.
export_environment() {
	[ -n "${GITHUB_ENV:-}" ] || return 0

	{
		echo "CMAKE_PREFIX_PATH=$PREFIX${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
		echo "PKG_CONFIG_PATH=$PKG_CONFIG_PATH"
		echo "LD_LIBRARY_PATH=$PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
	} >> "$GITHUB_ENV"
}

log "SDL3 stack in $PREFIX (linkage: $SDL3_LINKAGE)"

if [ "$MODE" = check ]; then
	check_all
	exit $?
fi

if [ "$MODE" != force ] && check_all; then
	log "Nothing to do"
	export_environment
	exit 0
fi

if [ "$INSTALL_DEPS" = 1 ]; then
	# https://wiki.libsdl.org/SDL3/README-linux#build-dependencies
	log "Installing SDL3 build dependencies"
	APT_SUDO=""
	[ "$(id -u)" = 0 ] || APT_SUDO="sudo"
	DEBIAN_FRONTEND=noninteractive $APT_SUDO apt-get -yq update
	DEBIAN_FRONTEND=noninteractive $APT_SUDO apt-get -yq --no-install-recommends install \
		build-essential cmake ninja-build pkg-config git \
		libasound2-dev libpulse-dev libaudio-dev libjack-dev libsndio-dev \
		libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev \
		libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
		libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev \
		libwayland-dev wayland-protocols libdecor-0-dev \
		libdbus-1-dev libudev-dev libibus-1.0-dev libpipewire-0.3-dev \
		libfreetype-dev libharfbuzz-dev libpng-dev
fi

for tool in cmake curl tar; do
	command -v "$tool" >/dev/null || { echo "required tool missing: $tool" >&2; exit 1; }
done

GENERATOR=()
command -v ninja >/dev/null && GENERATOR=(-G Ninja)

# Install without sudo whenever the prefix is already writable, so the script
# works both in a root CI container and in an unprivileged home directory.
mkdir -p "$PREFIX" 2>/dev/null || true
if [ -w "$PREFIX" ]; then
	SUDO=""
elif command -v sudo >/dev/null; then
	SUDO="sudo"
	$SUDO mkdir -p "$PREFIX"
else
	echo "cannot write to $PREFIX and sudo is unavailable" >&2
	exit 1
fi

if [ "$SDL3_LINKAGE" = static ]; then
	SHARED=OFF
	STATIC=ON
else
	SHARED=ON
	STATIC=OFF
fi

mkdir -p "$SDL3_BUILD_DIR"

# Downloads and unpacks a release tarball, echoing the source directory.
fetch() {
	local repo="$1" name="$2" version="$3"
	local archive="$SDL3_BUILD_DIR/$name-$version.tar.gz"
	local srcdir="$SDL3_BUILD_DIR/$name-$version"

	if [ ! -d "$srcdir" ]; then
		if [ ! -s "$archive" ]; then
			log "Downloading $name $version" >&2
			curl -fsSL --retry 3 -o "$archive.part" \
				"https://github.com/libsdl-org/$repo/releases/download/release-$version/$name-$version.tar.gz"
			mv "$archive.part" "$archive"
		fi
		tar -xzf "$archive" -C "$SDL3_BUILD_DIR"
	fi
	echo "$srcdir"
}

build() {
	local srcdir="$1"; shift
	local builddir="$srcdir/build-$SDL3_LINKAGE"

	cmake -S "$srcdir" -B "$builddir" "${GENERATOR[@]}" \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX="$PREFIX" \
		-DCMAKE_PREFIX_PATH="$PREFIX" \
		-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
		-DBUILD_SHARED_LIBS="$SHARED" \
		"$@"
	cmake --build "$builddir" --parallel "$SDL3_JOBS"
	$SUDO cmake --install "$builddir"
}

log "Building into $PREFIX with $SDL3_JOBS jobs"

if [ "$MODE" = force ] || ! have_component sdl3 "$SDL3_MIN_VERSION"; then
	# SDL treats a requested-but-unavailable backend as a hard configure error,
	# so turn off the optional ones whose headers are not installed rather than
	# failing outright. Run with --install-deps for a full-featured build.
	SDL_OPTIONS=()
	[ -f /usr/include/X11/extensions/XTest.h ] || SDL_OPTIONS+=(-DSDL_X11_XTEST=OFF)
	[ -f /usr/include/X11/extensions/scrnsaver.h ] || SDL_OPTIONS+=(-DSDL_X11_XSCRNSAVER=OFF)
	pkg-config --exists wayland-client 2>/dev/null || SDL_OPTIONS+=(-DSDL_WAYLAND=OFF)
	pkg-config --exists libpipewire-0.3 2>/dev/null || SDL_OPTIONS+=(-DSDL_PIPEWIRE=OFF)
	pkg-config --exists libpulse 2>/dev/null || SDL_OPTIONS+=(-DSDL_PULSEAUDIO=OFF)
	pkg-config --exists alsa 2>/dev/null || SDL_OPTIONS+=(-DSDL_ALSA=OFF)

	log "SDL $SDL3_VERSION ${SDL_OPTIONS[*]-}"
	build "$(fetch SDL SDL3 "$SDL3_VERSION")" \
		-DSDL_SHARED="$SHARED" \
		-DSDL_STATIC="$STATIC" \
		-DSDL_TESTS=OFF \
		-DSDL_EXAMPLES=OFF \
		-DSDL_INSTALL_TESTS=OFF \
		"${SDL_OPTIONS[@]}"
fi

# The satellite libraries use their built-in decoders where that is lossless,
# but PNG must go through libpng: its palette+tRNS images get expanded to RGBA,
# while the built-in stb decoder keeps them paletted. VCMI relies on the former,
# so the stb path would break transparency of paletted PNG assets.
if [ "$MODE" = force ] || ! have_component sdl3-image "$SDL3_IMAGE_MIN_VERSION"; then
	if pkg-config --exists libpng 2>/dev/null; then
		IMAGE_VENDORED=OFF
	else
		IMAGE_VENDORED=ON
	fi

	log "SDL_image $SDL3_IMAGE_VERSION (vendored libpng: $IMAGE_VENDORED)"
	imagedir="$(fetch SDL_image SDL3_image "$SDL3_IMAGE_VERSION")"
	if [ "$IMAGE_VENDORED" = ON ] && [ ! -d "$imagedir/external/libpng" ]; then
		command -v git >/dev/null || { echo "git needed to fetch vendored libpng" >&2; exit 1; }
		(cd "$imagedir" && ./external/download.sh)
	fi
	build "$imagedir" \
		-DSDLIMAGE_VENDORED="$IMAGE_VENDORED" \
		-DSDLIMAGE_DEPS_SHARED=OFF \
		-DSDLIMAGE_SAMPLES=OFF \
		-DSDLIMAGE_TESTS=OFF \
		-DSDLIMAGE_PNG=ON \
		-DSDLIMAGE_PNG_LIBPNG=ON \
		-DSDLIMAGE_AVIF=OFF \
		-DSDLIMAGE_JXL=OFF \
		-DSDLIMAGE_TIF=OFF \
		-DSDLIMAGE_WEBP=OFF
fi

if [ "$MODE" = force ] || ! have_component sdl3-mixer "$SDL3_MIXER_MIN_VERSION"; then
	log "SDL_mixer $SDL3_MIXER_VERSION"
	build "$(fetch SDL_mixer SDL3_mixer "$SDL3_MIXER_VERSION")" \
		-DSDLMIXER_VENDORED=OFF \
		-DSDLMIXER_DEPS_SHARED=OFF \
		-DSDLMIXER_EXAMPLES=OFF \
		-DSDLMIXER_TESTS=OFF \
		-DSDLMIXER_WAVE=ON \
		-DSDLMIXER_MP3_DRMP3=ON \
		-DSDLMIXER_MP3_MPG123=OFF \
		-DSDLMIXER_VORBIS_STB=ON \
		-DSDLMIXER_VORBIS_VORBISFILE=OFF \
		-DSDLMIXER_VORBIS_TREMOR=OFF \
		-DSDLMIXER_FLAC_LIBFLAC=OFF \
		-DSDLMIXER_OPUS=OFF \
		-DSDLMIXER_MOD=OFF \
		-DSDLMIXER_GME=OFF \
		-DSDLMIXER_WAVPACK=OFF \
		-DSDLMIXER_MIDI_FLUIDSYNTH=OFF
fi

if [ "$MODE" = force ] || ! have_component sdl3-ttf "$SDL3_TTF_MIN_VERSION"; then
	# FreeType has no built-in substitute; fall back to the vendored copy when
	# the system does not provide it.
	if pkg-config --exists freetype2 2>/dev/null; then
		TTF_VENDORED=OFF
	else
		TTF_VENDORED=ON
	fi
	log "SDL_ttf $SDL3_TTF_VERSION (vendored freetype: $TTF_VENDORED)"
	ttfdir="$(fetch SDL_ttf SDL3_ttf "$SDL3_TTF_VERSION")"
	if [ "$TTF_VENDORED" = ON ] && [ ! -d "$ttfdir/external/freetype" ]; then
		command -v git >/dev/null || { echo "git needed to fetch vendored freetype" >&2; exit 1; }
		(cd "$ttfdir" && ./external/download.sh)
	fi
	build "$ttfdir" \
		-DSDLTTF_VENDORED="$TTF_VENDORED" \
		-DSDLTTF_SAMPLES=OFF \
		-DSDLTTF_TESTS=OFF \
		-DSDLTTF_HARFBUZZ=ON \
		-DSDLTTF_PLUTOSVG=OFF
fi

if [ -z "$SUDO" ] && [ "$(id -u)" = 0 ] && command -v ldconfig >/dev/null; then
	ldconfig || true
fi

export_environment

log "Result:"
check_all
