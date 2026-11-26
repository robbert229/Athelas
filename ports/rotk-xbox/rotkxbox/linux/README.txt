The Return of the King (Xbox) - native Linux port
=================================================
By Shagg, with Claude (Anthropic). Part of Athelas.

An unofficial native port of the original Xbox game The Lord of the Rings:
The Return of the King (US release), recompiled to run on Linux and the
Steam Deck without Proton. No game files are included: you need your own US
Xbox copy, as a disc image (.iso) or its extracted files (default.xbe with
the game's folders beside it). Not affiliated with or endorsed by Electronic
Arts, New Line Cinema, Warner Bros. or the Tolkien Estate.


REQUIREMENTS
------------
- 64-bit Linux on an x86-64-v2 processor (with SSE4.2: Intel from 2008 on,
  AMD from 2011 on), with glibc 2.35 or newer: Ubuntu 22.04, Fedora 36,
  Debian 12, SteamOS 3 or newer, and most distributions from 2022 on.
- A Vulkan 1.2 driver: Mesa (AMD, Intel) or NVIDIA's own driver. Most
  distributions install it with the graphics driver; if the game says it
  found no Vulkan, install mesa-vulkan-drivers (Ubuntu, Debian, Fedora) or
  vulkan-radeon / vulkan-intel (Arch).
- An X11 or Wayland desktop, or gamescope (the Steam Deck's Game Mode).
Nothing else needs installing: the rest is built into the program.


RUNNING IT
----------
1. Extract the .tar.xz into a folder of its own:
     tar -xf ROTKXbox-<version>-linux-x86_64.tar.xz
2. Put your game in a folder named assets inside it (default.xbe at the top
   of assets), or put the .iso in the folder itself.
3. Start ./rotkxbox.sh in that folder.
   ./rotkxbox.sh --install-desktop adds it to the desktop's menu (and so to
   Steam's "Add a Non-Steam Game" list); --remove-desktop takes it out.

The game can be elsewhere: ./rotkxbox.sh --game /path/to/folder-or.iso, or
set ROTKXBOX_GAME. The AppImage (ROTKXbox-<version>-x86_64.AppImage) is the
same game in one file: make it executable, then put the game beside it. It
needs FUSE (fusermount3), which desktops and SteamOS have; where it says it
"cannot mount", start it with --appimage-extract-and-run, or use the .tar.xz.


WHERE ITS FILES GO
------------------
Settings (settings.toml), saves, logs, the shader cache, screenshots and
texture packs go to ~/.local/share/athelas/rotk-xbox ($XDG_DATA_HOME if set).
To move them, set ROTKXBOX_DATA to another folder. To keep everything in this
folder instead (on a USB stick, say), put an empty file named portable.txt
here. Your game files are only read, never changed.


STEAM DECK
----------
In Desktop Mode, extract it (Ark opens .tar.xz files), for example into
/home/deck/Games/ROTKXbox, and put your game there. Then in Steam: Games >
"Add a Non-Steam Game to My Library" and choose rotkxbox.sh (or the AppImage).
Don't set a Proton version for it: it runs natively. In Game Mode the Deck's
controls work as an Xbox controller, at the Deck's 1280x800. Set the game's
frame rate to 60 or 30 in its settings (F10 or Back + Start); the Deck's own
frame limit (the ... button > Performance) can then hold it lower to save
battery. Saves are in /home/deck/.local/share/athelas/rotk-xbox: back that
folder up with your game saves.


KEYS
----
F10 or Back + Start: settings menu. \ (above Enter): screenshot. The full
key list is in the port's README (ports/rotk-xbox/rotkxbox/README.md on
GitHub).


IF SOMETHING GOES WRONG
-----------------------
The log (logs/rotkx.log in the folder above) says why the game stopped. A
crash also leaves a crash log in logs/CrashDumps. Include both when you
report a problem: https://github.com/Shagg88/Athelas/issues


LICENCES
--------
The port is MIT licensed. LICENSE.txt is the recompiler runtime's licence;
THIRD_PARTY.txt holds the licences of the libraries built into the program.
