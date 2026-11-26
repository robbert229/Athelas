#!/usr/bin/env python3
"""vpad.py: a virtual Xbox 360 controller through uinput, driven by the harness's pad scripts, for playing a port's
native build from a script.

  vpad.py [--fps N] [--delay S] [--fifo PATH] [--product ID] [script ...]

Each script is the harness's format (sdk/harness/README.md): lines of "frame buttons [frames held]" (held 8 frames by
default), # comments, buttons joined with +: UP DOWN LEFT RIGHT START BACK A B X Y BLACK WHITE LT RT LS LSD LSL LSR
RSU RSD RSL RSR. A frame here is 1/fps seconds (default 60) from the controller's creation plus --delay. Scripts play
one after the other. With --fifo the controller then stays plugged in and plays lines from that pipe as they come:
"buttons [frames held]" pressed at once, or "wait frames". Without it the controller is unplugged when the scripts
end. --product gives the pad another Microsoft product id (0x028E, the wired 360 pad, by default), so that two game
instances can each take their own: SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x045e/<id> in each game's environment.
Needs python3-evdev and write access to /dev/uinput.
"""
import argparse
import os
import sys
import threading
import time

from evdev import AbsInfo, UInput, ecodes as e

BUTTONS = {
    "A": e.BTN_SOUTH, "B": e.BTN_EAST, "X": e.BTN_NORTH, "Y": e.BTN_WEST,  # xpad's codes: X is BTN_NORTH
    "WHITE": e.BTN_TL, "BLACK": e.BTN_TR, "BACK": e.BTN_SELECT, "START": e.BTN_START,
    "GUIDE": e.BTN_MODE, "LCLICK": e.BTN_THUMBL, "RCLICK": e.BTN_THUMBR,
}
HAT = {"UP": (e.ABS_HAT0Y, -1), "DOWN": (e.ABS_HAT0Y, 1), "LEFT": (e.ABS_HAT0X, -1), "RIGHT": (e.ABS_HAT0X, 1)}
TRIGGER = {"LT": e.ABS_Z, "RT": e.ABS_RZ}
# sticks: evdev's y is down, as SDL's
STICK = {"LS": (e.ABS_Y, -32767), "LSD": (e.ABS_Y, 32767), "LSL": (e.ABS_X, -32767), "LSR": (e.ABS_X, 32767),
         "RSU": (e.ABS_RY, -32767), "RSD": (e.ABS_RY, 32767), "RSL": (e.ABS_RX, -32767), "RSR": (e.ABS_RX, 32767)}
AXES = [e.ABS_X, e.ABS_Y, e.ABS_RX, e.ABS_RY, e.ABS_Z, e.ABS_RZ, e.ABS_HAT0X, e.ABS_HAT0Y]


def make_pad(product=0x028E):
    stick = AbsInfo(0, -32768, 32767, 16, 128, 0)
    trig = AbsInfo(0, 0, 255, 0, 0, 0)
    hat = AbsInfo(0, -1, 1, 0, 0, 0)
    caps = {
        e.EV_KEY: list(BUTTONS.values()),
        e.EV_ABS: [(e.ABS_X, stick), (e.ABS_Y, stick), (e.ABS_RX, stick), (e.ABS_RY, stick),
                   (e.ABS_Z, trig), (e.ABS_RZ, trig), (e.ABS_HAT0X, hat), (e.ABS_HAT0Y, hat)],
        e.EV_FF: [e.FF_RUMBLE],
    }
    # the wired Xbox 360 controller's ids, as the kernel's xpad driver gives them: SDL knows its mapping
    return UInput(caps, name="Microsoft X-Box 360 pad", vendor=0x045E, product=product, version=0x0110,
                  bustype=e.BUS_USB, max_effects=16)


def state_of(buttons):
    """the evdev state for a set of names joined with +: {(type, code): value}"""
    s = {}
    for b in filter(None, buttons.split("+")):
        if b in BUTTONS:
            s[(e.EV_KEY, BUTTONS[b])] = 1
        elif b in HAT:
            s[(e.EV_ABS, HAT[b][0])] = HAT[b][1]
        elif b in TRIGGER:
            s[(e.EV_ABS, TRIGGER[b])] = 255
        elif b in STICK:
            s[(e.EV_ABS, STICK[b][0])] = STICK[b][1]
        else:
            print(f"vpad: unknown button {b}", file=sys.stderr)
    return s


class Pad:
    def __init__(self, log, product=0x028E):
        self.ui = make_pad(product)
        self.now = {}
        self.log = log
        # force feedback: the kernel hands each upload and erase to this process and holds the game's ioctl until it
        # is answered (up to 30 s), so they are answered at once, and the motors' use logged
        threading.Thread(target=self.feedback, daemon=True).start()

    def feedback(self):
        effects = {}
        try:
            for ev in self.ui.read_loop():
                if ev.type == e.EV_UINPUT and ev.code == e.UI_FF_UPLOAD:
                    up = self.ui.begin_upload(ev.value)
                    up.retval = 0
                    effects[up.effect.id] = up.effect
                    self.ui.end_upload(up)
                elif ev.type == e.EV_UINPUT and ev.code == e.UI_FF_ERASE:
                    er = self.ui.begin_erase(ev.value)
                    er.retval = 0
                    self.ui.end_erase(er)
                elif ev.type == e.EV_FF:
                    fx = effects.get(ev.code)
                    r = fx.u.ff_rumble_effect if fx is not None and fx.type == e.FF_RUMBLE else None
                    what = f"strong {r.strong_magnitude} weak {r.weak_magnitude}" if r else "effect"
                    self.log(f"rumble {'on' if ev.value else 'off'}: {what}")
        except OSError:
            pass  # closed

    def set(self, state):
        """writes the controls that changed; everything not in state goes back to rest"""
        rest = {(e.EV_KEY, c): 0 for c in BUTTONS.values()} | {(e.EV_ABS, a): 0 for a in AXES}
        want = rest | state
        for k, v in want.items():
            if self.now.get(k, 0) != v:
                self.ui.write(k[0], k[1], v)
        self.ui.syn()
        self.now = want

    def close(self):
        self.set({})
        self.ui.close()


def load(path):
    presses = []
    with open(path) as f:
        for line in f:
            p = line.split()
            if not p or p[0].startswith("#"):
                continue
            presses.append((float(p[0]), p[1], float(p[2]) if len(p) > 2 else 8.0))
    return presses


def play(pad, presses, fps, t0, log):
    """plays one script from t0 (monotonic seconds); returns the time its last press ended"""
    if not presses:
        return t0
    end = max(at + held for at, _, held in presses)
    told = set()
    while True:
        frame = (time.monotonic() - t0) * fps
        state = {}
        for i, (at, b, held) in enumerate(presses):
            if at <= frame <= at + held:
                state |= state_of(b)
                if i not in told:
                    told.add(i)
                    log(f"frame {at:.0f}: {b} {held:.0f}")
        pad.set(state)
        if frame > end:
            return time.monotonic()
        time.sleep(0.25 / fps)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fps", type=float, default=60)
    ap.add_argument("--delay", type=float, default=0, help="seconds before the first script's frame 0")
    ap.add_argument("--fifo", help="a pipe to read live presses from once the scripts have played")
    ap.add_argument("--product", type=lambda v: int(v, 0), default=0x028E, help="the USB product id (Microsoft's vendor id)")
    ap.add_argument("scripts", nargs="*")
    a = ap.parse_args()
    start = time.monotonic()

    def log(msg):
        print(f"vpad {time.monotonic() - start:7.2f}s {msg}", flush=True)

    pad = Pad(log, a.product)
    log(f"plugged in: {pad.ui.device.path}")
    try:
        t = time.monotonic() + a.delay
        for s in a.scripts:
            log(f"script {s}")
            t = play(pad, load(s), a.fps, t, log)
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
                        n = float(p[1]) if len(p) > 1 else 8.0
                        if p[0] == "wait":
                            time.sleep(n / a.fps)
                            continue
                        log(f"live: {p[0]} {n:.0f}")
                        play(pad, [(0, p[0], n)], a.fps, time.monotonic(), lambda m: None)
                        pad.set({})
    except KeyboardInterrupt:
        pass
    finally:
        pad.close()
        log("unplugged")


if __name__ == "__main__":
    main()
