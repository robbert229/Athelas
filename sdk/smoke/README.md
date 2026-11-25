# Smoke test

`xbe_smoke` is the SDK runtime linked and started with no game. It checks that the host side of a port (kernel,
XDK, renderer, sound, window) builds and starts on a platform before any game has been recompiled there. The
recompiled code is a stand in (`cmake/XbeStandIn.cmake`): every XDK symbol the SDK's signatures name is absent, and
every recompiled function traps.

Today it starts logging, the settings file and guest memory, then exits 0. `--window` and `--frames N` are parsed
for the window and renderer work to wire up (open the window, draw N frames through Vulkan, play sound).

## Build and run (Linux)

```
cmake -S sdk/smoke -B sdk/smoke/build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
ninja -C sdk/smoke/build xbe_smoke
cd sdk/smoke/build && ./xbe_smoke [--window] [--frames N]
```

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

## Stand ins

Off Windows, `runtime/app/posix_pending.cpp` stands in for the Win32 app files the build leaves out
(`XBE_WIN32_ONLY_SOURCES` in `cmake/XbeRecomp.cmake`). Each stand in says what replaces it. Delete them as the real
code lands.
