# ROTK Xbox port, 12: first native Linux build and the M1 playtest (2026-11-25)

The US game (4541003E) built and played natively on Linux from `integration/m1`: Fedora 44 distrobox (clang 22), run
on the host's KDE Plasma 6 Wayland session, RTX 3080 Ti (NVIDIA 620.70.06, Vulkan 1.4.357), two 2560x1440
screens at 165 and 144 Hz. The user was at the computer throughout, so every run was windowed, the sound went to a
null sink, and nothing took the focus.

## Build

`build.sh linux-release` built first time: xbe_symbols skipped (config/game.toml has [symbols]), the recompile,
then 881 steps, 2 min 24 s on 16 threads, no errors or warnings. The exe is 22 MB and runs on the host as well as in
the container (only system libraries). Nothing in the SDK or the game sources needed a fix to build or link.

## How it was driven

- `sdk/smoke/vpad.py`: a virtual Xbox 360 pad through uinput, playing pad scripts or presses from a pipe. SDL 3.4
  sees it as "Xbox 360 Controller" with the standard mapping (checked: Y is north, X west).
- `kill -USR1 <pid>` (new, `main_sdl.cpp`): the game's own screenshot, so no capture holds other windows.
- Inside gamescope, `gamescopectl screenshot` captures the game's surface with the ImGui overlay.
- Sound: `pactl load-module module-null-sink`, `PIPEWIRE_NODE` for the game, `sdk/smoke/audio_level.sh` on its
  monitor.

## Results against M1's exit

| Item | Result |
| --- | --- |
| Builds from source | Yes, first time |
| Title screen and level 1 | Yes. First start: the HELMFMV1 film (not skippable on a first start, as on the console), then Helm's Deep. Pause menu, Exit To Menu, main menu, New Game, difficulty and back into Helm's Deep all work. |
| 60 fps in level 1 | Yes in play: every 5 s window 60.0 while fighting. A frame-time count (a local instrument in `vk.cpp`, not committed) over 100 s of fighting: worst frame 17 to 22 ms, one frame over 20 ms. The first 10 s after the film show frames up to 75 ms (the level loading). |
| Pad | Yes: moving, speed (A) and fierce (Y) attacks, kills (score 0 to 1350, level up), menus, rumble (the game's motor levels reach the pad), Back + Start for the in-game menu |
| Sound | Yes: film -9.9 dBFS rms, fighting -10.3 dBFS rms, no 100 ms block silent; in gamescope -8.8 dBFS |
| Saves | Partly. Settings changed from the F10 menu are saved (`menu: settings saved`) and the next run loads them; `achievements.txt` counts across runs; the game writes its title files to `hdd/E/UDATA/4541003e` at the first start. A game save needs Helm's Deep finished, which scripted input didn't do (the tutorial stayed at "Y Fierce" for 10 minutes of fierce attacks: it waits for the shield-bearing Uruk-hai, rotkx-13). |
| Window | Windowed, moved and resized (1280x720, 1920x1080) at 60 fps. Fullscreen and borderless on the desktop not tried: the user was working on both screens. |
| gamescope 1280x800 | Yes: host gamescope 3.17, windowed and borderless; borderless fills 1280x800 with the 4:3 picture pillarboxed, 60 fps, worst frame 21.7 ms. The F10 menu draws and pages there. |
| Vulkan validation | `XBE_VK_VALIDATE=1`, film, level start and fighting: no messages |
| Keyboard and mouse | Not tested: keys go to the focused window, which was the user's (done in M2, rotkx-13) |

## Problems found

1. **Fixed: the game crashed when gamescope ended.** Gamescope's Xwayland went with it; Xlib's own I/O error
   handler called `exit()`, whose destructors ran under the game's threads, and guest thread 14 crashed in
   `sub_001489EE` (crash log). `main_sdl.cpp` now handles the X server going away: a log line, then `_exit`.
2. **Fixed (test tool): rumble stalled the game thread.** A uinput pad that doesn't answer force feedback
   uploads holds the game's rumble ioctl for up to 30 s, on the game thread (`D` state in
   `uinput_request_submit`); the game froze and needed SIGKILL. `vpad.py` answers them. A real pad answers at once,
   but the rumble call is synchronous on the game thread, so a slow device would stall the game.
3. **Open: the Vulkan renderer logs no hitches.** Direct3D 12 logs frames of 50 ms or more with what they waited
   on (`d3d12.cpp` LogHitch); Vulkan writes only the 5 s average. That is all the release log has for Linux. A
   twin of LogHitch in `vk.cpp` would change the Windows Vulkan log too, so it was left for a decision.
4. **Open, the desktop's: frames of about 34 ms while the desktop was busy.** In some 5 s windows (55 to 59.7
   fps) the render thread's present time rose from 0.15 ms to 1.5 to 2.7 ms a frame, with its draw time unchanged:
   KWin held presents back while other windows on the same screens repainted. Quiet, none.
5. **Open, small: windowed placement on Wayland.** The log says "windowed on LG ULTRAGEAR" (the setting's screen),
   but KWin put the window on the other screen; a Wayland client can't place its window. The log line could say
   the screen is the compositor's choice.
6. **Open, small: the audio line names the default device** ("Built-in Audio Analog Stereo") when PipeWire
   routed the stream elsewhere.
7. **Seen, not a fault:** minimised, KWin throttles the window and the game runs at 29 fps (it keeps time).
   `hdd`, `settings.toml` and logs sit beside the exe, which is the M2 "where user data lives" decision.

## Not done

Helm's Deep to its end, a game save and its load, fullscreen and borderless on the desktop, keyboard and mouse, a
Steam Deck. Measuring with MangoHud didn't work (its CSV was never written).
