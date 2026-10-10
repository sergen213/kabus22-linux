#!/usr/bin/env python3
"""Reference captures of the ORIGINAL Kabus 22 (ISO edition) under Wine, for reverse engineering only.

Nothing in the native build or runtime uses Wine. This tool prepares a private Wine prefix and a
private copy of the ISO payload, runs the unmodified K22.exe windowed at its authored 1024x768,
drives it with XTest key events and captures the exact game window (x11grab -window_id).

  wineref.py setup  SCRATCH ISO [SAVES_DIR]   extract JPN-K22.exe from ISO, innoextract it, make prefix
  wineref.py run    SCRATCH CASE.k22s OUTDIR   launch K22.exe, replay CASE, kill the prefix

CASE is a native scenario (tests/reference/*.k22s) replayed on the original with the same keys:
press KEY [HOLD_FRAMES], down/up KEY, frames N (N/60 s), load-save N (the same menu keys as the native
command) and capture NAME (NAME.png). Native-only checks (wait, assert, print, store, realtime, seed)
are skipped; lines "#wine: CMD" run only here: sleep SECONDS, wait-log TEXT [TIMEOUT_S] (acklog.txt, searched after
the previous wait-log match: A6 logs "Level(X.WMB)" when loading starts and "Physics restarted" when it is done),
record NAME SECONDS (background video). Anything else stops the run instead of silently diverging.
Movement and turning are scaled by TIME on both sides; give turn holds in multiples of 5 frames (1/12 s = 6 frames
at the original's FPS_MAX 72) so both sides turn by the same angle.
Keys are sent as X keysyms, so the host keyboard layout does not matter. The game pauses while
unfocused, so every input and capture re-activates the window first.
Environment: DISPLAY (default :0), XAUTHORITY (Xwayland mutter auth is found automatically),
INNOEXTRACT (default innoextract on PATH). On a desktop session the compositor may refuse to focus the
game window (keys then go nowhere); run on a private rootful Xwayland instead, which has the GPU and no
window manager:  Xwayland :7 -geometry 1280x900 -nolisten tcp &  DISPLAY=:7 wineref.py run ...
"""
import glob, os, re, shutil, subprocess, sys, time

# Skipped on the original: checks, plus native steering (walk-to/route/turn-to); cases give "#wine:" key holds instead.
NATIVE_ONLY = {"wait", "assert", "print", "store", "realtime", "seed", "walk-to", "route", "turn-to", "find", "list"}


def keysym(sdl):
    """SDL scancode name (scenario syntax) -> X keysym."""
    named = {"Space": "space", "Left_Ctrl": "Control_L", "Left_Shift": "Shift_L", "Left_Alt": "Alt_L", "Tab": "Tab"}
    return named.get(sdl, sdl.lower() if len(sdl) == 1 else sdl)

ARGS = ["-nc", "-diag", "-d", "defiae_d3d", "-d", "defdil_en", "-d", "defses_en"]
# -nc: the ISO K22.exe fails A6's own executable checksum (stored 233761058, actual sum 233546425;
# K22.exe 0041fc00/00422af0) and stops with "E356 Startup failure"; -nc is the A6 option that skips
# that check (00412b90). defiae_d3d is required by K22.wdl main(); without deftam_d3d it runs windowed.


def env():
    e = dict(os.environ)
    e.setdefault("DISPLAY", ":0")
    if "XAUTHORITY" not in e:
        auth = glob.glob(f"/run/user/{os.getuid()}/.mutter-Xwaylandauth.*")
        if auth:
            e["XAUTHORITY"] = auth[0]
    e.setdefault("WINEDEBUG", "-all")
    return e


def paths(scratch):
    scratch = os.path.abspath(scratch)
    return scratch, f"{scratch}/prefix", f"{scratch}/game/app"


def setup(scratch, iso, saves=None):
    scratch, prefix, app = paths(scratch)
    e = dict(env(), WINEPREFIX=prefix, WINEDLLOVERRIDES="mscoree,mshtml=")
    if not os.path.exists(f"{app}/K22.exe"):
        os.makedirs(f"{scratch}/iso", exist_ok=True)
        subprocess.run(["7z", "x", "-y", f"-o{scratch}/iso", iso, "JPN-K22*"], check=True, stdout=subprocess.DEVNULL)
        subprocess.run([os.environ.get("INNOEXTRACT", "innoextract"), "-s", "-d", f"{scratch}/game", f"{scratch}/iso/JPN-K22.exe"], check=True)
        shutil.rmtree(f"{scratch}/iso")
    if not os.path.exists(prefix):
        subprocess.run(["wineboot", "-i"], env=e, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["wine", "reg", "add", r"HKCU\Software\Wine\Drivers", "/v", "Graphics", "/d", "x11", "/f"], env=e, check=True, stdout=subprocess.DEVNULL)
    # Host HID devices (e.g. an idle gamepad receiver) appear as a joystick whose resting axes the scripts read as
    # JOY_Ileri (GenelAyarlar.wdl Joy_Takip/Tuslari_Esitle): the player walks by itself and the inventory loop
    # (Envantermenu.wdl EM_TusKontrol) waits forever on Key_Ileri. Keep the prefix keyboard/mouse-only.
    for value, data in (("DisableHidraw", "1"), ("DisableInput", "1"), ("Enable SDL", "0")):
        subprocess.run(["wine", "reg", "add", r"HKLM\System\CurrentControlSet\Services\winebus", "/v", value, "/t", "REG_DWORD",
                        "/d", data, "/f"], env=e, check=True, stdout=subprocess.DEVNULL)
    drive = f"{prefix}/dosdevices/k:"
    if not os.path.lexists(drive):
        os.symlink(app, drive)
    if saves:
        for f in glob.glob(f"{saves}/kayit*.sav") + [f"{saves}/kysd.sav", f"{saves}/ftz.k22"]:
            if os.path.exists(f):
                shutil.copy2(f, app)
    subprocess.run(["wineserver", "-k"], env=e)
    print(subprocess.run(["sha256sum", f"{app}/K22.exe", f"{app}/K22.wrs"], capture_output=True, text=True).stdout, end="")


def x(e, *cmd):
    try:
        return subprocess.run(cmd, env=e, capture_output=True, text=True, errors="replace", timeout=10).stdout
    except subprocess.TimeoutExpired:
        return ""


def run(scratch, steps, out):
    scratch, prefix, app = paths(scratch)
    os.makedirs(out, exist_ok=True)
    e = dict(env(), WINEPREFIX=prefix)
    log = open(f"{out}/steps.log", "a")
    t0 = time.time()

    def note(msg):
        line = f"{time.time() - t0:8.2f}s {msg}"
        print(line, flush=True)
        log.write(line + "\n")
        log.flush()

    acklog = f"{app}/acklog.txt"
    if os.path.exists(acklog):
        os.remove(acklog)
    proc = subprocess.Popen(["wine", "K:\\K22.exe", *ARGS], cwd=f"{prefix}/dosdevices/k:", env=e,
                            stdout=open(f"{out}/wine.log", "w"), stderr=subprocess.STDOUT, start_new_session=True)
    note(f"start K22.exe {' '.join(ARGS)} pid {proc.pid}")
    win, recorders, logpos = None, [], 0
    try:
        # The compile splash also needs focus, otherwise A6 stalls in the compile loop.
        end = time.time() + 400
        while win is None and time.time() < end:
            for w in x(e, "xdotool", "search", "--onlyvisible", "--name", "^K22$").split():
                x(e, "xdotool", "windowactivate", w)
                x(e, "xdotool", "windowfocus", w)
                if "Geometry: 1024x768" in x(e, "xdotool", "getwindowgeometry", w):
                    win = w
            time.sleep(0.5)
        if win is None:
            raise SystemExit("no 1024x768 K22 window")
        note(f"window {win}")
        x(e, "xdotool", "mousemove", "9999", "9999")  # keep the pointer out of the game window (clamped to the root corner)

        # Desktop compositors (Xwayland under mutter) only give the window's own pixmap; on a nested WM-less
        # server -window_id yields no frame, so grab the root region the undecorated window occupies there.
        wm = "window id" in x(e, "xprop", "-root", "_NET_SUPPORTING_WM_CHECK")

        def grab():
            if wm:
                return ["-window_id", win, "-i", e["DISPLAY"]]
            pos = re.search(r"Position: (\d+),(\d+)", x(e, "xdotool", "getwindowgeometry", win))
            return ["-video_size", "1024x768", "-i", f"{e['DISPLAY']}+{pos[1]},{pos[2]}"]

        def focus():
            # windowactivate asks a window manager; windowfocus is what works on a nested WM-less server (module doc).
            x(e, "xdotool", "windowactivate", win)
            x(e, "xdotool", "windowfocus", win)
        def key(sym, hold=0.12, gap=0.35):
            # One xdotool process for down, hold and up (no spawn latency inside the hold). The original under Wine
            # still sees every hold about one frame (~14 ms at FPS_MAX 72) longer than sent: measured key-sample
            # counts for N/60 s holds were N*1.2 + 0.75..1 (Key_Sag/Key_Sol, cam1 calibration), i.e. a release-side
            # latency in the X/Wine input path. Subtract one 72 fps frame so turn angles match the native press.
            focus()
            x(e, "xdotool", "keydown", sym, "sleep", f"{max(0.0, hold - 1 / 72):.4f}", "keyup", sym)
            time.sleep(gap)

        for raw in open(steps):
            line = raw.strip()
            if line.startswith("#wine:"):
                line = line[6:]
            elif line.startswith("#"):
                continue
            words = line.split()
            if not words:
                continue
            c, a = words[0], words[1:]
            if proc.poll() is not None:
                note(f"process exited {proc.returncode} before: {line}")
                break
            if c in NATIVE_ONLY:
                continue
            if c == "sleep":
                time.sleep(float(a[0]))
            elif c == "frames":
                time.sleep(int(a[0]) / 60)
            elif c == "press":
                # Same timeline as the native press: hold N (default 3) frames, release, 3 more frames.
                key(keysym(a[0]), int(a[1] if len(a) > 1 else 3) / 60, gap=3 / 60)
            elif c in ("down", "up"):
                focus()
                x(e, "xdotool", "key" + c, keysym(a[0]))
            elif c == "load-save":  # same keys as the native load-save: AnaMenu cursor 1, YukleMenu slot N
                key("Down")
                key("Return", gap=2)
                # YukleMenuAc (YukleMenu.wdl:169-197) starts the cursor at Aktif_SaveNo-1, not 0: home it first.
                for _ in range(30):
                    key("Up", gap=0.15)
                for _ in range(int(a[0])):
                    key("Down")
                key("Return")
            elif c == "capture":
                focus()
                if os.path.exists(f"{out}/{a[0]}.png"):
                    os.remove(f"{out}/{a[0]}.png")
                r = subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "x11grab", "-draw_mouse", "0", *grab(),
                                    "-frames:v", "1", f"{out}/{a[0]}.png"], env=e, capture_output=True, text=True)
                if r.returncode or not os.path.exists(f"{out}/{a[0]}.png"):
                    note(f"capture {a[0]} FAILED {r.stderr.strip()}")
                    continue
            elif c == "record":  # runs in the background; later steps (input) happen during the recording
                focus()
                recorders.append(subprocess.Popen(
                    ["ffmpeg", "-loglevel", "error", "-y", "-f", "x11grab", "-draw_mouse", "0", "-framerate", "25", *grab(),
                     "-t", a[1], "-c:v", "libx264", "-crf", "20", "-pix_fmt", "yuv420p", f"{out}/{a[0]}.mp4"],
                    env=e, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
            elif c == "wait-log":  # TEXT after the previous wait-log match, so "wait-log Physics" follows a level name
                text, limit = a[0].lower(), float(a[1]) if len(a) > 1 else 180
                end = time.time() + limit
                while (found := open(acklog, errors="replace").read().lower().find(text, logpos)) < 0 and time.time() < end:
                    focus()
                    time.sleep(0.05)
                if found < 0:
                    note(f"wait-log {text} TIMEOUT")
                else:
                    logpos = found + len(text)
            else:
                raise SystemExit(f"command not reproducible on the original: {line}")
            note(line)
    finally:
        for r in recorders:
            try:
                r.wait(120)
            except subprocess.TimeoutExpired:
                r.kill()
        x(e, "wineserver", "-k")
        try:
            proc.wait(10)
        except subprocess.TimeoutExpired:
            proc.kill()
        if os.path.exists(acklog):
            shutil.copy2(acklog, f"{out}/acklog.txt")
        note("stopped")


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "setup":
        setup(*sys.argv[2:5])
    elif len(sys.argv) == 5 and sys.argv[1] == "run":
        run(*sys.argv[2:])
    else:
        sys.exit(__doc__)
