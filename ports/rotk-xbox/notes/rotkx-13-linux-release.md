# ROTK Xbox port, 13: the Linux release on clean distributions (2026-11-26)

M2-D: `release.sh` (was `appimage.sh`) makes the AppImage and `ROTKXbox-<version>-linux-x86_64.tar.xz` from one
build in its Ubuntu 22.04 container. Both start through `linux/rotkxbox.sh`, so they find the game and keep their data
the same way: `$XDG_DATA_HOME/athelas/rotk-xbox` (`~/.local/share/athelas/rotk-xbox`), `ROTKXBOX_DATA` to move it, a
`portable.txt` beside the AppImage or in the tarball's folder to keep it there.

## Clean-distribution runs

Each in a podman container of the distribution's own image with nothing added but its Mesa Vulkan driver
(llvmpipe), Xvfb and xdotool; the game mounted read-only, the release read-only, the home empty but for a
`settings.toml` of three lines (windowed 640x480, `skip_all_videos`: llvmpipe holds 30 fps at that size). Driven by
keys through xdotool: Enter skips the first start's Helm's Deep film, then in Helm's Deep Esc, Exit To Menu, yes,
and the main menu (Load Game, New Game) is shot by SIGUSR1. A first start (no saved game) always opens with that film
and the level, never the title (`rotk_movies.cpp`), so the main menu is reached through the pause menu.

| Distribution | glibc | Vulkan | tarball | AppImage |
| --- | --- | --- | --- | --- |
| Ubuntu 22.04.5 | 2.35 | llvmpipe, LLVM 15, Vulkan 1.3.255 | main menu | main menu (extract-and-run: no FUSE in the image) |
| Fedora 44 | 2.43 | llvmpipe, LLVM 22, Vulkan 1.4.354 | main menu | main menu (extract-and-run) |
| Arch (SteamOS stand-in) | 2.44 | llvmpipe, LLVM 23, Vulkan 1.4.363 | main menu | main menu, mounted through FUSE (`/dev/fuse`, fuse3) |

Every run exited 0 on SIGTERM. With no sound server in a container SDL finds no audio driver and the game logs
`audio: SDL audio unavailable (...), no sound` and plays on. Ubuntu's image has no `fusermount`, so the AppImage
there says "Cannot mount AppImage, please check your FUSE setup"; desktops and SteamOS have it, and the docs say
`--appimage-extract-and-run` or the tarball otherwise.

On this machine's GPU (RTX 3080 Ti, headless gamescope 1280x800) the AppImage starts borderless at 60 fps and logs
`audio: SDL pipewire, Built-in Audio Analog Stereo, ...`: the device name is there, though it is the default device's
even when `PIPEWIRE_NODE` sends the stream elsewhere (rotkx-12's item 6, still open).

Found and fixed on the way: the start script never found an `.iso` beside the AppImage when no `.ISO` was there too
(`ls` of a pattern that matched nothing failed the test); M1's AppRun had the same line.

## M1 leftovers

- **Vulkan hitch lines** (`gpu/vk.cpp` LogHitch): a frame of 50 ms or more, as Direct3D 12 writes it, with the
  shaders, pipelines and textures made, the GPU waits and the swap time, and the game threads' reads, waits and
  sleeps. Seen on llvmpipe, where the swap carries the wait: `hitch: frame 67 ms | shaders 0 (0 ms), pipelines 0
  (0 ms), textures 0 new (0 ms), GPU waits 0 ms, swap 66 ms, game and other 0 ms`. Windows' Vulkan log gets them too.
- **Screen choice on Wayland**: checked in a nested KWin (`kwin_wayland --virtual --output-count 2`) with
  `xbe_smoke --vulkan`. `main`, `DISPLAY2`, `2` (taken for a name before; now the screen's number) and the screen's own
  name (`Virtual-1`) each put the borderless and the fullscreen window on the right output; an unknown name falls back
  to the main one with a log line. Windowed, KWin places the window itself, and the log line now says so.
- **Audio device name**: logged (above).

## Steam Deck: a manual check list

The Deck can't be reached from the build machine. On a Deck with SteamOS 3:

1. Desktop Mode: extract the `.tar.xz` into `/home/deck/Games/ROTKXbox` with Ark; put the game in its `assets`.
   Run `./rotkxbox.sh --install-desktop` from Konsole; the game is in the menu under Games.
2. Start it from the menu in Desktop Mode: the window opens, sound plays, the Deck's controls move Gandalf.
3. Steam > Add a Non-Steam Game: "ROTK Xbox PC port" is in the list. Add it, leave Compatibility off.
4. Game Mode: it starts fullscreen at 1280x800 (log: `display: borderless on gamescope, 1280x800`), the controls
   work without Steam Input changes, the Steam button's overlay opens and closes over it.
5. In the game's F10 menu set Frame rate 60: the overlay's frame counter shows 60. Then the Deck's frame limit 40
   (... > Performance): 40, and the game plays at its normal speed (walking and fighting look as at 60, not
   slowed).
6. Play into Helm's Deep, then Steam button > Exit Game: it ends cleanly (log ends `Exit (0)`).
7. Sleep the Deck (power button) in Helm's Deep for a minute and wake it: the game carries on, with sound.
8. The AppImage too: chmod +x, game beside it, added as a non-Steam game, starts in Game Mode.
9. The game on the SD card: launch option `--game /run/media/deck/<card>/ROTK` (or the .iso there).
10. Files: after the first start `/home/deck/.local/share/athelas/rotk-xbox/saves/E/UDATA/4541003e` holds the
    title files, with `logs/rotkx.log` and `settings.toml` beside `saves`; a finished Helm's Deep saves there.
