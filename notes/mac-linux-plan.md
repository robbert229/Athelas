# Native Mac and Linux: The Return of the King (Xbox) roadmap (2026-11-23)

Goal: ROTKXbox running natively on Linux (x86-64 desktop and Steam Deck, then ARM64) and macOS (Apple Silicon), with
the same settings, saves and co-op as on Windows, and nothing the player has to install beyond a graphics driver.
Windows builds keep working throughout; nothing here changes how they behave.

Scope: the Xbox port only. ROTK PC (ROTK Fix) is a DLL loaded into the Windows game and can't become native; on
Linux and Mac it stays as it is today (Proton, CrossOver). The SDK work carries over to Two Towers, Fellowship and
Third Age (see "Later" at the end).

## Where we start

Most of the hard part is already portable:

- **The game and the SDK core already run on Linux.** The headless harness (`sdk/harness/`) builds the recompiled
  code with clang on Linux together with the runtime's `cpu/`, `kernel/`, `xdk/`, `core/` (except `input.cpp`) and the
  ROTK game code. ROTK boots, plays its movies and reaches the menus (`ports/rotk-xbox/notes/rotkx-01-bringup.md`).
  `kernel/memory.cpp` and `kernel/threads.cpp` already have POSIX paths (mmap + memfd, pthreads).
- **ARM64 is already done once.** `build_arm64.bat` builds Windows ARM64, so the generated code doesn't depend on an
  x86 host (`XBE_HAS_MXCSR` only covers tracing).
- **Vulkan has shipped** (1.8.5) for all four games, and `gpu/vk_loader.cpp` already `dlopen`s `libvulkan.so.1`.
  ROTK doesn't use `gpu::Extension` (only Two Towers does, through `tt_renderer.cpp`), so ROTK doesn't need the D3D12-only
  extension API.
- **Pads, settings, UPnP:** SDL3 (pads), toml++ (settings) and miniupnpc (co-op) are all portable already.

What's left is the Windows-only edge: the window, keyboard, audio output, crash handling, a few app-layer
extras, the build, the launcher and packaging.

## Decisions to make first

1. **Where user data lives.** Today saves and settings sit beside the exe. That breaks inside a read-only AppImage
   and inside a signed Mac `.app`. Proposal: keep "beside the exe" on Windows. On Linux use `$XDG_DATA_HOME/Athelas`
   (or `~/.local/share/Athelas`), on Mac `~/Library/Application Support/Athelas`, and keep a `portable.txt` override
   for players who want everything in one folder.
2. **Window layer.** Proposal: an SDL3 window and `imgui_impl_sdl3` on Linux and Mac only. The Win32 window
   (`app/main.cpp` `WndProc`), raw input and `imgui_impl_win32` stay on Windows, so Windows players see no change.
   Moving Windows to SDL3 as well is possible later, once the SDL3 path has proved itself.
3. **Mac Vulkan driver.** MoltenVK (mature, bundled as a dylib, portability subset) or KosmicKrisp (LunarG's Mesa
   Vulkan-on-Metal driver, newer and fuller). Proposal: build against the Vulkan loader and test both; ship the one
   that renders ROTK correctly.
4. **Mac signing.** Without an Apple Developer ID (99 USD/year) and notarisation, Gatekeeper blocks the app and
   players must allow it in System Settings > Privacy & Security. That's the Mac version of the "Run anyway" step
   Windows players already get. Ad-hoc signing is enough for arm64 to run. Decide whether to pay for a Developer ID.
5. **Launcher or no launcher at first.** Proposal: ship the port on its own first (settings through `settings.toml`
   and the in-game overlay), and port the launcher as a separate milestone. The launcher is the bigger job (below).

## M1: Linux, playable from a source build

Exit: on a Linux desktop and on a Steam Deck (desktop mode, then Game Mode as a non-Steam game), ROTK runs in a window
and fullscreen on Vulkan, with sound, pad and keyboard/mouse, and saves and loads; level 1 completes at 60 fps.

- **Build.** `sdk/cmake/XbeRecomp.cmake` assumes Windows: `add_executable(... WIN32 ...)`, the manifest `.rc`, and
  links `d3d12 dxgi xaudio2 dbghelp ws2_32 ...` (line ~207). Split those by platform. Leave d3d12.cpp, upscale.cpp,
  xaudio2.cpp and the Win32 app files out of non-Windows builds (as `harness/build.sh` already does), add
  `-ldl -lpthread`, and add Linux presets to `CMakePresets.json` (all presets have `hostSystemName == Windows` today).
  Add a `build.sh` beside `build.bat`, running the same recompile, then CMake.
- **The `_WIN32` meaning "test harness" problem.** ROTK's game code uses `#ifndef _WIN32` for harness-only test
  hooks (`rotk_movies.cpp:156`, `rotk_menu_art.cpp:387`, `rotk_fonts.cpp:279`, `rotk_widescreen.cpp:47/218`,
  `rotk_secrets.cpp:41`, `rotk_harness_probe.cpp`), and the SDK may do the same in places. A native Linux build would
  pull all of them in. Add `XBE_HEADLESS` (defined by `harness/build.sh`) and switch the test hooks to it **first**,
  before any other Linux work, checking each `_WIN32` in `runtime/` and `src/game/` as we go.
- **Window, events, frame pacing** (`app/main.cpp`, 552 lines): an SDL3 window (borderless fullscreen and windowed,
  the same placement as `settings.cpp`'s monitor maths through `SDL_GetDisplayBounds`), the SDL event loop forwarding
  to ImGui and to `GameInfo::message`, the frame limiter (replacing `CreateWaitableTimerExW` and `timeBeginPeriod`
  with `clock_nanosleep` plus a short spin), and the command line (`std::span<char*>` instead of `GetCommandLineW`).
- **Keyboard and mouse** (`app/keyboard.cpp`: raw input, `GetAsyncKeyState`, `GetCursorPos`): SDL3 keyboard state and
  relative mouse mode. ROTK's own uses: `rotk_controls.cpp`, `rotk_input.cpp`, and the F-keys in `rotk_widescreen.cpp`.
  `core/input.cpp` is already SDL3 and needs only its two `_WIN32` blocks checked.
- **Renderer** (`gpu/vk.cpp`): the surface from `SDL_Vulkan_CreateSurface` instead of `vkCreateWin32SurfaceKHR`
  (line 873); `imgui_impl_sdl3` beside `imgui_impl_vulkan`; `HdrScreen` (DXGI, line 1075) through SDL3's display HDR
  properties, or HDR off for a first pass. Force `renderer = Vulkan` off Windows and hide Direct3D 12 and the
  upscalers (DLSS/XeSS/FSR are D3D12-only, as the launcher already says).
- **Audio** (`audio/xaudio2.cpp`, 80 lines): only the output end is XAudio2; the mixer and `reverb.cpp` are software.
  Pull the same mixer from an `SDL_OpenAudioDeviceStream` callback. `harness/null_audio.cpp` shows the pull shape.
- **Crash handling** (`SetUnhandledExceptionFilter` + `MiniDumpWriteDump`, main.cpp:72/471): a `sigaction` handler
  for SIGSEGV/SIGBUS/SIGILL/SIGFPE on an alternate stack that writes a crash log with the guest EIP, the host
  backtrace and the last log lines, to the same `CrashDumps` folder. Minidumps can wait (Crashpad later if needed).
- **Paths.** The host file system is case-sensitive; `kernel/files.cpp` already lowercases lookups and the harness
  runs ROTK, but test extracted folders with mixed case as well as ISOs. Check every `std::wstring` path on the way to
  the host (`wchar_t` is 4 bytes on Linux and Mac) and keep paths UTF-8 off Windows.
- **ROTK settings** (`rotk_settings.cpp:90-110, 326`): `--ini` and `--import-ini` parsing is Windows-only
  (`CommandLineToArgvW`); make it portable so the launcher can pass them on every platform.
- **Steam Deck check:** Game Mode via "Add a Non-Steam Game" on the native binary, Deck controls as an Xbox pad,
  1280x800 (16:10 is already covered by the widescreen work), 40/60 fps caps.

## M2: Linux, complete and packaged

Exit: a Linux download a player can extract and run on a clean SteamOS / Ubuntu 22.04 / Fedora, with every feature
Windows has except the D3D12-only ones.

Work on `integration/m2` (stacked on `integration/m1`), in four workstreams:

| Stream | Scope |
| --- | --- |
| M2-A app layer | `splash.cpp`, `system_info.cpp`, `discord.cpp`, `handover.cpp` and the message boxes on POSIX; `posix_pending.cpp` emptied and deleted |
| M2-B keyboard and prompts | `prompt_style.cpp` and `rotk_prompts.cpp` key caps without GDI; keyboard and mouse checked in the game |
| M2-C online co-op | `netplay_window.cpp` as SDL3 + ImGui, the session folder out of the exe's folder, Linux to Linux and Linux to Windows co-op |
| M2-D release, CI, docs | the `.tar.xz`, a Linux CI job, README and dist-files, the M1 leftovers (Vulkan hitch log, Wayland screen choice), clean-distribution runs |

- **App-layer extras**, file by file:
  - `app/splash.cpp` (WIC + GDI): decode with `stb_image` (already in `gpu/stb/`) and show it in the SDL window.
  - `app/prompt_style.cpp` and `rotk_prompts.cpp` (key caps' text drawn with GDI fonts): rasterise with
    `stb_truetype` or the ImGui font atlas.
  - `app/netplay_window.cpp` (467 lines of GDI window for the co-op address): redo it as an SDL3 + ImGui window.
    Simpler, and it then looks the same on every platform.
  - `app/discord.cpp` (named pipe): Discord's Unix socket, `$XDG_RUNTIME_DIR/discord-ipc-0` (and `/tmp`, plus the
    Flatpak and Snap paths).
  - `app/system_info.cpp` (registry, CPU sets): `/proc/cpuinfo`, `uname`, `sched_getaffinity` for the log.
  - `app/game_files.cpp` (`MessageBoxW`): `SDL_ShowSimpleMessageBox`.
  - `app/handover.cpp` (named event): ROTK has one image (`game_info.cpp:49`), so it doesn't need the image handover;
    stub it on POSIX for now. Two Towers and Fellowship do need it (a pipe or an `eventfd` handed to the child).
- **Online co-op** (`xdk/netplay.cpp`, `xdk/xnet.cpp`): Winsock to BSD sockets (partly done; the harness already runs
  two instances over system link). Then test Linux-to-Windows co-op (see the determinism risk below).
- **Packaging:** build inside the Steam Runtime (sniper) SDK container so one binary runs on SteamOS and older
  glibc. SDL3 is already static; rely on the system's `libvulkan.so.1`. Ship a `.tar.xz` with a `.desktop` file and
  icon, plus an AppImage. Flatpak later, if people ask for it.
  - Done for ROTK Xbox: `ports/rotk-xbox/rotkxbox/appimage.sh` builds in an Ubuntu 22.04 container (glibc 2.35,
    clang 22 from apt.llvm.org, libstdc++ static) rather than sniper (glibc 2.31, but too old a C++ library for
    clang 22 without more work), so the exe needs only libc and libm. Its `AppRun` puts the data in
    `$XDG_DATA_HOME/athelas/rotk-xbox` through the existing `--config`/`--saves`/... arguments, so decision 1 needed
    no runtime change for it. Online co-op still writes `online-session` beside the exe (read-only in an AppImage):
    fix that when Linux co-op lands.
  - M2: `appimage.sh` became `release.sh`, which also makes `ROTKXbox-<version>-linux-x86_64.tar.xz`. Both start
    through `linux/rotkxbox.sh` (the AppImage's AppRun), so they keep their data in the same place; a `portable.txt`
    beside the AppImage or in the tarball's folder keeps it there (decision 1's override). Run to the main menu in
    clean Ubuntu 22.04, Fedora 44 and Arch containers (`ports/rotk-xbox/notes/rotkx-13-linux-release.md`).
- **CI:** add a Linux job that builds the runtime and launcher without game code (the game can't be built in CI:
  it needs the player's `default.xbe`). The harness tests stay local. Done for the runtime (M2): `build.yml`'s
  `linux` job builds `xbe_smoke` and `xbe_port_check` with clang 22, runs them under Xvfb on llvmpipe and runs
  `wincheck.sh`. The launcher waits for M4.
- **README / dist-files:** Requirements and Steam Deck sections ("native" next to "Proton").

## M3: macOS (Apple Silicon)

Exit: an arm64 `.app` that plays ROTK on an M1-class Mac (macOS 13+) on Vulkan-on-Metal, at 60 fps in level 1,
fullscreen and windowed, with pad, keyboard and sound.

- **Guest memory** (`kernel/memory.cpp`):
  - No `memfd_create` on macOS: back the physical RAM with `shm_open` + `shm_unlink` (or `mach_vm_remap`) and map
    it twice, as Linux does.
  - **16 KB host pages** (also Asahi Linux): `Commit`/`Decommit` work in 4 KB `kPage` steps through `mprotect`,
    which needs host-page-aligned ranges. Round commits out to the host page; leave a shared host page committed
    until all four of its guest pages are freed; log guest protection changes smaller than a host page instead of
    failing.
  - The `0x100000000` hint lands inside the 4 GB `__PAGEZERO` on arm64 macOS. The kernel then picks another base,
    which works, but crash logs lose their fixed base. Try a higher hint first.
- **Threads** (`kernel/threads.cpp`): `pthread_setaffinity_np` and `cpu_set_t` don't exist on macOS (affinity is
  only a hint there, so make it a no-op). `pthread_setname_np` takes only the name and names only the calling thread.
  The 64 MB stack reservation goes through `pthread_attr_setstacksize`. Check that the game's threads run at QoS
  user-interactive and not on efficiency cores.
- **Linking:** the recompiled functions are `__attribute__((weak))` and host overrides rely on a strong symbol
  winning (lld today, `x86_ops.h:31`). Apple's linker should honour that too, but confirm it early with one override
  and a test that calls through `__imp__`.
- **Vulkan on Metal** (`gpu/vk.cpp`, `gpu/vk_loader.cpp`):
  - The loader opens `libvulkan.1.dylib` from `Contents/Frameworks` (bundle the loader plus the ICD and its JSON),
    with `VK_KHR_portability_enumeration`, `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`, and
    `VK_KHR_portability_subset` on the device.
  - Read `VkPhysicalDevicePortabilitySubsetFeaturesKHR` and check every "false" against what the backend uses:
    triangle fans (already turned into lists in `vertex.cpp` for D3D12), constant-alpha blend factors
    (`vk.cpp:2579`), sampler mip LOD bias (`vk.cpp:3446`, also the player's LOD bias setting), image view swizzles,
    point polygons, separate stencil reference values.
  - No D24S8 on Apple GPUs: the D32S8 fallback and its depth-bias fix already exist (`vk.cpp:1441, 3596`).
  - Pipelines compile SPIR-V to MSL at creation, so first-time hitches are worse than on Windows: keep a pipeline
    cache on disk and warm it up at load.
  - Dynamic rendering is required (`vk.cpp:914`): confirm the chosen driver exposes it.
- **Mac app polish:** `.app` bundle and `Info.plist` (game controller usage, high-resolution capable), Retina
  scaling (render at pixel size, ImGui at point size), native fullscreen Spaces vs borderless, Cmd+Q and Cmd+F, the
  menu bar while windowed, and Game Controller framework pads through SDL3.
- **Packaging:** `.dmg` or `.zip` of the `.app`, ad-hoc or Developer ID signed (decision 4). Universal (x86-64 too)
  only if Intel Mac players ask; Intel Macs' GPUs are weak for 4x render scale anyway.
- **CI:** a `macos-14` (arm64) runner builds the runtime and the launcher.

## M4: the launcher on Linux and Mac

Exit: Athelas on Linux and Mac finds the player's ROTK ISO, sets every option, starts the port, backs up saves,
updates itself and sends "Report a problem", as on Windows.

The launcher's UI is already SDL3 + Dear ImGui (`imgui_impl_sdlrenderer3`), but it's a 32-bit mingw build and 14
files call Win32 directly (`launcher/src/config.cpp` alone uses about 40 distinct Win32 functions: the registry,
`WritePrivateProfileString`, `CreateProcess`/`ShellExecute` at 27 sites).

- Put a small platform layer (`launcher/src/platform.{h,cpp}`) under all of it: files via `std::filesystem`,
  starting processes via `SDL_CreateProcess`, opening links via `SDL_OpenURL`, file pickers via
  `SDL_ShowOpenFileDialog`, a small INI reader and writer of our own instead of `*PrivateProfileString`, and HTTPS via
  libcurl (system on Linux, `NSURLSession` or libcurl on Mac) instead of WinHTTP (`update.cpp`, `ornament.cpp`).
- Builds: 64-bit native, CMake (keep `build.sh`'s mingw build for Windows).
- Pages: hide the ports this platform has no build for, and handle ROTK PC per platform (hide it, or point at
  Proton/CrossOver and offer to install ROTK Fix into a Wine prefix; decide later).
- The updater downloads the release asset for this OS and architecture; release naming
  `Athelas-<version>-{windows,linux-x86_64,linux-arm64,macos-arm64}`.
- `scripts/package.sh` builds the three packages; `build.yml` publishes them on a tag.
- `hwrec.cpp`, `gpu.cpp` (the Vulkan check in About): `vk_loader`'s logic, and SDL display modes.
- `report.cpp` ("Report a problem"): the same zip, with `$HOME` instead of the Windows user name taken out of logs.

## M5: release

- A public beta first, labelled experimental like co-op. Ask for Steam Deck, desktop Linux (AMD/Intel/NVIDIA) and
  M1-M4 Mac reports.
- Release notes, README (Requirements, Quick start per OS), `dist-files/README.txt`.

## Risks

- **Co-op across architectures.** Lockstep co-op needs both PCs to compute identical results. x86-64 and ARM64
  builds can round differently (FMA contraction, libm differences, the x87 emulation's paths), so a Mac player and a
  Windows player could desync. Build the generated code with `-ffp-contract=off` everywhere, run the lockstep checks
  between an x86-64 and an ARM64 build, and only allow mixed-architecture co-op once they match; until then, warn or
  block it.
- **Vulkan-on-Metal gaps:** a missing feature that ROTK's combiners need could mean a shader workaround. Find out in
  week one of M3 by running a frame capture of the menu and of level 1.
- **16 KB pages:** a game that changes protection at 4 KB granularity and relies on the fault could misbehave. ROTK
  isn't known to; the harness can confirm by logging every protection call under a 16 KB page build on Asahi.
- **Linux drivers:** NVIDIA's proprietary driver, Mesa RADV, Intel ANV all need a pass; the D3D12 path's driver
  workarounds (if any) need Vulkan counterparts.
- **Support load:** three platforms triple the "Report a problem" surface. The launcher's reports need the OS and
  driver in them from day one.

## Order of work and rough size

| Step | Size | Depends on |
| --- | --- | --- |
| `XBE_HEADLESS` split | S | none |
| CMake + presets + `build.sh` (Linux) | M | none |
| SDL3 window, events, frame pacing, keyboard | M | build |
| Vulkan surface + ImGui SDL3 | S | window |
| SDL3 audio | S | build |
| Crash handler, paths, ROTK settings args | S | build |
| **M1 Linux playable** | | all of the above |
| Splash, prompts, netplay window, Discord, system info | M | M1 |
| Linux co-op, packaging, CI | M | M1 |
| **M2 Linux release candidate** | | |
| Memory (shm, 16 KB), threads, linker check | M | M1 |
| Vulkan-on-Metal bring-up and feature audit | L | M1 |
| `.app`, Retina, fullscreen, signing | M | Metal |
| **M3 Mac release candidate** | | |
| Launcher platform layer, 64-bit build, updater, packaging | L | M2 |
| **M4 launcher** / **M5 release** | | |

M1 to M3 are mostly SDK work in `sdk/runtime/app`, `gpu/vk.cpp`, `audio/`, `kernel/memory.cpp` and
`kernel/threads.cpp`, plus a handful of ROTK files. The launcher (M4) is the largest single piece and can run in
parallel with M3 once M2 is done.

## Later: the other ports

The SDK work above is shared, so Fellowship and Third Age need only their own `_WIN32` checks and a test pass.
Two Towers renders part of its look through `gpu::Extension`, which is D3D12-only (`gpu/ext.h` hands extensions an
`ID3D12Device` and command list). It needs a backend-neutral extension API (or a Vulkan twin of
`tt_renderer.cpp`) before it can run on Vulkan anywhere, including Windows. Two Towers and Fellowship also need the
image handover on POSIX.
