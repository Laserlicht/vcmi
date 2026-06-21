# VCMI — Nintendo Switch port (devkitA64 + libnx)

This directory contains the Nintendo Switch (homebrew) support for VCMI: a CMake
toolchain, a header-only sequential TBB shim, and the NRO packaging glue. The Switch
is treated as a **mobile-class target** (like iOS/Android): a single statically-linked
executable that runs the game server in-process (no `fork`/`exec`), with no Qt
launcher or map editor.

## Architecture decisions

| Concern | Decision |
|---|---|
| Platform macro | `VCMI_SWITCH` (defined in `Global.h` from the toolchain's `__SWITCH__`) which also implies `VCMI_UNIX` and `VCMI_MOBILE`. |
| Server process | None. `VCMI_MOBILE` selects the in-process `ServerThreadRunner` and compiles out `boost::process`. |
| Linking | One static `vcmiclient.elf` → `vcmiclient.nro` (`ENABLE_STATIC_LIBS`, `ENABLE_SINGLE_APP_BUILD`). |
| Threading library (TBB) | Real oneTBB is unavailable; replaced by an in-tree **header-only sequential shim** (`switch/tbb-shim/`) wired in as the `TBB::tbb` target. Everything runs single-threaded. |
| Scripting (Lua) | Plain **Lua 5.1** (LuaJIT is impossible — no JIT / W^X on Switch). 5.1 keeps VCMI's `lua_setfenv` path active, so no engine source changes. |
| Filesystem | `VCMIDirsSwitch`: writable data on the SD card (`sdmc:/switch/vcmi`), bundled engine data in the NRO's read-only `romfs:/`. |
| Video (ffmpeg) | `ENABLE_VIDEO=ON` — ffmpeg 7.1 from portlibs, statically linked. Uses VCMI's shared mobile video path (no Switch-specific video code). Requires the H3 `.vid` archives in the data folder. |
| ML battle AI (MMAI) | `ENABLE_MMAI=OFF` (needs onnxruntime, no Switch build). BattleAI / StupidAI / Nullkiller2 remain. |

## Source changes made to the engine

- `Global.h` — new `__SWITCH__` arm → `VCMI_UNIX` + `VCMI_SWITCH`; folded into `VCMI_MOBILE`.
- `lib/CThreadHelper.cpp` — excluded Switch from `<sys/prctl.h>`; added a no-op thread-naming branch (newlib has no `prctl`).
- `lib/VCMIDirs.cpp` — added `VCMIDirsSwitch` (sdmc/romfs paths) + dispatcher entry.
- `clientapp/EntryPoint.cpp` — libnx init (`romfsInit` + `socketInitializeDefault`) at startup; disabled the stdin console on Switch.
- `CMakeLists.txt`, `clientapp/CMakeLists.txt` — Switch platform gating, dependency wiring and NRO packaging.
- `cmake_modules/Toolchain_switch.cmake`, `CMakePresets.json` (`switch-release`) — new.

## Building

Everything must run inside the **devkitPro msys2 login shell** (so `DEVKITPRO=/opt/devkitpro`).

### 1. Toolchain & system packages (one-time)

```sh
pacman -S --needed switch-dev switch-sdl2 switch-sdl2_image switch-sdl2_mixer \
                   switch-sdl2_ttf switch-ffmpeg switch-zlib switch-bzip2 \
                   switch-pkg-config cmake ninja
```

### 2. From-source dependencies (one-time)

These are not available as devkitPro packages and are cross-compiled into
`$DEVKITPRO/portlibs/switch` by the scripts in `../../switch/deps/`:

```sh
bash switch/deps/build-boost.sh        # Boost filesystem/program_options/date_time (+headers)
bash switch/deps/build-iconv.sh        # GNU libiconv (newlib has the header but no symbols)
bash switch/deps/build-small-deps.sh   # Lua 5.1 + libsquish
```

(The TBB shim is header-only and lives in-tree, so nothing to build for it.)

### 3. Configure & build VCMI

```sh
cmake --preset switch-release          # or use switch/build-vcmi-switch.sh
cmake --build out/build/switch-release -j4
```

The result is `vcmiclient.nro` next to the linked `vcmiclient.elf`.

## Installing on the Switch

1. Copy `vcmiclient.nro` to `sdmc:/switch/vcmi/vcmiclient.nro` (or anywhere; launch via hbmenu).
2. Copy your **legally-owned Heroes III** data onto the SD card:
   ```
   sdmc:/switch/vcmi/Data/   (H3 .lod archives etc.)
   sdmc:/switch/vcmi/Maps/
   sdmc:/switch/vcmi/Mp3/
   ```
   Shadow of Death or Complete is required (Restoration of Erathia alone is not supported).
3. Saves and config are written under `sdmc:/switch/vcmi/`. The cache and the
   log (`VCMI_Client_log.txt`) go to `sdmc:/switch/vcmi-cache/` — deliberately a
   *sibling* of the data folder, because the data folder is scanned recursively at
   startup and libnx's FAT layer returns EIO when stat-ing a file that is open for
   writing (the live log), which would otherwise abort the scan.

## Input

Joy-Con / Pro Controller and the touchscreen are handled by SDL2's Switch backend
through VCMI's existing mobile input paths (`InputSourceGameController`,
`InputSourceTouch`). The software cursor is driven by the stick/touch automatically.

## Build status

The full engine (lib + client + server + all AIs + Lua), statically linked with Boost,
SDL2/SDL2_image/mixer/ttf, libiconv, libsquish and the TBB shim, **compiles and links**
into `bin/vcmiclient.nro` (AArch64 PIE, NACP "VCMI" / "VCMI Team" / 1.8.0, romfs with
config/Mods/scripts). **Boots on real hardware into the main menu and into gameplay** -
data/mod loading, the in-process server thread, software cursor, controller input, map
loading and codepage/text conversion are all confirmed working.

### libnx compatibility shims (`switch/compat/`)
The build relies on a small set of force-included compatibility pieces because newlib/libnx
is missing parts of the POSIX surface boost::asio expects:
- `sys/uio.h`, `sys/un.h`, `net/if.h`, `netinet/in.h` header shims
- `switch_compat.h` (force-included): `ESHUTDOWN`, `SA_RESTART`/`SA_NOCLDWAIT`,
  `BOOST_ASIO_DISABLE_SERIAL_PORT`, `BOOST_STACKTRACE_USE_NOOP`
- `switch_net_stubs.c`: `if_nametoindex`/`if_indextoname` (failing stubs), `pthread_sigmask`
  (no-op), `pause` (ENOSYS), and **`pipe()` emulated via a loopback TCP socket pair** (libnx
  has no kernel pipe; asio builds a self-pipe interrupter eagerly when an io_context is created).

## Known limitations / TODO

Essential for a polished port:
- **Docked vs handheld** — window is sized to display bounds at startup; handle the dock/undock resolution change (720p↔1080p) at runtime.
- **Suspend/resume** — map the home-menu applet pause to `onAppPaused`.

Nice to have:
- **Performance** — the TBB shim is sequential, so adventure AI (Nullkiller2), RMG and xBRZ upscaling are single-threaded; a pthread-backed `parallel_for` would speed up large maps.
- **Memory** — request full-RAM application mode in the NACP for big maps/mods if needed.
- `en_US.UTF-8` locale isn't available in newlib (minor: number formatting falls back to C).

Resolved runtime issues (post-bring-up):
- `createWindow()` had no Switch branch → garbage window pointer crashed `SDL_CreateRenderer` (fixed).
- cache/log lived inside a scanned data dir → `stat` on the open log aborted the FS scan (fixed: cache moved to `sdmc:/switch/vcmi-cache`).
- `boost::filesystem::canonical()` fails on libnx device paths → all maps reported invalid (fixed: skip canonical on Switch in the loaders' `getFullFileURI`).
- libiconv rejected undefined CP1252 bytes → text conversion failures (fixed: `//TRANSLIT//IGNORE` on Switch).

Video (built; needs H3 `.vid` data to verify):
- **ENABLE_VIDEO=ON** — ffmpeg 7.1 from portlibs is statically linked. The full libs are present (745 demuxers / 1024 decoders, incl. Smacker/Bink). The static dep chain (`dav1d`, `bz2`, `z`) plus the ffmpeg components are wrapped in `--start-group` in `client/CMakeLists.txt`.
- **No Switch-specific video code.** Playback uses VCMI's shared software **surface** path (`CVideoPlayer::open` → `prepareOutput(..., false)`), byte-for-byte the same as the iOS/Android backends. The intent is: if it works on those mobile targets, it works here.
- **Not yet verified on hardware** — the H3 data set used for testing shipped **no `.vid` archives** (only `.lod`/`.snd`). With no video files, `CArchiveLoader` skips the (absent) `.vid`, `findVideoData()` returns null, `open()` fails, and every clip renders black while the background-music theme + campaign prolog text still play. This presents *exactly* like a render bug but isn't one. To enable movies, copy the H3 video archives — `Heroes3.vid`, `VIDEO.VID`, `H3ab_ahd.vid` — into `sdmc:/switch/vcmi/Data/`.

Input (implemented):
- **Software keyboard** — `CTextInput::clickPressed` opens the native libnx full-screen keyboard (`swkbd`), seeded with the field's current text, and falls back to SDL's inline keyboard if the keyboard applet is unavailable (`client/widgets/CTextInput.cpp`).
- **Controller layout** — `SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS=0` (set in `ScreenHandler`) makes the physical A (right) button accept and B (bottom) cancel, matching Nintendo convention; the rest of the default joystick layout (L/R, ZL/ZR, +/-, sticks, d-pad) already maps cleanly.

Startup performance (implemented):
- **Logging** (`lib/logging/CLogger.cpp`, `CLogFileTarget::write`) — the log lives on the SD card (FAT). Two Switch-only changes: (1) records below `INFO` are dropped (the content-init phase emits ~10k `TRACE` lines; skipping them before formatting/IO removed the bulk of startup cost and keeps the release log small); (2) remaining records use `'\n'` instead of `std::endl`, with an explicit `flush()` only on `WARN`/`ERROR`, so normal lines are buffered instead of fsync-per-line while crash-relevant output is still persisted. This roughly halved boot time on its own.
- **CPU boost** (`clientapp/EntryPoint.cpp`) — `appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad)` raises the CPU clock (1020 → ~1785 MHz) for the CPU-bound, single-threaded content init, dropped back to `Normal` exactly when the main menu becomes the active interface (`makeActiveInterface`). NOTE: `appletSetCpuBoostMode` only takes effect when the NRO runs in **application / title-takeover** mode; launched as a plain library applet over the album, the request is silently ignored (harmless, but no boost).
