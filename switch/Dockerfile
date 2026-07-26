# syntax=docker/dockerfile:1
# Cross-compiles VCMI for Switch (devkitA64 + libnx) end-to-end. From the repo root:
#   docker build -f switch/Dockerfile --target artifact --output out/switch .
# Result: out/switch/vcmiclient.nro

# ---- deps: devkitPro packages + from-source cross-compiled libs ----
FROM devkitpro/devkita64:latest AS switch-deps

# pkg.devkitpro.org's Cloudflare WAF 403s pacman's default downloader; use wget
# with a User-Agent instead (devkitPro/pacman#63)
RUN sed -i '/^\[options\]/a XferCommand = /usr/bin/wget -U "dkp-pacman" --passive-ftp -O %o %u' \
        /opt/devkitpro/pacman/etc/pacman.conf

# drop the baked-in sync db: its stale signature makes pacman reject a fresh -Sy
RUN rm -f /opt/devkitpro/pacman/var/lib/pacman/sync/*

RUN dkp-pacman -Sy --noconfirm && \
    dkp-pacman -S --needed --noconfirm \
        switch-dev switch-sdl2 switch-sdl2_image switch-sdl2_mixer \
        switch-sdl2_ttf switch-ffmpeg switch-zlib switch-bzip2 \
        switch-pkg-config switch-mesa switch-glm && \
    dkp-pacman -Scc --noconfirm

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake ninja-build curl ca-certificates patch build-essential ccache && \
    rm -rf /var/lib/apt/lists/*

ENV DEVKITPRO=/opt/devkitpro
ENV CCACHE_DIR=/ccache
WORKDIR /src

# copy only the deps scripts so this layer stays cached across source changes
COPY switch/deps /src/switch/deps
RUN bash /src/switch/deps/build-boost.sh && \
    bash /src/switch/deps/build-iconv.sh && \
    bash /src/switch/deps/build-small-deps.sh

# ---- build: full VCMI source, cross-compiled client ----
FROM switch-deps AS build
COPY . /src
RUN --mount=type=cache,target=/ccache,sharing=locked \
    ccache --set-config=compiler_check=content && \
    ccache --set-config=hash_dir=false && \
    ccache --set-config=sloppiness=pch_defines,time_macros && \
    cmake --preset switch-release \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache && \
    cmake --build --preset switch-release -j"$(nproc)" && \
    ccache -s

FROM build AS collect
RUN mkdir -p /out && \
    find /src/out/build/switch-release -name 'vcmiclient.nro' -exec cp {} /out/vcmiclient.nro \; && \
    test -f /out/vcmiclient.nro

# just the .nro, for docker build --output
FROM scratch AS artifact
COPY --from=collect /out/vcmiclient.nro /vcmiclient.nro
