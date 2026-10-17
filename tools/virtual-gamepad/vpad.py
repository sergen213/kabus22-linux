#!/usr/bin/env python3
"""Virtual Xbox 360 pad (uinput) for live Kabus 22 controller tests. Needs python-evdev and write access to /dev/uinput.

  vpad.py [--pads N] [--sync LOG] [--ff-log FILE] [--probe] [SCRIPT]

The device copies the xpad driver's Xbox 360 identity (USB 045e:028e, BTN_A..BTN_THUMBR, ABS_X/Y/RX/RY +-32767,
ABS_Z/RZ 0-255 triggers, ABS_HAT0X/Y d-pad) plus EV_FF/FF_RUMBLE. Every force-feedback upload/erase/play the game
sends is acknowledged and logged (stdout and --ff-log) with its strength and duration. The device is destroyed on
exit, error, SIGINT, SIGTERM and SIGHUP.

SCRIPT lines (blank and # ignored), run in order:
  wait TEXT...           block until a new line of the --sync log contains TEXT (scenario `store NAME ...` lines)
  sleep MS
  down BTN | up BTN | press BTN [MS=100]     BTN: A B X Y LB RB BACK START GUIDE LS RS
  axis AXIS VALUE [MS]   raw evdev value; AXIS: LX LY RX RY (-32768..32767, up is negative), LT RT (0..255),
                         HATX HATY (-1..1); with MS the axis returns to rest after MS
  unplug | plug          destroy / recreate the device (hotplug)
  pad K COMMAND...       any command above for pad K (0-based); a bare command drives pad 0
--pads N: N virtual pads (default 1). Pad 0 is created at start; pads 1.. stay unplugged until their `pad K plug`, so the
game always enumerates pad 0 first and each later pad hotplugs into the next game controller slot.
--probe: after creating the device, list SDL2 joysticks with SDL_IsGameController and send one 200 ms rumble.
"""
import argparse, ctypes, ctypes.util, os, select, signal, sys, threading, time
from evdev import AbsInfo, UInput, ecodes as e

BUTTONS = {"A": e.BTN_A, "B": e.BTN_B, "X": e.BTN_X, "Y": e.BTN_Y, "LB": e.BTN_TL, "RB": e.BTN_TR, "BACK": e.BTN_SELECT,
           "START": e.BTN_START, "GUIDE": e.BTN_MODE, "LS": e.BTN_THUMBL, "RS": e.BTN_THUMBR}
AXES = {"LX": e.ABS_X, "LY": e.ABS_Y, "RX": e.ABS_RX, "RY": e.ABS_RY, "LT": e.ABS_Z, "RT": e.ABS_RZ, "HATX": e.ABS_HAT0X, "HATY": e.ABS_HAT0Y}
STICK, TRIGGER, HAT = AbsInfo(0, -32768, 32767, 16, 128, 0), AbsInfo(0, 0, 255, 0, 0, 0), AbsInfo(0, -1, 1, 0, 0, 0)
CAPS = {e.EV_KEY: list(BUTTONS.values()),
        e.EV_ABS: [(e.ABS_X, STICK), (e.ABS_Y, STICK), (e.ABS_Z, TRIGGER), (e.ABS_RX, STICK), (e.ABS_RY, STICK),
                   (e.ABS_RZ, TRIGGER), (e.ABS_HAT0X, HAT), (e.ABS_HAT0Y, HAT)],
        e.EV_FF: [e.FF_RUMBLE]}


class Pad:
    def __init__(self, ff_log, index=0, t0=None):
        self.lock, self.ui, self.ff_log, self.stop = threading.Lock(), None, ff_log, False
        self.t0, self.tag = t0 or time.monotonic(), f"pad{index} " if index else ""
        if not index:
            self.plug()
        threading.Thread(target=self.serve_ff, daemon=True).start()

    def log(self, text):
        line = f"t={time.monotonic() - self.t0:.3f} {self.tag}{text}"
        print("vpad:", line, flush=True)
        if self.ff_log and text.startswith("ff"):
            with open(self.ff_log, "a") as f:
                f.write(line + "\n")

    def plug(self):
        with self.lock:
            if not self.ui:
                self.ui = UInput(CAPS, name="Microsoft X-Box 360 pad", vendor=0x045e, product=0x028e, version=0x0114,
                                 bustype=e.BUS_USB, max_effects=16)
                self.log(f"device created {self.ui.device.path if self.ui.device else ''}")

    def unplug(self):
        with self.lock:
            if self.ui:
                self.ui.close()
                self.ui = None
                self.log("device destroyed")

    def emit(self, kind, code, value):
        with self.lock:
            if not self.ui:
                raise RuntimeError("input while unplugged")
            self.ui.write(kind, code, value)
            self.ui.syn()

    def serve_ff(self):
        # The kernel blocks the game's EVIOCSFF until this thread acknowledges the upload, so it runs beside the script.
        while not self.stop:
            ui = self.ui
            if not ui:
                time.sleep(0.02)
                continue
            try:
                if not select.select([ui.fd], [], [], 0.05)[0]:
                    continue
            except (OSError, ValueError):
                continue
            with self.lock:
                if ui is not self.ui:
                    continue
                while (ev := ui.read_one()) is not None:
                    if ev.type == e.EV_UINPUT and ev.code == e.UI_FF_UPLOAD:
                        up = ui.begin_upload(ev.value)
                        fx = up.effect
                        if fx.type == e.FF_RUMBLE:
                            r = fx.u.ff_rumble_effect
                            self.log(f"ff upload id={fx.id} rumble strong={r.strong_magnitude} weak={r.weak_magnitude} length_ms={fx.ff_replay.length}")
                        else:
                            self.log(f"ff upload id={fx.id} type={fx.type} length_ms={fx.ff_replay.length}")
                        up.retval = 0
                        ui.end_upload(up)
                    elif ev.type == e.EV_UINPUT and ev.code == e.UI_FF_ERASE:
                        er = ui.begin_erase(ev.value)
                        self.log(f"ff erase id={er.effect_id}")
                        er.retval = 0
                        ui.end_erase(er)
                    elif ev.type == e.EV_FF:
                        self.log(f"ff {'play' if ev.value else 'stop'} id={ev.code} count={ev.value}" if ev.code < e.FF_GAIN else f"ff gain {ev.value}")


def probe(pad):
    lib = ctypes.CDLL(ctypes.util.find_library("SDL2-2.0") or "libSDL2-2.0.so.0")
    lib.SDL_JoystickNameForIndex.restype = lib.SDL_GameControllerNameForIndex.restype = ctypes.c_char_p
    lib.SDL_GameControllerOpen.restype = ctypes.c_void_p
    lib.SDL_GameControllerRumble.argtypes = [ctypes.c_void_p, ctypes.c_uint16, ctypes.c_uint16, ctypes.c_uint32]
    lib.SDL_GameControllerClose.argtypes = [ctypes.c_void_p]
    if lib.SDL_Init(0x2000 | 0x200):  # GAMECONTROLLER | JOYSTICK
        raise RuntimeError("SDL_Init failed")
    found = False
    for i in range(lib.SDL_NumJoysticks()):
        name, gc = lib.SDL_JoystickNameForIndex(i).decode(), lib.SDL_IsGameController(i)
        print(f"probe: joystick {i} '{name}' SDL_IsGameController={gc}" + (f" mapping '{lib.SDL_GameControllerNameForIndex(i).decode()}'" if gc else ""), flush=True)
        if gc and "360" in name and not found:
            found = True
            c = lib.SDL_GameControllerOpen(i)
            print(f"probe: SDL_GameControllerRumble -> {lib.SDL_GameControllerRumble(c, 0x8000, 0x4000, 200)}", flush=True)
            time.sleep(0.5)
            lib.SDL_GameControllerClose(c)
    lib.SDL_Quit()
    return found


def run(pads, path, sync):
    offset = 0
    for no, raw in enumerate(open(path), 1):
        w = raw.split("#", 1)[0].split()
        if not w:
            continue
        pads[0].log(f"script {no}: {' '.join(w)}")
        pad = pads[0]
        if w[0] == "pad":
            if len(w) < 3 or not w[1].isdigit() or int(w[1]) >= len(pads):
                raise RuntimeError(f"line {no}: pad K COMMAND with K < --pads {len(pads)}")
            pad, w = pads[int(w[1])], w[2:]
        c = w[0]
        if c == "wait":
            text, deadline = " ".join(w[1:]), time.monotonic() + 600
            while True:
                with open(sync, "rb") as f:
                    f.seek(offset)
                    data = f.read()
                cut = data.rfind(b"\n") + 1
                hit = next((l for l in data[:cut].split(b"\n") if text.encode() in l), None)
                if hit is not None:
                    offset += data.find(hit) + len(hit)
                    break
                if time.monotonic() > deadline:
                    raise RuntimeError(f"line {no}: timeout waiting for '{text}'")
                time.sleep(0.02)
        elif c == "sleep":
            time.sleep(int(w[1]) / 1000)
        elif c in ("down", "up", "press"):
            code = BUTTONS[w[1].upper()]
            if c != "up":
                pad.emit(e.EV_KEY, code, 1)
            if c == "press":
                time.sleep((int(w[2]) if len(w) > 2 else 100) / 1000)
            if c != "down":
                pad.emit(e.EV_KEY, code, 0)
        elif c == "axis":
            code = AXES[w[1].upper()]
            pad.emit(e.EV_ABS, code, int(w[2]))
            if len(w) > 3:
                time.sleep(int(w[3]) / 1000)
                pad.emit(e.EV_ABS, code, 0)
        elif c == "unplug":
            pad.unplug()
        elif c == "plug":
            pad.plug()
        else:
            raise RuntimeError(f"line {no}: unknown command {c}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("script", nargs="?")
    ap.add_argument("--pads", type=int, default=1, choices=range(1, 5))
    ap.add_argument("--sync", help="log file that `wait` lines watch (the game's stdout)")
    ap.add_argument("--ff-log", help="append force-feedback events here")
    ap.add_argument("--probe", action="store_true")
    a = ap.parse_args()

    def bye(sig, _):
        raise SystemExit(128 + sig)
    for s in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(s, bye)
    t0 = time.monotonic()
    pads = [Pad(a.ff_log, i, t0) for i in range(a.pads)]
    try:
        time.sleep(0.5)  # let udev publish the node
        if a.probe and not probe(pads[0]):
            raise SystemExit("probe: no Xbox 360 game controller found")
        if a.script:
            run(pads, a.script, a.sync)
        elif not a.probe:
            signal.pause()
    finally:
        for pad in pads:
            pad.stop = True
            pad.unplug()


if __name__ == "__main__":
    main()
