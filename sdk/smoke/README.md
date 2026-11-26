# Smoke test

`xbe_smoke` is the SDK runtime linked and started with no game. It checks that the host side of a port (kernel,
XDK, renderer, sound, window) builds and starts on a platform before any game has been recompiled there. The
recompiled code is a stand in (`cmake/XbeStandIn.cmake`): every XDK symbol the SDK's signatures name is absent, and
every recompiled function traps.

Alone it starts logging, the settings file and guest memory, then exits 0. `--window` opens the game window and logs
what reaches the game from it, with a paced frame thread (`--fps`), and the keyboard player's controller as the keys
and the mouse make it, whenever it changes. `--vulkan` opens the window and draws N frames
(`--frames N`, 0 until the window is closed) through the real Vulkan backend: a clear and three triangles through
`Backend::Draw` (fixed-function state, shaders through glslang), shown at the console's 4:3 with the ImGui overlay, paced at `--fps` too (with v-sync, the screen's refresh is the
most).
`--audio` plays a 440/660 Hz tone through the platform audio output, alone for N / 60 seconds or while the frames are
drawn. It exits 1 when the renderer doesn't start or the output pulls less than half real time. `--display-change`
(with `--vulkan`) pushes a display mode change and a window HDR change twice a second, as SDL sends them when a screen
is plugged in, while the frames read the desktop mode the window thread keeps. `--splash` (with `--window` or `--vulkan`) shows
the boot splash first, `smoke-splash.png` or `.bmp` beside the exe, and opens the window behind it at the first
frame, as a game's. `--discord <application id>` (with `--window`) turns the Discord status on for the run, the frame count as
its state, and off again for the last quarter of the frames; a stand-in server shows what reaches Discord's socket.

## Build and run (Linux)

```
cmake -S sdk/smoke -B sdk/smoke/build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
ninja -C sdk/smoke/build xbe_smoke
cd sdk/smoke/build && ./xbe_smoke [--window] [--vulkan] [--audio] [--frames N] [--fps N] [--display-change] [--splash] [--discord ID]
```

Useful with `--vulkan` and `--audio`: `XBE_VK_VALIDATE=1` (or `sync`) logs the Khronos validation layer's errors, or
`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` prints its warnings too; `SDL_VIDEO_DRIVER=x11` or `wayland`;
`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json` for llvmpipe; `SDL_AUDIO_DRIVER=disk` writes the sound
to `sdlaudio.raw` (float stereo, 48 kHz) to check it. A frame rate measured on Wayland needs the screens on: with them off (DPMS,
`kscreen-doctor --dpms show`) KWin paces every window at about 20 Hz, `vkcube` too.

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

## The Win32 app files off Windows

Off Windows the build leaves out the Win32 app files (`XBE_WIN32_ONLY_SOURCES` in `cmake/XbeRecomp.cmake`) and builds
their SDL3 and POSIX twins instead (`XBE_SDL_APP_SOURCES`): `main_sdl.cpp`, `splash_sdl.cpp`, `netplay_window_sdl.cpp`
and the rest. No stand ins are left.

## Playing a native build from a script

`vpad.py` plugs in a virtual Xbox 360 controller through uinput (`python3-evdev`, write access to `/dev/uinput`) and
plays the harness's pad scripts on it (`sdk/harness/README.md`), a frame being 1/60 s; `--fifo <pipe>` then keeps it
plugged in and presses what is written to the pipe (`A 8`, `LS+X 20`, `wait 30`). It answers the game's rumble and
logs it. Off Windows `kill -USR1 <pid>` takes a screenshot, as the backslash key does, into `Screenshots`.

`vkbd.py` is its keyboard and mouse twin: the same scripts with the `[keyboard]` settings' key names (`W+LAlt 30`,
`Mouse2 8`, `Numpad5`, `F10`), `move dx dy [frames]` for relative mouse movement and `wheel n`, through XTest into one
X server (`--display`): gamescope's own Xwayland (the game's `DISPLAY` there, in `/proc/<pid>/environ`) or Xvfb. A
uinput keyboard won't do: headless gamescope reads no input devices, and the desktop's compositor takes every new
keyboard and types into its focused window. `gamescopectl screenshot` takes the game with the in-game menu; give it
`GAMESCOPE_WAYLAND_DISPLAY` from the game's environment, or it reaches whichever gamescope holds the default socket.

`audio_level.sh <seconds> [sink]` records a sink's monitor and prints its level. With a null sink of its own
(`pactl load-module module-null-sink sink_name=xbe_test`, then `PIPEWIRE_NODE=xbe_test` for the game) it measures the
game alone, and nothing reaches the speakers.
