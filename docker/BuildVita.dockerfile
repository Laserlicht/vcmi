FROM ubuntu:noble
WORKDIR /usr/local/app

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential cmake ninja-build git curl ca-certificates patch \
    python3 pkg-config wget tar bzip2 xz-utils sudo

# VitaSDK: install_vitasdk fetches a prebuilt nightly toolchain (no from-source GCC
# build needed - unlike devkitPro's Switch toolchain, this step is fast).
ENV VITASDK=/usr/local/vitasdk
ENV PATH=${VITASDK}/bin:${PATH}
RUN git clone --depth 1 https://github.com/vitasdk/vdpm.git /usr/local/vdpm \
    && cd /usr/local/vdpm \
    && ./bootstrap-vitasdk.sh

# vdpm packages needed to satisfy VCMI's dependency list (see cmake_modules/Find*.cmake
# and root CMakeLists.txt's BOOST_COMPONENTS / ENABLE_VIDEO): the SDL2 stack + their
# transitive codec deps, ffmpeg (ENABLE_VIDEO), boost (filesystem + program_options),
# minizip, and the Vita GPU/system stub libs SDL2 itself links against.
#
# vdpm does not auto-install a package's own dependencies (unlike apt/vdpm's own
# install-all.sh, which just lists everything flat) - openssl/lame/xz/zstd/libxmp
# below were only discovered as needed from actual "undefined reference" link errors
# against the vdpm ffmpeg/sdl2_mixer builds (both built with many optional codecs
# enabled), not from reading either package's declared dependencies anywhere.
RUN /usr/local/vdpm/vdpm \
    zlib bzip2 freetype harfbuzz libpng libjpeg-turbo libwebp \
    libogg libvorbis opus opusfile mpg123 libmikmod libmodplug \
    minizip boost ffmpeg openssl lame xz zstd libxmp libxmp-lite \
    sdl2 sdl2_image sdl2_mixer sdl2_ttf libvita2d

# Not available via vdpm - cross-built from source instead. See each script for why.
# (build-iconv.sh compiles vita/compat/iconv_shim.c, hence copying the whole vita/ tree.)
COPY vita /usr/local/vita-deps/vita
RUN chmod +x /usr/local/vita-deps/vita/deps/*.sh \
    && /usr/local/vita-deps/vita/deps/build-lua.sh \
    && /usr/local/vita-deps/vita/deps/build-libsquish.sh \
    && /usr/local/vita-deps/vita/deps/build-iconv.sh

CMD ["sh", "-c", " \
    cd /vcmi ; \
    cmake --preset vita-release ; \
    cmake --build --preset vita-release \
"]

# Build with:
#      docker build -f docker/BuildVita.dockerfile -t vcmi-vita-build .
#      docker run -it --rm -v $PWD/:/vcmi vcmi-vita-build
