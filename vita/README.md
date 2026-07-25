# VCMI — PS Vita port (VitaSDK)

This directory contains the PS Vita (homebrew) support for VCMI: a CMake toolchain
wrapper, a header-only sequential TBB shim, source-build scripts for the three
dependencies vdpm doesn't package, a custom linker script, and the .vpk packaging
glue. Like the Android/iOS
ports (and the unmerged Nintendo Switch port this one used as an architecture
reference — `develop...NaGaa95:vcmi_nx:switch`, on GitHub), the Vita is treated as a
**mobile-class target**: a single statically-linked executable that runs the game
server in-process (no `fork`/`exec`), with no Qt launcher or map editor.

## Status: builds and packages cleanly; one confirmed link bug fixed; one open Vita3K-only gap

**The Docker build cross-compiles VCMI end to end and produces a structurally valid
`vcmiclient.vpk`** (confirmed: contains `eboot.bin`, `sce_sys/param.sfo`, and the
bundled `config/`/`Mods/`/`scripts/` trees - 663 files total, ~19MB).

A later session gained access to the Vita3K emulator, real Heroes III data, and
(still later) the user's real PS Vita hardware. Both crashed identically on launch
(Vita3K: `std::bad_alloc` → an infinite "Invalid read of uint32_t" cascade → native
SIGSEGV; real hardware: system error `C1-2609-7`). That crash has now been
**root-caused and fixed at the link level** - see "Runtime crash investigation"
below - but a second, narrower issue remains that only reproduces on Vita3K and
looks like a gap in that emulator's own kernel emulation, not a VCMI bug. **The
fixed build has not yet been re-tested on real hardware** - that's the next
concrete step.

## Architecture decisions

| Concern | Decision |
|---|---|
| Platform macro | `VCMI_VITA` (defined in `Global.h` from VitaSDK's `__vita__`), which also implies `VCMI_UNIX` and `VCMI_MOBILE`. |
| Server process | None. `VCMI_MOBILE` selects the in-process `ServerThreadRunner` and compiles out `boost::process`. |
| Linking | One static `vcmiclient` ELF → `eboot.bin` → `vcmiclient.vpk` (`ENABLE_STATIC_LIBS`, `ENABLE_SINGLE_APP_BUILD`). |
| Threading library (TBB) | Real oneTBB is unavailable; replaced by an in-tree **header-only sequential/mutex-guarded shim** (`vita/tbb-shim/`), scoped to exactly the 11 TBB headers VCMI actually includes (verified by grep, not guessed from upstream TBB's surface). Tasking primitives run inline on the calling thread; the `concurrent_*` containers are still internally mutex-guarded because the Nullkiller2 AI turn genuinely runs on a real `std::thread` in parallel with the main thread (see below) - the difference from Switch's shim is that this one was written from scratch against VCMI's actual call sites, not adapted from another codebase. |
| Scripting (Lua) | Plain **Lua 5.1**, cross-built from lua.org source by `vita/deps/build-lua.sh` - vdpm only packages **LuaJIT**, and LuaJIT's JIT compiler needs RWX executable memory pages, which unsigned Vita homebrew cannot allocate. That's a runtime failure mode (link succeeds, JIT compile of the first hot function crashes or is silently rejected), not a compile error, so it can't be caught without hardware - building plain Lua instead avoids the risk rather than gambling on it. VCMI already has a non-LuaJIT fallback path (`CMakeLists.txt`'s `find_package(Lua)` branch), so no engine source changes were needed. |
| Filesystem | `VCMIDirsVita`: writable data on the memory card (`ux0:data/vcmi`), bundled engine data in the `.vpk`'s read-only `app0:/` mount. |
| Video (ffmpeg) | `ENABLE_VIDEO=ON` - ffmpeg from vdpm, statically linked. Uses VCMI's shared mobile video path (no Vita-specific video code), same as iOS/Android/Switch. Requires the H3 `.vid` archives on the memory card to actually play anything. |
| DXT texture compression (libsquish) | Not a vdpm package (VCMI normally gets it from its conan dependency bundle, which has no Vita profile); cross-built from source by `vita/deps/build-libsquish.sh`. |
| `iconv` | vitasdk's newlib ships the `<iconv.h>` header but no implementation. GNU libiconv was tried first and turned into a multi-day rabbit hole of cross-compilation quirks specific to this newlib target (see `vita/deps/build-iconv.sh`'s comment for the full story); replaced with a small purpose-built shim (`vita/compat/iconv_shim.c`) covering exactly the UTF-8 ↔ Windows-125x conversions VCMI actually does, with byte tables pulled from Unicode.org's canonical mapping files (not hand-transcribed). CP932/CP949 (Japanese/Korean) are **not** supported - loading those language packs will fail cleanly (logged error), not crash. |
| Position-independent code | Off for Vita (`CMAKE_POSITION_INDEPENDENT_CODE OFF`), unlike every other platform. `vita-elf-create` (converts the linked ELF to Sony's format) only understands a fixed, small set of ARM relocation types and rejects the GOT-relative ones `-fPIC` generates (`R_ARM_BASE_PREL`, "Invalid relocation type 25!"). Vita homebrew is a single static executable with nothing else to be position-independent *for*. |
| ARM enum ABI | `-fno-short-enums` forced at the toolchain level. The 32-bit ARM EABI defaults to "short enums" (smallest type that fits the enumerators); VCMI's entity ID types static_assert their underlying type is exactly `int32_t`. AArch64 (Switch) and Android's ARM/Linux ABI don't have this rule, which is why it only surfaces here. |
| Linker script | `vita/link/vita-vcmi.ld` - vitasdk's own default script (dumped via `arm-vita-eabi-ld --verbose`) with one added line forcing extra padding between the text and data segments. `vita-elf-create` injects its SCE metadata into that gap, and VCMI's ~21MB text segment (a full game engine, not typical homebrew) left too little of it by default. |
| ML battle AI (MMAI) | `ENABLE_MMAI=OFF` (needs onnxruntime, no Vita build). BattleAI / StupidAI / Nullkiller2 remain. |

## Source changes made to the engine

- `Global.h` - new `__vita__` arm → `VCMI_UNIX` + `VCMI_VITA`; folded into `VCMI_MOBILE`.
- `lib/CThreadHelper.cpp` - excluded Vita from the `<sys/prctl.h>` branch; added a no-op thread-naming branch (newlib has no `prctl`).
- `lib/VCMIDirs.cpp` - added `VCMIDirsVita` (ux0:/app0: paths) + dispatcher entry.
- `lib/AsyncRunner.h` + `AI/Nullkiller2/AIGateway.cpp` - real-`std::thread` opt-in for the AI turn (`AsyncRunner(bool useRealThreads)`), since the TBB shim's tasking primitives are sequential and running the AI turn inline would deadlock the client (it blocks on the network thread while it runs).
- `clientapp/EntryPoint.cpp` - VitaSDK `sceNet`/`sceNetCtl` init at startup, disabled the stdin console on Vita, `main()` used directly instead of `SDL_main` (same reasoning as Switch: SDL2 doesn't redefine `main→SDL_main` for `__vita__`, and vitasdk's crt0 calls `main()` directly).
- `client/renderSDL/ScreenHandler.cpp` - fixed 960x544 fullscreen window branch in `createWindow()` (this function previously fell through with no return for any platform outside Windows/iOS/Android/desktop - would have been undefined behavior on Vita); `SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS=0` so the physical X (bottom) button accepts and O (right) cancels, matching PlayStation convention.
- `lib/filesystem/{CArchiveLoader,CFilesystemLoader,CZipLoader}.cpp` - skip `boost::filesystem::canonical()` on Vita device paths (`ux0:`, `app0:`), which aren't real POSIX paths and make `canonical()` fail.
- `lib/logging/CLogger.cpp` - `CLogConsoleTarget::write()` needed an explicit Vita branch: it previously assumed any non-Android/non-iOS platform has a `CConsoleHandler` member, which doesn't exist under `VCMI_MOBILE` (a real compile error found by the build, not a hypothetical).
- `luascript/api/callback/IBattleInfoCallback.cpp` - `LUA_OK` (introduced in Lua 5.2) polyfilled locally for the one call site that uses it, since plain Lua 5.1 (see below) doesn't define it.
- `CMakeLists.txt`, `client/CMakeLists.txt`, `clientapp/CMakeLists.txt` - Vita platform gating, dependency wiring, static-link groups, and `.vpk` packaging.
- `cmake_modules/Toolchain_vita.cmake`, `CMakePresets.json` (`vita-release`) - new.

Deliberately **not** changed, unlike the Switch port: `client/gui/CursorHandler.cpp`
(Vita already gets the software cursor for free through the existing
`VCMI_MOBILE`-gated branch - no Vita-specific branch needed) and the Lua version
itself (VCMI's existing plain-Lua fallback already does the right thing once
`luajit::luajit` isn't a CMake target).

## Building

Everything runs inside the provided Docker image - no local VitaSDK install needed.

```sh
docker build -f docker/BuildVita.dockerfile -t vcmi-vita-build .
docker run -it --rm -v $PWD/:/vcmi vcmi-vita-build
```

This installs VitaSDK (a prebuilt nightly toolchain via `vdpm`'s
`bootstrap-vitasdk.sh` - fast, unlike devkitPro's from-source GCC build for Switch),
the vdpm packages VCMI needs (boost, the SDL2 stack, ffmpeg, zlib/bzip2/freetype and
their codec dependencies, minizip, openssl/lame/xz/zstd/libxmp), cross-builds Lua,
libsquish, and the iconv shim from source (see `vita/deps/`), then runs:

```sh
cmake --preset vita-release
cmake --build --preset vita-release
```

which configures with `cmake_modules/Toolchain_vita.cmake`, links against
`vita/link/vita-vcmi.ld`, and packages via VitaSDK's `vita_create_self`/
`vita_create_vpk` CMake macros. The result is `eboot.bin` and `vcmiclient.vpk` next
to the linked ELF under `out/build/vita-release/`.

## Installing (once/if a build succeeds)

1. Copy `vcmiclient.vpk` to a PC and install it via VitaShell (or send with FTP + install).
2. Copy your **legally-owned Heroes III** data onto the memory card:
   ```
   ux0:data/vcmi/Data/   (H3 .lod archives etc.)
   ux0:data/vcmi/Maps/
   ux0:data/vcmi/Mp3/
   ```
3. Saves and config are written under `ux0:data/vcmi/`. The cache and log
   (`VCMI_Client_log.txt`) go to `ux0:data/vcmi-cache/`, a *sibling* of the data
   folder - deliberately, as cheap insurance against the same class of bug the Switch
   port hit (a live-open log file being `stat()`-ed mid recursive-scan aborting the
   scan on its SD card's FAT filesystem). This has not been confirmed as an actual
   problem on Vita's exFAT-based `ux0:` - it may turn out to be unnecessary caution.
4. The eboot is built `UNSAFE` with an extended `MEMSIZE` request (~328MB instead of
   the ~26MB default) - the console needs **"Enable Unsafe Homebrew"** set (in
   VitaShell / enso config) or the app likely won't get the memory it needs.

## Runtime crash investigation (Vita3K + real hardware)

**Confirmed root cause, fixed**: `libSceLibMonoBridge_stub[_weak].a` (a vitasdk stub
archive for Sony's Mono/.NET-bridge system module - unrelated to VCMI, which has no
Mono usage) statically defines stub-trampoline symbols for the *entire pthread API
surface* (`pthread_create`, `pthread_mutex_lock`, `pthread_key_create`, ...) that
each redirect to a NID import of the Mono-bridge module, not real threading. `client/
CMakeLists.txt`'s Vita link step globs every `.a` in the vitasdk lib directory into
one `--start-group` with `-Wl,--allow-multiple-definition` (needed for a real, separate
reason - see "What the Docker build actually verified" below); because
`libSceLibMonoBridge_stub.a` sorts alphabetically ahead of `libpthread.a`, its bogus
pthread stubs silently won every threading call in the engine, on both Vita3K and
real hardware. Since `ServerRunner` (which runs the local game server) spawns a real
thread unconditionally - not just the Nullkiller2 AI-turn path - this affected every
game session, not just AI turns. This is a very strong match for both the Vita3K
crash shape (an `.init_array`/static-init-adjacent PTE resource error cascading into
memory corruption) and the real-hardware `C1-2609-7` error. **Fixed** by excluding
`libSceLibMonoBridge_stub*.a` from the glob and moving `-lpthread` ahead of the glob
as defense in depth (`client/CMakeLists.txt`).

**Confirmed and fixed separately**: the `UNSAFE MEMSIZE 335872` declaration on
`vita_create_self()` (`clientapp/CMakeLists.txt`) had been stripped out mid-session
while testing an unrelated (and since-ruled-out) Vita3K memory-budget hypothesis, and
was never restored before the real-hardware test that produced `C1-2609-7`. Real
hardware enforces the declared memory budget strictly (unlike Vita3K); a ~15MB
binary with VCMI's real heap needs would very plausibly fail to launch under the
~26MB default homebrew budget. Restored.

**Still open, Vita3K-specific**: with the MonoBridge stub excluded, a minimal,
VCMI-free reproduction (`vita_diag_test/`: just `std::thread t([]{});`, nothing
else - no mutex, no setenv, no VCMI code at all) still crashes identically on Vita3K,
now via `pte_osThreadCreate()`'s legitimate, real `sceKernelCreateThread` path.
Vita3K logs `Unimplemented kstrncmp import called` and `Unimplemented kmemcmp import
called` immediately before an `udf #0xff` trap inside newlib's `_kill_r` (i.e.
something calls `abort()`, and Vita3K can't emulate the signal-delivery trap
newlib's minimal `_kill_r` uses). `kstrncmp`/`kmemcmp` do not appear anywhere in
vitasdk's own static libraries (checked via `nm` across every `.a`, both as defined
*and* as undefined/referenced symbols) - they are not literal symbol names our code
or vitasdk's pthread implementation calls, which points at a NID resolution issue
inside Vita3K itself (its NID→name database mapping the wrong name, or an
incompletely-emulated kernel function pte_osThreadCreate legitimately depends on)
rather than anything fixable from VCMI's or vitasdk's side. Ruled out during
bisection: link order of `_stub.a` vs. `_stub_weak.a` variants (identical crash
address either way), and `setenv`/environment-related codepaths (crash persists in a
test with no environment calls at all). **Real hardware runs Sony's actual kernel,
not a third-party reimplementation with emulation gaps, so it may well not hit this
specific issue** - but that can only be confirmed by testing the MonoBridge-fixed
build on the console itself, which is the next concrete step.

Bisection method, for anyone picking this up: rather than debug the full ~650-TU
client, link trivial test programs against the *actual* prebuilt `libvcmi.a` /
`libvcmiservercommon.a` / `libvcmiclientcommon.a` (`out/build/vita-release/bin/*.a`)
via `-Wl,--whole-archive` (forces every translation unit's real static initializers
to run, not just ones a trivial `main()` happens to reference), install the
resulting `eboot.bin` into an existing installed Vita3K app's directory
(`~/.local/share/Vita3K/Vita3K/ux0/app/<TITLEID>/eboot.bin`) to skip Vita3K's
interactive VPK-install dialog, and read `~/.cache/Vita3K/vita3k.log` directly. This
isolated `libvcmi.a`'s (core engine + AI) static initializers as crash-free
(~485ms of static-init work, clean exit) before `libvcmiclientcommon.a`
(client/SDL/render code) reproduced the exact original crash cascade.

## Known risks (most of these could not be checked without hardware)

- **Memory.** The user explicitly chose full feature parity (video + Lua) over a
  leaner first pass, accepting this risk. VCMI plus H3's assets, xBRZ upscaling, and
  a full asset/mod tree is a lot to fit even inside the ~328MB extended-memory budget
  requested above; on a device with 512MB total RAM shared with the OS, this may not
  fit, especially on larger maps or with big mod packs installed. There's no engine
  memory profiling data for this port to say more precisely.
- **LuaJIT vs. plain Lua.** Addressed by building plain Lua from source instead of
  using vdpm's LuaJIT package (see the architecture table above) - this should avoid
  the JIT/W^X crash risk entirely, but "should" is doing real work in that sentence
  until it's actually exercised on hardware.
- **Threading correctness.** The TBB shim's `concurrent_*` containers are
  mutex-guarded specifically because the Nullkiller2 AI turn runs on a real
  `std::thread` (see `lib/AsyncRunner.h`'s Vita branch) concurrently with the
  main/render thread. This reasoning was verified against VCMI's actual call sites,
  but has not been exercised under real concurrency on Vita's ARM Cortex-A9 (weaker
  memory ordering guarantees than the x86/ARM64 machines this was written on).
- **Extended memory eligibility.** Some Vita firmware/plugin configurations don't
  grant the "unsafe homebrew" extended-memory permission at all, or grant a smaller
  budget than requested; there's no fallback path in this build for that case.
- **Enum ABI mismatch warnings.** The link emits many
  `uses 32-bit enums yet the output is to use variable-size enums` warnings for
  VCMI's own object files. This is because vdpm's prebuilt packages (boost, SDL2,
  ffmpeg, ...) were built with the ARM EABI's default short-enum setting, while VCMI's
  own code is forced to `-fno-short-enums` (see the architecture table - required for
  its own internal correctness). This is only a real problem where an enum value
  actually crosses the boundary between VCMI's code and one of those libraries' C++
  APIs; none of them expose raw C++ `enum`-typed parameters in the functions VCMI
  calls, so it's expected to be harmless, but this was not exhaustively verified
  against every call site.
- **Software keyboard.** `client/widgets/CTextInput.cpp` was not given a Vita-specific
  `sceImeDialog` hookup (the Switch port's equivalent used `swkbd`) - Vita's IME is an
  asynchronous, poll-driven API rather than SDL's/Switch's blocking-call shape, which
  would need a real per-frame poll hook in the render loop, not just a local code
  change. Left as a known gap rather than rushed; text input may not work correctly
  wherever the engine expects an on-screen keyboard.

## What the Docker build actually verified

The full pipeline - VitaSDK bootstrap, vdpm packages, the three from-source
dependencies (Lua, libsquish, the iconv shim), CMake configure, compiling all ~650
translation units, linking, and the three-stage Vita packaging
(`vita-elf-create` → `vita-make-fself` → `vita-pack-vpk`) - completes with exit code 0
and produces a `vcmiclient.vpk` containing a valid `eboot.bin`, `sce_sys/param.sfo`,
and the full bundled `config`/`Mods`/`scripts` trees (663 files, ~19MB).

Getting there took a long iterative loop against real compiler/linker output (not
guessed fixes) - documented here since several of these are non-obvious and would
otherwise be undiscoverable without a working cross-compiler in hand:

- **ARM EABI short enums** silently violating VCMI's `int32_t`-underlying-type
  static_asserts (`-fno-short-enums`, see architecture table).
- **`CLogConsoleTarget::write()`** assumed a `CConsoleHandler` member that doesn't
  exist under `VCMI_MOBILE` (needed a Vita branch, matching Android/iOS's).
- **Two missing TBB shim headers + one shim bug**, found only once real Nullkiller2/AI
  code exercised them: `concurrent_unordered_map`, `parallel_invoke`, and a
  `deferred_task` constructor that only accepted rvalue functors (broke on lvalue
  callers).
- **vdpm doesn't resolve a package's own dependencies** - `openssl`, `lame`, `xz`,
  `zstd`, `libxmp` all had to be added explicitly once ffmpeg/SDL2_mixer's *actual*
  (as opposed to assumed) transitive link requirements surfaced as "undefined
  reference" errors, not from reading either package's declared dependencies anywhere.
- **GNU libiconv's build was abandoned** after multiple distinct cross-compilation
  failures specific to this newlib target (a broken cross-compile guess defaulting
  `mbrtowc` to "unavailable" and then emitting a conflicting replacement declaration;
  a broken generated `signal.h` replacement; GCC 15's C23 default dialect breaking
  old-style K&R declarations on top of that) - replaced with the small purpose-built
  shim described in the architecture table.
- **Lua 5.1's headers have no `extern "C"` self-guard** (that convention came later,
  via a separate `lua.hpp` that plain Lua 5.1 doesn't ship but LuaJIT does). Without
  it, every Lua C API call in VCMI's C++ code got C++ (mangled) linkage while
  `liblua.a`'s own plain-C-compiled objects exported unmangled names - every call site
  was an unresolvable symbol mismatch (`_Z9lua_pcallP9lua_Stateiii` wanted,
  `lua_pcall` provided) until `build-lua.sh` started generating a `lua.hpp` too. This
  was the single most time-consuming bug in the whole port: the symptom (plain
  "undefined reference to lua_pcall") gave no hint that linkage, not archive
  ordering, was the actual cause, and it only reproduced with the *real* client
  libraries linked in, not in cut-down isolation - many linker-archaeology dead ends
  (segment ordering, `--whole-archive`, `--allow-multiple-definition`, response-file
  argument limits) were chased before `readelf -sW`'s mangled-vs-unmangled symbol
  names made the real cause obvious.
- **`-fPIC` (VCMI's project-wide default) breaks `vita-elf-create`**, which only
  understands a fixed set of ARM relocation types and rejects the GOT-relative ones
  PIC code generates ("Invalid relocation type 25!"). Turned off for Vita only.
- **`vita-elf-create` needs room to inject SCE metadata** between the text and data
  segments, and VCMI's unusually large (~21MB) text segment left too little of it by
  default - fixed with a one-line-changed copy of vitasdk's own linker script.
- **`CMAKE_TOOLCHAIN_FILE` gets `include()`-d more than once per configure** (e.g.
  once during the `CMAKE_C/CXX_COMPILER_WORKS` `try_compile` checks, again for the
  main project) - `Toolchain_vita.cmake` unconditionally appended to
  `CMAKE_EXE_LINKER_FLAGS`, so a genuinely fresh (`rm -rf`'d) build directory hit
  `ld: error: linker script file '...' appears multiple times`, while incrementally
  reconfigured build directories masked it (cached flag values weren't reprocessed
  the same way). Fixed with a `CACHE INTERNAL` guard variable around the append.
- **A very expensive false lead**: several iterations were spent on the wrong
  location within that linker script (a `SEGMENT_START("ldata-segment", .)` line, not
  the actual `R E`/`RW` PT_LOAD segment boundary a few lines earlier), and separately,
  several more were spent because CMake's build directory in this environment ends up
  root-owned (Docker runs as root by default) - a non-root `rm -rf` on it fails
  silently, so what looked like "clean rebuilds" that mysteriously didn't pick up
  changes were actually stale, unchanged build trees the whole time. Both are worth
  knowing if picking this up again: verify a "clean" rebuild actually happened (check
  compile step counts, not just exit codes), and remember ninja doesn't invalidate a
  cached link command just because a file referenced only via a `-Wl,-T,<path>` flag
  changed content - deleting the specific output (as root) forces it.
