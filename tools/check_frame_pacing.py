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


def frame_generation_trace(path, tail, max_split, max_p99):
    """Summarize a K22_FG_TRACE file: `microseconds G|R frame periodMs msSinceFrameCompletion` per present call."""
    rows = []
    for line in path.read_text().split("\n"):
        fields = line.split()
        if len(fields) != 5:
            continue
        if rows and int(fields[0]) < rows[-1][0]:
            rows = []  # a new presenter (renderer restart) restarts the clock
        rows.append((int(fields[0]), fields[1], int(fields[2]), float(fields[3]), float(fields[4])))
    rows = rows[-tail:] if tail else rows
    require(len(rows) >= 100, f"only {len(rows)} presents in {path}")
    gaps, to_real, to_generated = [], [], []
    for before, after in zip(rows, rows[1:]):
        gap = (after[0] - before[0]) / 1000
        gaps.append(gap)
        if before[1] == "G" and after[1] == "R" and before[2] == after[2]:
            to_real.append(gap)
        elif before[1] == "R" and after[1] == "G":
            to_generated.append(gap)
    require(to_real and to_generated, "no generated frames in the trace")

    def quantile(values, q):
        ordered = sorted(values)
        return ordered[min(len(ordered) - 1, int(q * len(ordered)))]

    seconds = (rows[-1][0] - rows[0][0]) / 1e6
    real = sum(1 for row in rows[1:] if row[1] == "R")
    g2r, r2g = quantile(to_real, .5), quantile(to_generated, .5)
    split = abs(g2r - r2g) / (g2r + r2g)
    p50, p99 = quantile(gaps, .5), quantile(gaps, .99)
    print(f"presents={len(rows)} presentedFps={len(gaps) / seconds:.1f} renderedFps={real / seconds:.1f} "
          f"interval p1={quantile(gaps, .01):.2f} p50={p50:.2f} p99={p99:.2f} max={max(gaps):.2f} "
          f"generatedToReal p50={g2r:.2f} p99={quantile(to_real, .99):.2f} "
          f"realToGenerated p50={r2g:.2f} p99={quantile(to_generated, .99):.2f} "
          f"split={split * 100:.1f}% p99/p50={p99 / p50:.2f} pacingInterval={quantile([row[3] for row in rows], .5):.2f} "
          f"completionToGenerated p50={quantile([row[4] for row in rows if row[1] == 'G'], .5):.2f} "
          f"completionToReal p50={quantile([row[4] for row in rows if row[1] == 'R'], .5):.2f}")
    if max_split is not None:
        require(split <= max_split, f"generated/real halves differ by {split * 100:.1f}% of their sum")
    if max_p99 is not None:
        require(p99 <= max_p99 * p50, f"present interval p99 {p99:.2f} ms exceeds {max_p99:g} x p50 {p50:.2f} ms")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path,
                        help="Real graphical native kabus22 executable (not a headless check)")
    parser.add_argument("--fg-trace", type=Path,
                        help="Summarize a frame-generation present trace (K22_FG_TRACE=FILE) instead of running the engine")
    parser.add_argument("--tail", type=int, default=0, help="--fg-trace: only the last N presents (steady state)")
    parser.add_argument("--max-split", type=float,
                        help="--fg-trace: fail when |generated->real - real->generated| exceeds this fraction of their sum")
    parser.add_argument("--max-p99", type=float, help="--fg-trace: fail when the p99 present interval exceeds this multiple of p50")
    args = parser.parse_args()
    if args.fg_trace:
        frame_generation_trace(args.fg_trace, args.tail, args.max_split, args.max_p99)
        return
    require(args.engine is not None, "--engine or --fg-trace is required")
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
