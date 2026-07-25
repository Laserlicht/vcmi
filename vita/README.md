# VCMI — PS Vita port (VitaSDK)

This directory contains the PS Vita (homebrew) support for VCMI: a CMake toolchain
wrapper, a header-only sequential TBB shim, source-build scripts for the three
dependencies vdpm doesn't package, a custom linker script, and the .vpk packaging
glue. Like the Android/iOS
ports (and the unmerged Nintendo Switch port this one used as an architecture
reference — `develop...NaGaa95:vcmi_nx:switch`, on GitHub), the Vita is treated as a
**mobile-class target**: a single statically-linked executable that runs the game
server in-process (no `fork`/`exec`), with no Qt launcher or map editor.

## Status: three runtime-crash root causes fixed; packaging currently being re-verified

**The Docker build cross-compiles VCMI end to end.** A later session gained access to
the Vita3K emulator, real Heroes III data, and (still later) the user's real PS Vita
hardware. Both crashed identically on launch (Vita3K: `std::bad_alloc` → an infinite
"Invalid read of uint32_t" cascade → native SIGSEGV; real hardware: system error
`C1-2609-7`). Three distinct, confirmed root causes have since been fixed - see
"Runtime crash investigation" below for the full story of each:

1. `libSceLibMonoBridge_stub.a` was silently hijacking every pthread call in the
   engine (link-order bug in `client/CMakeLists.txt`) - **fixed**.
2. The real-hardware `UNSAFE MEMSIZE` declaration had been accidentally left stripped
   out from an earlier diagnostic session - **restored**.
3. `std::thread` itself crashes on Vita (both emulator and real hardware) for reasons
   isolated to precompiled vitasdk/libstdc++ internals; raw `pthread_create()` does
   not - **fixed** by introducing `lib/VCMIThread.h` (a minimal std::thread-compatible
   wrapper: raw pthread on Vita, a plain `using VCMIThread = std::thread;` alias
   everywhere else) and swapping every real thread-creation call site to use it.

**Not yet re-verified end-to-end as of this writing**: adding `lib/VCMIThread.h`
(fix #3, above) touched enough files to trigger a CMake reconfigure, which exposed a
*fourth*, unrelated bug purely in the build's own packaging step - a CMake toolchain
idempotency bug that silently drops the custom linker script on any reconfigure (see
"Packaging bug" below). That bug is now fixed too, but a clean build + Vita3K smoke
test to confirm the fully-fixed `vcmiclient.vpk` actually boots past the original
crash point **was still running when this session had to hand off**. Check whether
`out/build/vita-release/clientapp/vcmiclient.vpk` exists and is recent (see
`/tmp/vita_build_verify.log` if that path is still around, or just rerun the build -
see "Building" below) before assuming any of this actually works end to end.
**The real-hardware retest that would confirm fix #1/#2 resolve `C1-2609-7`** also
has not happened yet - that's still the next concrete step once a good build exists.

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
- `lib/AsyncRunner.h` + `AI/Nullkiller2/AIGateway.cpp` - real-thread opt-in for the AI turn (`AsyncRunner(bool useRealThreads)`), since the TBB shim's tasking primitives are sequential and running the AI turn inline would deadlock the client (it blocks on the network thread while it runs). Uses `VCMIThread` (see below), not `std::thread` directly.
- `lib/VCMIThread.h` - new. Minimal std::thread-API-compatible wrapper; raw `pthread_create`/`join`/`detach` on Vita (where `std::thread` itself crashes - see "Runtime crash investigation"), `using VCMIThread = std::thread;` everywhere else. Every real thread-creation call site in the engine was swapped from `std::thread` to this: `lib/CConsoleHandler.{h,cpp}`, `client/CServerHandler.{h,cpp}`, `client/ServerRunner.{h,cpp}`, `client/ArtifactsUIController.cpp`, `client/battle/BattleInterface.cpp`, `client/adventureMap/CInGameConsole.cpp`, `clientapp/EntryPoint.cpp`, `server/CVCMIServer.cpp`.
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

### Fix #1: MonoBridge pthread stub collision

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

### Fix #2: missing MEMSIZE declaration

**Confirmed and fixed separately**: the `UNSAFE MEMSIZE 335872` declaration on
`vita_create_self()` (`clientapp/CMakeLists.txt`) had been stripped out mid-session
while testing an unrelated (and since-ruled-out) Vita3K memory-budget hypothesis, and
was never restored before the real-hardware test that produced `C1-2609-7`. Real
hardware enforces the declared memory budget strictly (unlike Vita3K); a ~15MB
binary with VCMI's real heap needs would very plausibly fail to launch under the
~26MB default homebrew budget. Restored.

### Fix #3: std::thread itself crashes on Vita

With the MonoBridge stub excluded, a minimal, VCMI-free reproduction
(`vita_diag_test/`: just `std::thread t([]{});`, nothing else - no mutex, no setenv,
no VCMI code at all) still crashed identically on Vita3K, via `pte_osThreadCreate()`'s
legitimate, real `sceKernelCreateThread` path. Vita3K logged `Unimplemented kstrncmp
import called` and `Unimplemented kmemcmp import called` immediately before an
`udf #0xff` trap inside newlib's `_kill_r` (i.e. something calls `abort()`, and
Vita3K can't emulate the signal-delivery trap newlib's minimal `_kill_r` uses).
`kstrncmp`/`kmemcmp` do not appear anywhere in vitasdk's own static libraries
(checked via `nm` across every `.a`, both as defined *and* as undefined/referenced
symbols) - not literal symbol names our code or vitasdk's pthread implementation
calls.

Critically: **raw C `pthread_create()`** (bypassing libstdc++'s `std::thread` C++
wrapper entirely) worked correctly in the same minimal-repro harness, with the exact
same underlying `pte_osThreadCreate()`/`sceKernelCreateThread()` call chain. This was
narrowed down further by disassembling libstdc++'s `thread.o`
(`_M_start_thread`/`execute_native_thread_routine`): its code is trivially simple - a
`pthread_create()` call plus a tiny virtual-dispatch trampoline, nothing unusual - so
whatever differs must live inside precompiled vitasdk/libstdc++ C-library
initialization plumbing that only gets pulled in when `std::thread`'s specific link
footprint is present, not something patchable in VCMI's own source or even easily
diagnosable further by disassembly alone. Ruled out during bisection: link order of
`_stub.a` vs. `_stub_weak.a` variants (identical crash address either way),
`setenv`/environment-related codepaths, and lambda/closure type-erasure (a plain
function pointer crashes identically). Also confirmed: this is not a version issue -
the installed Vita3K build (`v0.2.1 4066-5ded8c78`, 2026-07-23) was already the
latest available release at the time of testing.

**Fixed** by adding `lib/VCMIThread.h`: a minimal, std::thread-API-compatible class
(constructible from any invocable + args, `joinable()`/`join()`/`detach()`/
`get_id()`) that wraps raw `pthread_create()`/`pthread_join()`/`pthread_detach()`
directly on Vita, and is just `using VCMIThread = std::thread;` on every other
platform - zero behavior change anywhere except Vita. One deliberate semantic
divergence from `std::thread`: destroying a still-joinable `VCMIThread` detaches it
instead of calling `std::terminate()`, since a hard terminate is a worse outcome on a
homebrew console than a detached thread. Every real thread-creation call site was
swapped to use it: `lib/AsyncRunner.h`, `lib/CConsoleHandler.{h,cpp}`,
`client/CServerHandler.{h,cpp}`, `client/ServerRunner.{h,cpp}`,
`client/ArtifactsUIController.cpp`, `client/battle/BattleInterface.cpp`,
`client/adventureMap/CInGameConsole.cpp`, `clientapp/EntryPoint.cpp`,
`server/CVCMIServer.cpp` (that last one matters for Vita too, even though
`ENABLE_SERVER=OFF` - the single-app build still compiles server code into the
client binary via `libvcmiservercommon`, and `ServerRunner` runs it in-process on a
real thread). Left untouched: `std::this_thread::get_id()` /
`std::hash<std::thread::id>` usages in `lib/CThreadHelper.cpp` and
`lib/CRandomGenerator.cpp` - those only *identify* the current thread, they never
create one, so they don't exercise the broken code path (a companion
`VCMIThisThread::get_id()` was added instead of touching those, used at the one
call site in `client/CServerHandler.cpp` that compares thread IDs across the
VCMIThread/std::thread boundary).

**This fix has not yet been runtime-verified** - the build that would prove it
(compile the real client with `VCMIThread` and confirm no crash on Vita3K) hit the
packaging bug described next before a test could happen. See "Status" above for
where to pick this up.

Bisection method, for anyone continuing this investigation: rather than debug the
full ~650-TU client, link trivial test programs against the *actual* prebuilt
`libvcmi.a` / `libvcmiservercommon.a` / `libvcmiclientcommon.a`
(`out/build/vita-release/bin/*.a`) via `-Wl,--whole-archive` (forces every
translation unit's real static initializers to run, not just ones a trivial
`main()` happens to reference), install the resulting `eboot.bin` into an existing
installed Vita3K app's directory
(`~/.local/share/Vita3K/Vita3K/ux0/app/<TITLEID>/eboot.bin`) to skip Vita3K's
interactive VPK-install dialog, and read `~/.cache/Vita3K/vita3k.log` directly
(launch with `Vita3K -r <TITLEID> -l 0`; note that guest `printf`/stdout output does
*not* appear to land in that log file, only Vita3K's own HLE trace does - rely on
crash signatures and clean-exit/"Game closed" markers, not printed checkpoints, when
testing this way). This isolated `libvcmi.a`'s (core engine + AI) static
initializers as crash-free (~485ms of static-init work, clean exit) before
`libvcmiclientcommon.a` (client/SDL/render code) reproduced the exact original
crash cascade, and later isolated the crash down to `std::thread` construction
specifically (vs. mutex-only usage, which is a no-op weak-symbol stub unless real
thread creation is also linked in and actually exercised).

### Packaging bug found while verifying fix #3: reconfigure silently drops the linker script

While rebuilding to pick up `VCMIThread.h`, `vita-elf-create` failed again with the
same `Cannot allocate N bytes for SCE data ...; segment overlaps` error that fix
"Linker script" (architecture table, above) was supposed to have already solved.
Root cause: `cmake_modules/Toolchain_vita.cmake` guarded against appending the
custom `-Wl,-T,vita-vcmi.ld` flag twice within one configure (a real bug, fixed
earlier - `CMAKE_TOOLCHAIN_FILE` is `include()`-d more than once per configure) using
a `CACHE INTERNAL` boolean. That guard is wrong: `CACHE` variables persist in
`CMakeCache.txt` across *separate* `cmake` invocations, not just within one
configure. On any reconfigure of an already-configured build directory (e.g.
touching a `CMakeLists.txt` to add a new source file, exactly what wiring in
`VCMIThread.h` did), the boolean was already `TRUE` from the *previous* configure, so
the flag was never re-applied to the fresh, empty-for-this-run
`CMAKE_EXE_LINKER_FLAGS` at all. Nothing rejects a *missing* flag, so the build
"succeeds" all the way through compiling and linking `bin/vcmiclient` - it just
silently reverts to vitasdk's *unmodified* default linker script (with none of the
extra padding), and only fails much later, at the `vita-elf-create` packaging step,
with the exact same symptom the original fix already claimed to have solved. Confirmed
by grepping the generated `build.ninja` for `vita-vcmi.ld` after a "successful"
build and finding zero occurrences. Wasted a fair amount of time initially suspecting
the *padding amount* itself was insufficient (tried increasing it several times,
including to a deliberately distinctive value to test causality) before checking
whether the flag was even present in the link command at all.

**Fixed** by checking the *current value* of `CMAKE_EXE_LINKER_FLAGS` directly
(`if(NOT CMAKE_EXE_LINKER_FLAGS MATCHES "vita-vcmi\\.ld")`) instead of a separately
persisted flag - this is correct in both cases: it dedupes within one configure, and
starts fresh every time `CMAKE_EXE_LINKER_FLAGS` itself does. Also bumped the padding
from `0x20000` to `0x40000` while at it (more headroom, since the exact metadata size
needed shifts with every code change and isn't worth precisely tuning each time) -
**this specific value has not been runtime-verified either**, only that the flag is
now actually being applied (confirmed via the same `build.ninja` grep). A build to
confirm the packaging step itself succeeds again was in progress when this session
ended - check `out/build/vita-release/clientapp/vcmiclient.vpk`'s timestamp, or
just rerun `cmake --build --preset vita-release` and watch for the
`vita-elf-create`/"Converting to Sony ELF" step.

**Lesson for future linker-script-flag changes on this toolchain**: after editing
`vita/link/vita-vcmi.ld` or `Toolchain_vita.cmake`, don't trust an "incremental"
build to pick it up. Ninja does not invalidate a cached link command just because a
file referenced only via a `-Wl,-T,<path>` flag changed content, *and* a stale
`CACHE` variable can silently suppress the flag being passed at all on reconfigure.
Delete `out/build/vita-release/bin/vcmiclient` itself (not just the downstream
`.velf`/`eboot.bin`/`.vpk` outputs) and re-run `cmake --preset vita-release &&
cmake --build --preset vita-release` to be sure a change actually took effect; verify
with `arm-vita-eabi-readelf -lW out/build/vita-release/bin/vcmiclient | grep LOAD`
that the RW segment's start address actually moved before concluding a padding
change had any effect.

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
  the same way). The *first* fix attempt (a `CACHE INTERNAL` guard variable) turned
  out to be wrong in a different way and caused a follow-on bug - see "Packaging bug"
  under "Runtime crash investigation" above for the full story and the actual fix
  (checking `CMAKE_EXE_LINKER_FLAGS`'s own content instead of a separate flag).
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

## Picking this up in a fresh session (no memory of prior work)

Concrete state as of the end of this session, so a fresh session (human or LLM) can
verify rather than re-derive:

- **Git**: branch `vita`, local commit `fbcc05c53` ("Add PS Vita port (VitaSDK
  cross-compile, Docker build)") contains everything through fix #1/#2/#3 above
  (MonoBridge exclusion, MEMSIZE restore, `VCMIThread`). The packaging-bug fix
  (`Toolchain_vita.cmake`'s `CACHE INTERNAL` guard → content-check, and the
  `vita-vcmi.ld` padding bump to `0x40000`) was made *after* that commit and may or
  may not be committed yet depending on exactly when this session ended - `git log`
  and `git status`/`git diff` on `cmake_modules/Toolchain_vita.cmake` and
  `vita/link/vita-vcmi.ld` to check. A remote named `laserlicht` pointing at
  `https://github.com/Laserlicht/vcmi.git` was added but **the push was blocked on
  missing credentials** in that sandboxed environment (no stored token, no SSH key) -
  confirm whether `git push -u laserlicht vita` has since succeeded before assuming
  the fork is up to date. `vita_diag_test/` is a large (~27MB) scratch directory of
  build artifacts from the debugging session, deliberately left untracked/uncommitted
  - fine to delete, or keep for reference, but never commit it as-is (it's compiled
  binaries, not source).
- **Immediate next step**: confirm the build actually completes cleanly end to end
  with all current fixes applied (see "Building" above; watch specifically for the
  `vita-elf-create`/"Converting to Sony ELF" step succeeding, not just compilation).
  Then install the resulting `vcmiclient.vpk` and test on Vita3K (see the bisection
  method under "Runtime crash investigation" for the fast eboot-swap iteration
  technique - no need to reinstall the full VPK for every test) to confirm the
  original crash cascade is actually gone, not just that the theory sounds right.
  Only after that: ask the user to retest on real hardware.
- **Known-good reference points from this session**: a full clean build succeeded
  (651/651, valid `.vpk` produced) with fixes #1 and #2 applied, *before* the
  `VCMIThread` change existed. That build's runtime behavior on Vita3K was never
  actually tested (the session moved straight to implementing `VCMIThread`) - it's
  unknown whether fix #1+#2 alone (without fix #3) would have been enough to survive
  Vita3K's `std::thread` crash, though logically it should not have been (fix #3
  addresses a completely separate, independently-reproduced crash).
- **Vita3K test setup**, if not already present: emulator at whatever path the user
  has it (`~/Downloads/Vita3K-x86_64.AppImage` in this session); installed apps live
  under `~/.local/share/Vita3K/Vita3K/ux0/app/<TITLEID>/`; the real VCMI install used
  title ID `VCMI00001`; a separate `TEST00001` slot was used for minimal diagnostic
  repros (see `vita_diag_test/`) so the two don't interfere. Launch non-interactively
  with `Vita3K -r <TITLEID> -l 0`; a real GUI window opens regardless of flags used
  (there is no headless mode) - the user is aware of this and has approved driving it
  this way. Logs land in `~/.cache/Vita3K/vita3k.log`; delete it before each run to
  avoid confusing old/new output. Guest `printf` output does not reliably appear in
  that log - rely on crash signatures (`Unhandled SIGSEGV`, `Unimplemented ... import
  called`, `Invalid read/write of uint32_t`) and phase markers (`Game started`/`Game
  closed`) instead.
