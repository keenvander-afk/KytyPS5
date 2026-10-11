#!/usr/bin/env python3
"""Run PS5 titles under kyty_emulator for a fixed time and record frame-rate results.

The emulator's built-in frame benchmark (src/graphics/presentation/window/frameBenchmark.cpp)
is driven through KYTY_BENCH_* environment variables; this script launches the
title, enforces a hard timeout, collects the summary, and appends one record per
run to <results>/history.jsonl so every change can be compared against the best
previous run of the same title.

Single title:
    python tools/bench/kyty_bench.py --emu _Build/windows/install/kyty_emulator.exe \
        --game D:/ps5/PPSA01342-app --name "Demon's Souls" --warmup 60 --seconds 60

Suite (TOML file with [[title]] name/game/warmup/seconds/args entries):
    python tools/bench/kyty_bench.py --emu ... --suite tools/bench/suite.toml

Game files must be dumps of titles you own. They are never committed.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import platform
import subprocess
import sys
import time
import tomllib
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def git_rev() -> str:
    try:
        rev = subprocess.run(["git", "-C", str(REPO), "rev-parse", "--short=10", "HEAD"], capture_output=True, text=True).stdout.strip()
        dirty = subprocess.run(["git", "-C", str(REPO), "status", "--porcelain", "--untracked-files=no"], capture_output=True, text=True).stdout.strip()
        return rev + ("-dirty" if dirty else "")
    except OSError:
        return "unknown"


def gpu_name() -> str:
    try:
        out = subprocess.run(["vulkaninfo", "--summary"], capture_output=True, text=True, timeout=20).stdout
        for line in out.splitlines():
            if "deviceName" in line:
                return line.split("=", 1)[1].strip()
    except (OSError, subprocess.TimeoutExpired):
        pass
    return "unknown"


def run_title(emu: Path, title: dict, results: Path, timeout_pad: float, base_args: list[str]) -> dict:
    name = title["name"]
    slug = "".join(c if c.isalnum() else "_" for c in name).strip("_")[:60]
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    run_dir = results / slug / stamp
    run_dir.mkdir(parents=True, exist_ok=True)
    csv = run_dir / "frames.csv"
    warmup = float(title.get("warmup", 30))
    seconds = float(title.get("seconds", 60))

    env = dict(os.environ)
    env.update(KYTY_BENCH_SECONDS=str(seconds), KYTY_BENCH_WARMUP=str(warmup), KYTY_BENCH_OUT=str(csv))
    cmd = [str(emu), "--game", str(title["game"]), "--no-splash", "--skip-notice-screen", "true",
           "--printf-direction", "File", "--printf-output-file", str(run_dir / "emulator.log"),
           *base_args, *title.get("args", [])]
    # Boot time is unknown, so the hard limit is generous; the benchmark itself
    # ends 5 s after the window if frames stop arriving.
    limit = float(title.get("timeout", warmup + seconds + timeout_pad))

    print(f"== {name}: warmup {warmup:g}s, measure {seconds:g}s, timeout {limit:g}s")
    t0 = time.monotonic()
    with open(run_dir / "stdout.txt", "w", encoding="utf-8", errors="replace") as out:
        proc = subprocess.Popen(cmd, cwd=emu.parent, env=env, stdout=out, stderr=subprocess.STDOUT)
        try:
            code = proc.wait(timeout=limit)
            outcome = "completed" if (Path(str(csv) + ".summary.json")).exists() else f"exited({code})"
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
            code = None
            outcome = "timeout"
    wall = time.monotonic() - t0

    summary = {}
    sp = Path(str(csv) + ".summary.json")
    if sp.exists():
        summary = json.loads(sp.read_text())
    record = {
        "time": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
        "title": name,
        "game": str(title["game"]),
        "commit": git_rev(),
        "host": {"os": platform.platform(), "cpu": platform.processor(), "gpu": gpu_name()},
        "args": cmd[2:],
        "outcome": outcome,
        "exit_code": code,
        "wall_s": round(wall, 1),
        "run_dir": str(run_dir),
        **summary,
    }
    (run_dir / "record.json").write_text(json.dumps(record, indent=2))
    with open(results / "history.jsonl", "a", encoding="utf-8") as h:
        h.write(json.dumps(record) + "\n")
    return record


def best_previous(results: Path, title: str, exclude_dir: str) -> dict | None:
    best = None
    hist = results / "history.jsonl"
    if not hist.exists():
        return None
    for line in hist.read_text(encoding="utf-8").splitlines():
        r = json.loads(line)
        if r["title"] != title or r["run_dir"] == exclude_dir or r.get("outcome") != "completed":
            continue
        if best is None or r.get("avg_fps", 0) > best.get("avg_fps", 0):
            best = r
    return best


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--emu", required=True, type=Path, help="path to kyty_emulator(.exe)")
    ap.add_argument("--game", type=Path)
    ap.add_argument("--name")
    ap.add_argument("--warmup", type=float, default=30)
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--suite", type=Path, help="TOML file with [[title]] entries")
    ap.add_argument("--results", type=Path, default=REPO / "bench-results")
    ap.add_argument("--timeout-pad", type=float, default=600, help="extra seconds allowed for boot")
    ap.add_argument("emu_args", nargs="*", help="extra emulator arguments (after --)")
    args = ap.parse_args()

    if args.suite:
        with args.suite.open("rb") as f:
            titles = tomllib.load(f)["title"]
    elif args.game:
        titles = [{"name": args.name or args.game.name, "game": args.game, "warmup": args.warmup, "seconds": args.seconds}]
    else:
        ap.error("--game or --suite is required")

    args.results.mkdir(parents=True, exist_ok=True)
    rows = []
    for t in titles:
        rec = run_title(args.emu.resolve(), t, args.results, args.timeout_pad, args.emu_args)
        prev = best_previous(args.results, rec["title"], rec["run_dir"])
        rows.append((rec, prev))

    print("\n| Title | Outcome | Avg FPS | 1% low | p99 ms | Spikes (compile) | Shader ms | Pipeline ms | Best before |")
    print("|---|---|---:|---:|---:|---:|---:|---:|---:|")
    for rec, prev in rows:
        before = f"{prev['avg_fps']:.1f} ({prev['commit']})" if prev else "—"
        spikes = f"{rec.get('spike_frames', 0)} ({rec.get('spike_frames_with_compiles', 0)})"
        print(f"| {rec['title']} | {rec['outcome']} | {rec.get('avg_fps', 0):.1f} | {rec.get('one_percent_low_fps', 0):.1f} | "
              f"{rec.get('p99_frame_ms', 0):.1f} | {spikes} | {rec.get('shader_compile_ms', 0):.0f} | "
              f"{rec.get('pipeline_create_ms', 0):.0f} | {before} |")
    return 0 if all(r["outcome"] == "completed" for r, _ in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
