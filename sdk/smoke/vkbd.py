#!/usr/bin/env python3
"""vkbd.py: a virtual keyboard and mouse for playing a port's native build from a script, vpad.py's twin.

  vkbd.py [--display :N] [--fps N] [--delay S] [--fifo PATH] [script ...]

The keys and the mouse go through XTest into one X server: gamescope's own Xwayland (the game's DISPLAY inside it),
or Xvfb. Nothing reaches the desktop. (A uinput keyboard can't be used for this: headless gamescope reads no input
devices, and the desktop's compositor takes every new keyboard and types into its focused window.)

Scripts are vpad.py's format, a frame being 1/fps s: lines of "frame keys [frames held]" (8 by default), # comments,
keys joined with +, named as the [keyboard] settings name them (W, Space, LCtrl, Mouse1 ... Mouse5, F10, Esc, Grave,
Backslash, BracketLeft, BracketRight, Numpad0 ...), or "frame move dx dy [frames]": the mouse moved that far, spread
over the frames (relative motion, as relative mouse mode reads it), or "frame wheel n": n notches (up is positive).
With --fifo the same lines without the frame come from a pipe once the scripts end, "wait frames" waits, "quit" ends.
Needs libX11 and libXtst.
"""
import argparse
import ctypes
import ctypes.util
import os
import sys
import time

# the settings' key names (core/keys.h) and the ports' hotkeys, as X keysyms
KEYSYMS = {
    **{chr(c): 0x61 + c - ord("A") for c in range(ord("A"), ord("Z") + 1)},
    **{str(d): 0x30 + d for d in range(10)},
    **{f"F{n}": 0xFFBE + n - 1 for n in range(1, 13)},
    "Space": 0x20, "Enter": 0xFF0D, "Esc": 0xFF1B, "Tab": 0xFF09, "Backspace": 0xFF08, "CapsLock": 0xFFE5,
    "LShift": 0xFFE1, "RShift": 0xFFE2, "LCtrl": 0xFFE3, "RCtrl": 0xFFE4, "LAlt": 0xFFE9, "RAlt": 0xFFEA,
    "Up": 0xFF52, "Down": 0xFF54, "Left": 0xFF51, "Right": 0xFF53,
    "Insert": 0xFF63, "Delete": 0xFFFF, "Home": 0xFF50, "End": 0xFF57, "PageUp": 0xFF55, "PageDown": 0xFF56,
    **{f"Numpad{d}": 0xFFB0 + d for d in range(10)},
    "NumpadStar": 0xFFAA, "NumpadPlus": 0xFFAB, "NumpadMinus": 0xFFAD, "NumpadDot": 0xFFAE, "NumpadSlash": 0xFFAF,
    "Minus": 0x2D, "Equals": 0x3D, "Comma": 0x2C, "Period": 0x2E, "Slash": 0x2F, "Semicolon": 0x3B,
    "Apostrophe": 0x27, "Grave": 0x60, "Backslash": 0x5C, "BracketLeft": 0x5B, "BracketRight": 0x5D,
}
BUTTONS = {"Mouse1": 1, "Mouse3": 2, "Mouse2": 3, "Mouse4": 8, "Mouse5": 9}  # X's: 2 is the middle one


class X:
    def __init__(self, display):
        x11 = ctypes.CDLL(ctypes.util.find_library("X11") or "libX11.so.6")
        xtst = ctypes.CDLL(ctypes.util.find_library("Xtst") or "libXtst.so.6")
        x11.XOpenDisplay.restype = ctypes.c_void_p
        x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        x11.XFlush.argtypes = [ctypes.c_void_p]
        x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
        for f in ("XTestFakeKeyEvent", "XTestFakeButtonEvent"):
            getattr(xtst, f).argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
        xtst.XTestFakeRelativeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
        self.x11, self.xtst = x11, xtst
        self.dpy = x11.XOpenDisplay(display.encode() if display else None)
        if not self.dpy:
            sys.exit(f"vkbd: can't open the X display {display or os.environ.get('DISPLAY')}")
        self.down = set()

    def press(self, name, down):
        if name in BUTTONS:
            self.xtst.XTestFakeButtonEvent(self.dpy, BUTTONS[name], int(down), 0)
        elif name in KEYSYMS:
            code = self.x11.XKeysymToKeycode(self.dpy, KEYSYMS[name])
            if not code:
                print(f"vkbd: no keycode for {name}", file=sys.stderr)
                return
            self.xtst.XTestFakeKeyEvent(self.dpy, code, int(down), 0)
        else:
            print(f"vkbd: unknown key {name}", file=sys.stderr)
            return
        (self.down.add if down else self.down.discard)(name)

    def set(self, names):
        """holds exactly these keys and buttons"""
        for n in sorted(self.down - names):
            self.press(n, False)
        for n in sorted(names - self.down):
            self.press(n, True)
        self.x11.XFlush(self.dpy)

    def move(self, dx, dy):
        if dx or dy:
            self.xtst.XTestFakeRelativeMotionEvent(self.dpy, int(dx), int(dy), 0)
            self.x11.XFlush(self.dpy)

    def wheel(self, n):
        b = 4 if n > 0 else 5
        for _ in range(abs(int(n))):
            self.xtst.XTestFakeButtonEvent(self.dpy, b, 1, 0)
            self.xtst.XTestFakeButtonEvent(self.dpy, b, 0, 0)
        self.x11.XFlush(self.dpy)

    def close(self):
        self.set(set())
        self.x11.XCloseDisplay(self.dpy)


def parse(words):
    """one press without its frame: (kind, value, frames)"""
    if words[0] == "move":
        return ("move", (float(words[1]), float(words[2])), float(words[3]) if len(words) > 3 else 1.0)
    if words[0] == "wheel":
        return ("wheel", int(words[1]), 1.0)
    return ("keys", set(filter(None, words[0].split("+"))), float(words[1]) if len(words) > 1 else 8.0)


def load(path):
    presses = []
    with open(path) as f:
        for line in f:
            p = line.split()
            if not p or p[0].startswith("#"):
                continue
            presses.append((float(p[0]),) + parse(p[1:]))
    return presses


def play(x, presses, fps, t0, log):
    """plays one script from t0 (monotonic seconds); returns the time its last press ended"""
    if not presses:
        return t0
    end = max(at + held for at, _, _, held in presses)
    told, moved = set(), {}
    while True:
        frame = (time.monotonic() - t0) * fps
        held = set()
        for i, (at, kind, v, n) in enumerate(presses):
            if frame < at:
                continue
            if i not in told:
                told.add(i)
                log(f"frame {at:.0f}: {kind} {v if kind != 'keys' else '+'.join(sorted(v))} {n:.0f}")
                if kind == "wheel":
                    x.wheel(v)
            if kind == "keys" and frame <= at + n:
                held |= v
            elif kind == "move":
                # spread over its frames: what is due by now, less what was sent
                f = min(1.0, (frame - at) / max(n, 1.0))
                sx, sy = moved.get(i, (0, 0))
                dx, dy = round(v[0] * f) - sx, round(v[1] * f) - sy
                x.move(dx, dy)
                moved[i] = (sx + dx, sy + dy)
        x.set(held)
        if frame > end:
            return time.monotonic()
        time.sleep(0.25 / fps)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--display", help="the X display (default $DISPLAY)")
    ap.add_argument("--fps", type=float, default=60)
    ap.add_argument("--delay", type=float, default=0, help="seconds before the first script's frame 0")
    ap.add_argument("--fifo", help="a pipe to read live presses from once the scripts have played")
    ap.add_argument("scripts", nargs="*")
    a = ap.parse_args()
    start = time.monotonic()

    def log(msg):
        print(f"vkbd {time.monotonic() - start:7.2f}s {msg}", flush=True)

    x = X(a.display)
    log(f"on {a.display or os.environ.get('DISPLAY')}")
    try:
        t = time.monotonic() + a.delay
        for s in a.scripts:
            log(f"script {s}")
            t = play(x, load(s), a.fps, t, log)
        if a.fifo:
            if not os.path.exists(a.fifo):
                os.mkfifo(a.fifo)
            log(f"reading {a.fifo}")
            while True:
                with open(a.fifo) as f:  # blocks until a writer opens it
                    for line in f:
                        p = line.split()
                        if not p or p[0].startswith("#"):
                            continue
                        if p[0] == "quit":
                            return
                        if p[0] == "wait":
                            time.sleep(float(p[1]) / a.fps)
                            continue
                        log(f"live: {line.strip()}")
                        play(x, [(0.0,) + parse(p)], a.fps, time.monotonic(), lambda m: None)
                        x.set(set())
    except KeyboardInterrupt:
        pass
    finally:
        x.close()
        log("done")


if __name__ == "__main__":
    main()
