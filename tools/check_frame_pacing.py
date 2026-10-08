#!/usr/bin/env python3
"""Check live native CLI frame pacing; requires an existing SDL/OpenGL display.

Usage: python3 tools/check_frame_pacing.py --engine /absolute/path/to/kabus22
Only a generated, asset-free WDL program runs. All files/user directories are
private and temporary. The child must finish exactly 11 frames within 20 seconds.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import signal
import subprocess
import sys
import tempfile
import time


# MAIN begins on frame 1. Each wait(1) resumes on the following frame:
# baseline 2,3; cap20 4,5; cap40 6,7; cap0 8,9; cap-10 10,11.
FRAMES = 11
FIXTURE = """var caps[4] = (20,40,0,-10);
function sample(phase, handle) {
    file_var_write(handle, phase);
    file_var_write(handle, FPS_MAX);
    file_var_write(handle, TOTAL_FRAMES);
    file_var_write(handle, TIME_FRAME * 1024);
}
function main() {
    var handle = file_open_write("pacing.txt");
    var phase = 0;
    var i = 0;
    wait(1); sample(-1, handle);
    wait(1); sample(-1, handle);
    while (phase < 4) {
        FPS_MAX = caps[phase];
        i = 0;
        while (i < 2) {
            wait(1);
            sample(phase, handle);
            i += 1;
        }
        phase += 1;
    }
    file_close(handle);
}
"""


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", required=True, type=Path,
                        help="Real graphical native kabus22 executable (not a headless check)")
    args = parser.parse_args()
    engine = args.engine.resolve(strict=True)
    require(engine.is_file() and os.access(engine, os.X_OK),
            f"not an executable file: {engine}")
    with tempfile.TemporaryDirectory(prefix="kabus22-pacing-") as temporary:
        root = Path(temporary)
        env = os.environ.copy()
        for variable in ("HOME", "XDG_DATA_HOME", "XDG_CONFIG_HOME",
                         "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"):
            directory = root / variable.lower()
            directory.mkdir(mode=0o700)
            env[variable] = str(directory)
        data = root / "empty-data"
        data.mkdir(mode=0o700)
        script = root / "pacing.wdl"
        script.write_text(FIXTURE, encoding="ascii")
        argv = [str(engine), "--data", str(data), "--script", str(script),
                "--frames", str(FRAMES)]
        print("argv:", shlex.join(argv), flush=True)
        log = root / "engine.log"
        try:
            started = time.monotonic()
            with log.open("wb") as output:
                child = subprocess.Popen(argv, cwd=root, env=env, stdout=output,
                                         stderr=subprocess.STDOUT, start_new_session=True)
                try:
                    child.wait(timeout=20)
                except subprocess.TimeoutExpired as error:
                    raise RuntimeError("graphical child exceeded its hard 20-second deadline") from error
                finally:
                    if child.poll() is None:
                        try:
                            os.killpg(child.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        child.wait()
            elapsed = time.monotonic() - started
            require(child.returncode == 0, f"graphical child exited {child.returncode}")
            text = log.read_text(encoding="utf-8", errors="replace")
            require(re.search(r"\bNative frames=11 live_processes=0\b", text),
                    "child did not normally complete exactly 11 frames and its WDL process")
            saves = Path(env["XDG_DATA_HOME"]) / "kabus22"
            words = [int(token) for token in (saves / "pacing.txt").read_text().split()]
            require(len(words) == 40, "expected exactly 40 live pacing sample words")
            records = [words[offset:offset + 4] for offset in range(0, 40, 4)]
            intervals = {}
            for index, (phase, cap, frame, raw_time) in enumerate(records):
                expected_phase = -1 if index < 2 else (index - 2) // 2
                expected_cap = (20, 40, 0, -10)[expected_phase] if index >= 2 else cap
                require(phase == expected_phase and cap == expected_cap and frame == index + 2,
                        f"unexpected live phase/cap/frame record: {records[index]}")
                require(1 <= raw_time <= 16384, f"invalid TIME_FRAME raw word: {raw_time}")
                # TIME_FRAME = 16 * seconds in signed 22.10. WDL multiplication
                # by 1024 writes its raw word exactly, avoiding %.3f rounding.
                intervals.setdefault(phase, []).append(raw_time / 16384)
            for phase, label in ((-1, "baseline"), (0, "FPS_MAX=20"), (1, "FPS_MAX=40"),
                                 (2, "FPS_MAX=0"), (3, "FPS_MAX=-10")):
                print(f"{label}: " + ", ".join(f"{dt * 1000:.3f}ms" for dt in intervals[phase]))
            print(f"normal exit: {FRAMES} frames, wall={elapsed:.3f}s; TIME_FRAME saturates at 1000ms")
            # Allow 2ms for quantization/poll-boundary jitter; oversleep is valid.
            # No narrow upper bound: display refresh and scheduler load vary.
            for phase, floor in ((0, 0.050), (1, 0.025), (2, 1.000), (3, 1.000)):
                require(min(intervals[phase]) >= floor - 0.002,
                        f"phase {phase} ran below its {floor * 1000:g}ms pacing floor")
            if max(intervals[-1]) < 0.024:
                require(min(intervals[1]) < min(intervals[0]) * 0.90,
                        "baseline permits 40fps, but changing FPS_MAX 20→40 did not speed up frames")
                print("dynamic 20→40 speedup checked (baseline permits >40fps)")
            else:
                print("dynamic speedup unexercised: baseline display/load does not prove >40fps; "
                      "both pacing floors and zero/negative clamps checked")
            print("Native graphical frame-pacing checks passed")
        except BaseException:
            if log.exists():
                print("--- graphical child failure log ---", file=sys.stderr)
                print(log.read_text(encoding="utf-8", errors="replace"), file=sys.stderr, end="")
            raise


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Frame-pacing check failed: {error}", file=sys.stderr)
        sys.exit(1)
