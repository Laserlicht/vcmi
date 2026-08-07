#!/usr/bin/env bash

set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

source $(dirname "${BASH_SOURCE[0]}")/linux_onnxruntime.sh

APT_CACHE="${APT_CACHE:-${RUNNER_TEMP:-/tmp}/apt-cache}"
sudo mkdir -p "$APT_CACHE"

sudo apt -yq -o Acquire::Retries=3 update
sudo apt -yq install eatmydata

sudo eatmydata apt -yq --no-install-recommends \
  -o Dir::Cache::archives="$APT_CACHE" \
  -o APT::Keep-Downloaded-Packages=true \
  -o Acquire::Retries=3 -o Dpkg::Use-Pty=0 \
  install \
  libboost-dev libboost-filesystem-dev libboost-date-time-dev \
  libboost-program-options-dev libboost-iostreams-dev \
  libasound2-dev libpulse-dev libpipewire-0.3-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev \
  libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
  libwayland-dev wayland-protocols libdecor-0-dev \
  libdrm-dev libgbm-dev libgl1-mesa-dev libegl1-mesa-dev \
  libdbus-1-dev libudev-dev libibus-1.0-dev \
  libpng-dev libfreetype-dev libharfbuzz-dev \
  qt6-base-dev qt6-base-dev-tools qt6-tools-dev qt6-tools-dev-tools \
  qt6-l10n-tools qt6-svg-dev \
  ninja-build zlib1g-dev libavformat-dev libswscale-dev libtbb-dev \
  libluajit-5.1-dev libminizip-dev libsqlite3-dev \
  libsquish-dev libfmt-dev gettext

sudo rm -f  "$APT_CACHE/lock" || true
sudo rm -rf "$APT_CACHE/partial" || true
sudo chown -R "$USER:$USER" "$APT_CACHE"

# No distribution ships a usable SDL3 stack yet, so build it from source into a
# cacheable prefix. The script skips components that are already present, and
# exports the prefix to the remaining steps of the job.
SDL3_PREFIX="${SDL3_PREFIX:-${RUNNER_TEMP:-/tmp}/sdl3}"
"$(dirname "${BASH_SOURCE[0]}")/linux_sdl3.sh" --prefix "$SDL3_PREFIX"
