# Smoke test

`xbe_smoke` is the SDK runtime linked and started with no game. It checks that the host side of a port (kernel,
XDK, renderer, sound, window) builds and starts on a platform before any game has been recompiled there. The
recompiled code is a stand in (`cmake/XbeStandIn.cmake`): every XDK symbol the SDK's signatures name is absent, and
every recompiled function traps.

Alone it starts logging, the settings file and guest memory, then exits 0. `--window` opens the game window and logs
what reaches the game from it, with a paced frame thread (`--fps`). `--vulkan` opens the window and draws N frames
(`--frames N`, 0 until the window is closed) through the real Vulkan backend: a clear and three triangles through
`Backend::Draw` (fixed-function state, shaders through glslang), shown at the console's 4:3 with the ImGui overlay.
`--audio` plays a 440/660 Hz tone through the platform audio output, alone for N / 60 seconds or while the frames are
drawn. It exits 1 when the renderer doesn't start or the output pulls less than half real time.

## Build and run (Linux)

```
cmake -S sdk/smoke -B sdk/smoke/build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
ninja -C sdk/smoke/build xbe_smoke
cd sdk/smoke/build && ./xbe_smoke [--window] [--vulkan] [--audio] [--frames N] [--fps N]
```

Useful with `--vulkan` and `--audio`: `XBE_VK_VALIDATE=1` (or `sync`) logs the Khronos validation layer's errors, or
`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` prints its warnings too; `SDL_VIDEO_DRIVER=x11` or `wayland`;
`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json` for llvmpipe; `SDL_AUDIO_DRIVER=disk` writes the sound
to `sdlaudio.raw` (float stereo, 48 kHz) to check it.

It writes `logs/` and reads `smoke.toml` beside the exe. The first configure downloads the SDK's dependencies (SDL3,
glslang, SPIRV-Tools and the rest), so it takes a minute. `xbe_recomp` and `xbe_symbols` build in the same tree.

## A port's own sources

`-DXBE_PORT=<port project>` adds `xbe_port_check`. It compiles that port's `src/*/*.cpp` and links them with the
runtime and a stand in made from its `config/game.toml` (the symbols' addresses, plus every `sub_XXXXXXXX` its sources
name). No game files are needed. Use it to check a port's changes on a platform where it can't be built in full yet:

```
cmake -S sdk/smoke -B sdk/smoke/build -DXBE_PORT=$PWD/ports/rotk-xbox/rotkxbox
ninja -C sdk/smoke/build xbe_port_check
```

Until the SDL3 window layer lands, running it only prints that the window layer is missing.

## Windows syntax check

`wincheck.sh <source>...` compiles files for Windows (`-fsyntax-only`, clang targeting x86_64-w64-mingw32 with
Fedora's mingw64 headers: `sudo dnf install mingw64-gcc-c++ mingw64-headers`), to catch Windows breakage from Linux.
It covers the whole runtime, Direct3D 12, XAudio2 and the upscalers included. It needs the smoke build configured, for
the dependencies, and `-DXBE_PORT` configured for a port's sources. For example, every file a branch changed:

```
sdk/smoke/wincheck.sh $(git diff --name-only origin/main -- '*.cpp')
```

## Windows cross-build

`wincross.sh` builds and links `xbe_smoke.exe` for Windows from Linux, through `XbeRecomp.cmake`'s Windows path
(`mingw-clang.cmake`: clang and lld for x86_64-w64-mingw32 with Fedora's mingw64 sysroot, linked statically). The link
proves every Windows symbol still has a definition, which `wincheck.sh` can't. `XBE_PORT=<port project>` adds
`xbe_port_check.exe`; `RUN=1` then starts `xbe_smoke.exe` under Wine. It writes `sdk/smoke/build-win`.

```
sudo dnf install mingw64-gcc-c++ mingw64-headers mingw64-crt mingw64-winpthreads-static wine
XBE_PORT=ports/rotk-xbox/rotkxbox RUN=1 sdk/smoke/wincross.sh
```

Under Wine `xbe_port_check.exe --no-dialogs` runs the real `wWinMain` as far as finding the game, and exits 2.

## Stand ins

Off Windows, `runtime/app/posix_pending.cpp` stands in for the Win32 app files the build leaves out
(`XBE_WIN32_ONLY_SOURCES` in `cmake/XbeRecomp.cmake`). Each stand in says what replaces it. Delete them as the real
code lands.
