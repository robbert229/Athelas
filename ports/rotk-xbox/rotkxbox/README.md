# The Return of the King - PC port

An unofficial native PC port of the original Xbox game *The Lord of the Rings: The Return of the King* (US release), made by recompiling the game's own program for Windows and Linux (Steam Deck included) with [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp). It adds frame rates up to 240, widescreen and ultrawide, sharper text and menus, keyboard and mouse, and fixes for slowdowns and vanishing enemies at high frame rates.

> [!NOTE]
> No game files are included. You need your own copy of the Xbox game: a disc image (`.iso`) or its extracted files. This port is not affiliated with or endorsed by Electronic Arts, New Line Cinema, Warner Bros. or the Tolkien Estate.

## Playing

**With the Athelas launcher (recommended):** the `ROTK-Xbox` folder sits beside `Athelas.exe` in the Athelas download. Choose your game's folder or disc image in the launcher. The settings are on the same page as the PC version's (ROTK Fix), and are kept in its `ROTKFix.ini`.

**On its own:** put your game in the `assets` folder beside `ROTKXbox.exe` (the `.iso`, or `default.xbe` with the game's folders beside it) and run it. Settings are then in `settings.toml` beside the exe.

**On Linux and the Steam Deck:** a native build, no Proton or Wine. See [Linux and Steam Deck](#linux-and-steam-deck).

The US release of the game is supported. Another release is detected and you are asked before it starts.

## What it adds

**Display**
- Fullscreen, borderless or windowed, at any resolution.
- Widescreen and ultrawide, with a wider view rather than a stretched one. The HUD can keep its shape and sit at the screen's edges, and the Xbox's TV-safe border can be turned off.
- Frame rates of 30 (the original), 60, 90, 120, 144, 165, 180 and 240, with the game at its normal speed. Slowdowns and enemies vanishing at high frame rates are fixed.
- Anti-aliasing, with smooth edges on leaves, grass and fences, texture filtering and sharpening.
- Adjustable field of view, draw distance and brightness, and optional colour looks.

**Text and menus**
- Sharper text and menus: if you also have The Return of the King (PC), its larger fonts and title and menu pictures are used. Not required.

**Movies**
- Any video can be skipped, and the opening logos can be skipped altogether.

**Controls**
- Keyboard and mouse, as well as a controller (see below). With no controller plugged in, the game's hints show the keys.
- Xbox, PlayStation and Switch controllers, with rumble, button remapping and the right button pictures in the game's hints.
- Two-player co-op on one PC, with two controllers or a controller and the keyboard.

**In-game settings menu**
- `F10`, or Back + Start on a controller, opens a settings menu over the game, which stands still behind it.
- It holds the settings that take effect at once (picture, controls, field of view, draw distance) and the achievements list. It saves them when it closes (`F10`, `Esc`, B or Start). LB and RB, or Page Up and Page Down, change its page.

**In the background**
- "Pause in the background" stops the game, picture and sound, while another window is in front, and carries on where it was when you come back.
- "Mute in the background" only silences it. Both are off by default.

**Gameplay**
- Damage you take, damage enemies take, and every level and character unlocked. All off by default.

**Achievements**
- 29 achievements, the same as the PC version's: every level, every hero, co-op, a hero at level 10, enemies defeated. They are earned as you play, and what your saves already hold counts too, so an old save earns the levels it has finished the first time you play.
- A note shows in the bottom right corner when you earn one ("Achievement notes" setting). The list, earned and not, is in the launcher and on the in-game menu's Achievements page.
- They are kept in `achievements.txt` beside the log, and only on this PC; nothing is sent anywhere.

**Other**
- Texture packs, and saving the game's textures to make them.
- **Boot splash:** a `splash.png` (or `.bmp`) beside the exe is shown at launch.

## Keys

| Key | What it does |
|---|---|
| `F10` | Settings menu (the game pauses) |
| `\` (above Enter) | Screenshot, saved as a PNG in `Screenshots` |
| `]` | Next colour look |
| `[` | Sharpening off and on, to compare |

## Keyboard and mouse

The keys are the PC version's. Each one can be changed on the launcher's Controls tab: click a move, then press the key or mouse button.

| Key | What it does |
|---|---|
| `W` `A` `S` `D` | Move (hold Left Alt to walk) |
| Left mouse, `Enter` | Speed attack; in the menus, `Enter` goes on (and "press start") |
| Right mouse | Fierce attack |
| `Space`, middle mouse | Killing move |
| `Q` | Parry |
| Left Ctrl, Backspace | Physical attack (knock back), and back in menus |
| Left Shift | Ranged attack |
| `Tab` | Jump back |
| `E` | Action |
| `F5` | Special ability |
| `Esc` | Pause; in the menus, back |
| Arrow keys | Menus |

## Known issue

The picture is darker than the PC version's because it keeps the Xbox's brightness curve. Raise Brightness to about 1.2 to 1.3 to match.

## Requirements

- Windows 10 or 11, 64-bit, and a graphics card that supports Direct3D 12.
- Or Linux, natively: an x86-64-v2 processor (SSE4.2: Intel from 2008, AMD from 2011), glibc 2.35 or newer (Ubuntu
  22.04, Fedora 36, Debian 12, SteamOS 3 and later) and a Vulkan 1.2 driver (Mesa or NVIDIA's). An X11 or Wayland
  desktop, or gamescope.
- A keyboard and mouse or a controller.

## Linux and Steam Deck

The Linux release is two downloads of the same game; take either:

- `ROTKXbox-<version>-linux-x86_64.tar.xz`: extract it (`tar -xf ...`), put the game in a folder named `assets` inside
  it (or the `.iso` in the folder itself), and start `./rotkxbox.sh`.
- `ROTKXbox-<version>-x86_64.AppImage`: one file. Make it executable (`chmod +x`), put the game beside it (an `.iso`,
  or a folder named `assets`) and start it. It needs FUSE, which desktops and SteamOS have; where it says it cannot
  mount, start it with `--appimage-extract-and-run`, or use the `.tar.xz`.

Either can be started with `--game <folder or .iso>` (or `ROTKXBOX_GAME`) when the game is elsewhere, and
`--install-desktop` puts it in the desktop's menu (`--remove-desktop` takes it out). Nothing else needs installing
beyond the graphics driver: if the game says it found no Vulkan, install `mesa-vulkan-drivers` (Ubuntu, Debian,
Fedora) or `vulkan-radeon` / `vulkan-intel` (Arch).

**Where its files go.** Settings (`settings.toml`), saves, logs, the shader cache, screenshots and texture packs go to
`~/.local/share/athelas/rotk-xbox` (`$XDG_DATA_HOME/athelas/rotk-xbox`), the same for both downloads. `ROTKXBOX_DATA`
moves them; an empty `portable.txt` beside the AppImage, or in the tarball's folder, keeps them there instead. Any of
`--config`, `--saves`, `--log`, `--shader-cache`, `--screenshots` or `--textures` given on the command line wins.

**Steam Deck**, natively, next to the Proton way of running Athelas:
1. In Desktop Mode, extract the `.tar.xz` (Ark opens it), for example into `/home/deck/Games/ROTKXbox`, and put your
   game there as above. The game can also be on an SD card: start it with `--game <path>` (step 2's launch options).
2. In Steam: **Games > Add a Non-Steam Game to My Library**, browse to `rotkxbox.sh` (or the AppImage) and add it.
   Leave **Properties > Compatibility** off: no Proton. (`./rotkxbox.sh --install-desktop` first puts it in the list
   of programs Steam offers.)
3. Start it in Game Mode. The Deck's controls are an Xbox controller, and it fills the Deck's 1280x800 screen.
4. Frame rate: set the game's own **Frame rate** to 60 (`F10`, or Back + Start, opens the settings). The game keeps
   its normal speed at any rate it gets, so the Deck's frame limit (**... > Performance**) can then hold it at 40 to
   save battery; 30 is the console's.

Saves on the Deck are in `/home/deck/.local/share/athelas/rotk-xbox/saves`; back that folder up to keep them.

## Files it writes

From Athelas: `ROTK-Xbox\saves` (save games), `ROTK-Xbox\rotkx.log` and `ROTK-Xbox\achievements.txt`, with the settings in `ROTKFix.ini`. On its own, beside the exe: `settings.toml`, `hdd` (save games), `shadercache` (safe to delete; it refills), `logs`, `textures` and `Screenshots`. On Linux the same files go to `~/.local/share/athelas/rotk-xbox` (above). Your game files are only read, never changed.

## If something goes wrong

The log says why the game stopped. A crash also leaves a `.dmp` file in `CrashDumps` beside the log (on Linux, a crash log in `logs/CrashDumps`). Include both when you report a problem.

## Building

1. Put the extracted game files in `assets`: `default.xbe` with the game's folders beside it.
2. Install Visual Studio 2022 or newer with **Desktop development with C++**, **C++ Clang tools for Windows** and **C++ CMake tools for Windows** (`build.bat` offers to add the last two), and [Git](https://git-scm.com/).
3. Run `build.bat`. The first build recompiles the game and takes a while. The exe is written to `out\build\release`.
4. `release.bat` builds, then makes `release\ROTKXbox`, the folder that Athelas ships as `ROTK-Xbox`.

The recompiled code in `generated` is made from your own game and is never committed.

### Linux

`build.sh` is the Linux twin of `build.bat` (clang, lld, cmake, ninja, git and Python 3; the exe goes to
`out/build/linux-release`). `release.sh` is the twin of `release.bat`: it builds in an Ubuntu 22.04 container through
podman (`linux/Containerfile`), so the result runs on any distribution from 2022 on and on the Steam Deck, and writes
to `release/` the AppImage, the `.tar.xz` (each with its `.sha256`) and the exe's debug information (`.debug`). Both
start through `linux/rotkxbox.sh` (the AppImage's AppRun), which picks the data folder and finds the game. Neither
holds any game files.

## Credits

By Shagg, with Claude (Anthropic).

- [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp), the recompiler and runtime this is built on.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for the overridable function layout and instruction hooks.
- [Zydis](https://github.com/zyantific/zydis), used by the recompiler to read the game's code.
- [toml++](https://github.com/marzer/tomlplusplus), [Dear ImGui](https://github.com/ocornut/imgui), [SDL](https://github.com/libsdl-org/SDL) and [stb](https://github.com/nothings/stb), built into the exe.

The port is MIT licensed. The licences of the parts in the exe are in `THIRD_PARTY.txt` and `LICENSE.txt` (the runtime's MIT licence).
