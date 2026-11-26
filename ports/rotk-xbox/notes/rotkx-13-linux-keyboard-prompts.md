# ROTK Xbox port, 13: keyboard and mouse on Linux, the key caps, and the Helm's Deep tutorial (2026-11-26)

Workstream M2-B on `integration/m2`. The US game (4541003E) built natively and run inside the host's headless
gamescope 3.17 (1280x800, borderless), driven by a virtual keyboard and mouse; the game-free checks through
`xbe_smoke`. The user was at the computer, so nothing went to their screen or their keyboard.

## How it was driven

- `sdk/smoke/vkbd.py`: keyboard and mouse scripts (vpad.py's format, the `[keyboard]` key names, `move dx dy`,
  `wheel n`) through XTest into gamescope's own Xwayland. A uinput keyboard, as first planned, would have typed into
  the user's window: headless gamescope reads no input devices, and KWin takes every new keyboard.
- The game's pid and gamescope's socket are taken from the game started, not `pgrep -x ROTKXbox` or
  `gamescopectl`'s default: other sessions ran their own games and gamescopes at the same time, and a broad signal
  or screenshot reached theirs.
- `xbe_smoke --window` logs the pad the keys and mouse make (`keys::Merge`) frame by frame; `xbe_smoke --keycaps`
  writes the key caps as PNGs, on Windows through GDI (also under Wine).

## Key caps

Off Windows `app::KeyCapPicture` returned false, so with the keyboard playing and no controller plugged in the hints
kept the Xbox discs. Now Kenney's pictures (CC0, already embedded) are used everywhere, which covers every default key
and most others; a key without a picture (the number pad's) gets a drawn cap whose text is rasterised with
stb_truetype (Dear ImGui's copy, `imstb_truetype.h`):

- Arial Bold if installed (msttcorefonts paths), else Liberation Sans Bold (OFL, Arial's metrics, installed on
  Fedora, Ubuntu and SteamOS), else the in-game menu's embedded Liberation Sans Regular (OFL, already in the binary,
  `licenses/LiberationSans-OFL.txt`) drawn bold through its signed distance field. `XBE_KEYCAP_FONT` names another
  font file, or `builtin` for the last.
- The layout is GDI's: the size the name fits in (from 0.62, 0.5 or 0.36 of the cap down in steps of 2 px to 0.78
  of the cap's width), the em as the size, the line as ascent plus descent, centred one pixel up. The cap itself
  (rounded face, edge, shadow) is the same code as Windows'.
- Windows keeps GDI; only the text's coverage moved into a function of its own.

Compared with `xbe_smoke.exe --keycaps` under Wine (GDI, Wine's stand-in for Arial): the same caps and layout; Wine's
substitute face is a little narrower, so some names come out a size larger there. With Liberation Sans Bold's metrics
the sizes match real Arial Bold. The built-in fallback reads close to the bold face.

In the game: "Speed" shows Kenney's left mouse button from the first frame of Helm's Deep, and with the parry and
fierce attacks moved to `NumpadSlash, Q` and `Numpad5, Mouse2` the hints showed drawn "Num/" and "Num5" caps
(`prompts: key caps' text in .../LiberationSans-Bold.ttf`).

Not changed: the front end's "X Help" icon stays the Xbox disc. It isn't one of the seven textures
`rotk_prompts.cpp` replaces (its hashes are the in-level hints'), on Windows either.

## Keyboard and mouse in the game

| Check | Result |
| --- | --- |
| Every default key (`xbe_smoke --window` in gamescope) | W S A D full tilt, LAlt+W half, Mouse1 and Enter A, LCtrl and Backspace B, Q X, Mouse2 Y, E black, Tab white, LShift LT, Space and Mouse3 RT, Esc Start, Grave Back, F5 left stick click, arrows the d-pad |
| Mouse aim (relative mouse mode, gamescope and Xvfb) | Nothing without LShift; with it held, the mouse holds the stick over and eases back. In Helm's Deep Gandalf's ranged aim follows the mouse left and right |
| Film | Enter or Esc skips HELMFMV1 |
| Fighting | Mouse1 speed and Mouse2 fierce attacks land (kills, a level up); NumpadSlash parries (the tutorial's parry step passed) |
| Pause menu | Esc opens and closes it, arrows move, Enter chooses (Exit To Menu, "are you sure", yes) |
| Front end | Enter and left click choose, Backspace and right click go back, the wheel moves up and down (skipping the greyed co-op entry), New Game, Normal, back into Helm's Deep; Exit on the main menu ends the game (`Exit (0)`) |
| F-keys | F1 hides and shows the HUD, `]` the next colour look, `\` a screenshot, F10 opens the in-game menu, PgUp/PgDn change its page, arrows move, Esc closes it (once not, see below) |
| `button_pictures = "auto"` | Key caps with the keyboard and no pad (`rotk: keyboard key pictures in the hints`) |
| A rebinding | `x = "NumpadSlash, Q"`, `y = "Numpad5, Mouse2"` in `settings.toml`: the new keys act and the hints show them |
| `keyboard.player = 2` | (`xbe_smoke`) The keys and mouse reach player 2's port only |

The wheel doesn't move the pause menu in a level: `mouse_menus` covers the front end (`InMenus`), as on Windows.

Seen twice, not reproduced: Esc didn't close the F10 menu, both times with the machine's load average at 19 to 32
and the game at 5 to 10 fps. A log of every key the overlay received then showed each Esc arriving and closing the
menu in eight more tries at normal load. If it comes back, look at XTest under load (gamescope passes it through
libei) before the game.

Nothing in `keyboard_sdl.cpp`, `main_sdl.cpp`, `rotk_controls.cpp` or `rotk_input.cpp` needed a change.

## The Helm's Deep tutorial ("Y Fierce")

Not a Linux bug. The prologue's hints go A Speed, X Parry, Y Fierce, then L Climb at the ladder. The fierce step
doesn't count presses or kills: its lines in `hel01.scx` are "Use fierce attacks against shields!", "Smash their
shields with fierce blows!", "Fiercely attack! Shields will break!", and its hint (`help_ctltri`) sits beside
`SarumanUrukShield` spawns tethered to a point (`NPC_TetherOn`, `Center PIW_Fir5`, `Radius`), a
`Trig_KillAll_FirstArea` trigger and `FierceOff`. So it waits for that group of shield-bearing Uruk-hai: they hold
their ground, speed attacks bounce off their shields, and fierce attacks break them.

On Linux, keyboard only: one run passed it after about 90 s of fierce attacks while turning about, and the hint went
to "L Climb" at the ladder; another spent nearly three minutes of fierce attacks (score 2040) among the ordinary Uruks near
the start without passing. M1's pad script turned on the spot and mixed speed attacks into every burst, so it may
never have reached the shield-bearers or broken their shields. To pass it: find the Uruk-hai with the tall shields,
use fierce attacks (Y, right mouse) on them until the shields break, and kill them.

The exact condition (the whole group, or the area cleared) isn't settled: the level script is compressed in the
scene file, and the GPU went down (an NVIDIA fault at 13:17, needing a reboot) before more runs.
