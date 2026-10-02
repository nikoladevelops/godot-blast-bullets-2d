#!/usr/bin/env python3
"""Headless benchmark runner for BlastBullets2D (performance regression log).

Each scenario in test_project/benchmarks/scenarios/*.gd (a BlastBenchmark)
runs in its own headless Godot process with SIMULATED time
(`--fixed-fps 60`): frames are never slept on, so a frame's wall time is the
CPU cost of that frame. Every scenario is repeated (--repeat, default 3) and
the median of each statistic is kept to damp OS noise.

Outputs (all under test_project/benchmarks/log/):
  results/<UTC>_<sha>[-dirty].json  full results of this run (+ machine info)
  history.csv                       one row per scenario per run (append-only)
  baseline.json                     the reference numbers (--update-baseline)
  LATEST.md                         this run vs baseline, regressions flagged

Usage:
    python3 tools/run_benchmarks.py                     # all scenarios, 5 repeats
    python3 tools/run_benchmarks.py --scenario churn    # substring filter (repeatable)
    python3 tools/run_benchmarks.py --repeat 5
    python3 tools/run_benchmarks.py --gate              # exit 1 on a regression vs baseline
    python3 tools/run_benchmarks.py --update-baseline   # make this run the new baseline
    python3 tools/run_benchmarks.py --list

Regression rule (per scenario, vs baseline): frame or factory-tick p50 more
than +10% AND more than +0.05 ms, or p95 more than +20% AND +0.3 ms. p99 and
max are reported (spike hunting) but not gated: with 300 frames they are a
handful of frames and flip between modes from run to run.
Numbers are only comparable on the same machine and build type; the result
files record both. Close other heavy programs while benchmarking.
"""

import argparse
import csv
import datetime as dt
import json
import os
import platform
import statistics
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))
from run_tests import godot_binary, refresh_class_cache, stale_binary_reason  # noqa: E402

PROJECT = os.path.join(REPO_ROOT, "test_project")
BENCH_DIR = os.path.join(PROJECT, "benchmarks")
SCEN_DIR = os.path.join(BENCH_DIR, "scenarios")
# Data lives in log/ (with a .gdignore: Godot would otherwise import
# history.csv as a translation file).
LOG_DIR = os.path.join(BENCH_DIR, "log")
RESULTS_DIR = os.path.join(LOG_DIR, "results")
HISTORY = os.path.join(LOG_DIR, "history.csv")
BASELINE = os.path.join(LOG_DIR, "baseline.json")
LATEST = os.path.join(LOG_DIR, "LATEST.md")

STAT_KEYS = ("p50", "p95", "p99", "max", "mean")
SERIES = ("frame_ms", "factory_tick_ms", "physics_ms", "process_ms")


def git_info():
    def run(*args):
        try:
            return subprocess.run(["git", *args], cwd=REPO_ROOT, capture_output=True, text=True, timeout=20).stdout.strip()
        except (OSError, subprocess.SubprocessError):
            return ""
    sha = run("rev-parse", "--short", "HEAD") or "nogit"
    dirty = bool(run("status", "--porcelain", "--", "src"))
    return sha, dirty


def machine_info():
    cpu = platform.processor() or ""
    try:
        with open("/proc/cpuinfo", encoding="utf-8") as handle:
            for line in handle:
                if line.startswith("model name"):
                    cpu = line.split(":", 1)[1].strip()
                    break
    except OSError:
        pass
    return {"cpu": cpu, "cores": os.cpu_count(), "os": platform.platform(), "python": platform.python_version()}


def discover():
    return sorted(f[:-3] for f in os.listdir(SCEN_DIR) if f.endswith(".gd"))


def run_once(godot, scenario, timeout):
    with tempfile.TemporaryDirectory(prefix="blast_bench_") as tmp:
        out = os.path.join(tmp, "result.json")
        # --quit-after: hard frame cap so a script error (bench_main without a
        # working script never quits) fails fast instead of hanging.
        cmd = [godot, "--headless", "--fixed-fps", "60", "--quit-after", "20000", "--path", PROJECT,
               "res://benchmarks/bench_main.tscn",
               "--", f"--scenario=res://benchmarks/scenarios/{scenario}.gd", f"--out={out}"]
        proc = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True, errors="replace", timeout=timeout)
        if proc.returncode != 0 or not os.path.exists(out):
            tail = "\n".join((proc.stdout + proc.stderr).splitlines()[-25:])
            raise RuntimeError(f"{scenario}: exit {proc.returncode}\n{tail}")
        with open(out, encoding="utf-8") as handle:
            return json.load(handle)


def median_merge(runs):
    """Median of every numeric leaf across repeats (structure of runs[0])."""
    def merge(values):
        first = values[0]
        if isinstance(first, dict):
            return {k: merge([v[k] for v in values if k in v]) for k in first}
        if isinstance(first, (int, float)) and not isinstance(first, bool):
            return statistics.median(values)
        return first
    return merge(runs)


def pct(new, old):
    return 0.0 if not old else (new - old) / old * 100.0


def is_regression(new, old):
    """Gate on p50 and p95 only. p99 of 300 frames is the 3rd-worst frame:
    scenarios with a few legitimate spikes per run (pool churn) flip it
    between modes run to run, so p99/max are reported, never gated."""
    flags = []
    for series in ("frame_ms", "factory_tick_ms"):
        n, o = new.get(series, {}), old.get(series, {})
        if not n or not o:
            continue
        if n["p50"] > o["p50"] * 1.10 and n["p50"] - o["p50"] > 0.05:
            flags.append(f"{series}.p50 {o['p50']:.3f}->{n['p50']:.3f} ms ({pct(n['p50'], o['p50']):+.0f}%)")
        if n["p95"] > o["p95"] * 1.20 and n["p95"] - o["p95"] > 0.3:
            flags.append(f"{series}.p95 {o['p95']:.3f}->{n['p95']:.3f} ms ({pct(n['p95'], o['p95']):+.0f}%)")
    return flags


def write_latest(run, baseline):
    lines = [
        "# BlastBullets2D benchmark: latest run",
        "",
        f"- run: `{run['timestamp']}` commit `{run['commit']}`{' (dirty src/)' if run['dirty'] else ''}",
        f"- machine: {run['machine']['cpu']} ({run['machine']['cores']} threads), {run['machine']['os']}",
        f"- godot: {run.get('godot', '?')}, debug build: {run.get('debug_build', '?')}, repeats: {run['repeat']} (median)",
        f"- baseline: `{baseline.get('commit', 'none')}` ({baseline.get('timestamp', '-')})" if baseline else "- baseline: none (run with --update-baseline)",
        "",
        "Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).",
        "`frame` = whole engine frame; `tick` = BulletFactory2D physics tick only.",
        "",
        "| scenario | frame p50 | frame p99 | frame max | tick p50 | tick p99 | bullets | mem peak MB | vs baseline |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---|",
    ]
    regressions = {}
    for name, res in run["scenarios"].items():
        base = (baseline or {}).get("scenarios", {}).get(name)
        f, t = res["frame_ms"], res["factory_tick_ms"]
        if base:
            flags = is_regression(res, base)
            regressions[name] = flags
            delta = (f"frame p50 {pct(f['p50'], base['frame_ms']['p50']):+.0f}%, "
                     f"tick p50 {pct(t['p50'], base['factory_tick_ms']['p50']):+.0f}%")
            delta = ("**REGRESSION**: " + "; ".join(flags)) if flags else delta
        else:
            delta = "new"
        lines.append(f"| {name} | {f['p50']:.3f} | {f['p99']:.3f} | {f['max']:.3f} | {t['p50']:.3f} | {t['p99']:.3f} | "
                     f"{res['active_bullets_mean']:.0f} | {res['memory_static_peak_mb']:.1f} | {delta} |")
    extras = [(n, r["extra"]) for n, r in run["scenarios"].items() if r.get("extra")]
    if extras:
        lines += ["", "## Scenario extras", ""]
        for name, extra in extras:
            parts = ", ".join(f"{k}={v:.3f}" if isinstance(v, float) else f"{k}={v}" for k, v in sorted(extra.items()))
            lines.append(f"- **{name}**: {parts}")
    lines += ["", "## Scenarios", ""]
    for name, res in run["scenarios"].items():
        lines.append(f"- **{name}**: {res.get('description', '')}")
    with open(LATEST, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    return regressions


def append_history(run):
    new_file = not os.path.exists(HISTORY)
    with open(HISTORY, "a", newline="", encoding="utf-8") as handle:
        w = csv.writer(handle, lineterminator="\n")
        if new_file:
            w.writerow(["timestamp", "commit", "dirty", "scenario", "frame_p50", "frame_p99", "frame_max",
                        "tick_p50", "tick_p99", "bullets_mean", "mem_peak_mb", "cpu"])
        for name, r in run["scenarios"].items():
            w.writerow([run["timestamp"], run["commit"], int(run["dirty"]), name,
                        f"{r['frame_ms']['p50']:.4f}", f"{r['frame_ms']['p99']:.4f}", f"{r['frame_ms']['max']:.4f}",
                        f"{r['factory_tick_ms']['p50']:.4f}", f"{r['factory_tick_ms']['p99']:.4f}",
                        f"{r['active_bullets_mean']:.0f}", f"{r['memory_static_peak_mb']:.2f}", run["machine"]["cpu"]])


def main():
    ap = argparse.ArgumentParser(description="Run BlastBullets2D headless benchmarks.")
    ap.add_argument("--scenario", action="append", help="substring filter (repeatable)")
    ap.add_argument("--repeat", type=int, default=5)
    ap.add_argument("--timeout", type=int, default=600)
    ap.add_argument("--gate", action="store_true", help="exit 1 when any scenario regresses vs baseline")
    ap.add_argument("--update-baseline", action="store_true", help="store this run as baseline.json")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--allow-stale", action="store_true")
    args = ap.parse_args()

    scenarios = discover()
    if args.scenario:
        scenarios = [s for s in scenarios if any(p in s for p in args.scenario)]
    if args.list:
        print("\n".join(scenarios))
        return 0
    godot = godot_binary()
    stale = stale_binary_reason()
    if stale and not args.allow_stale:
        print(f"refusing to run: {stale}")
        return 2
    if not refresh_class_cache(godot):
        return 2
    sha, dirty = git_info()
    run = {"timestamp": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H-%M-%SZ"), "commit": sha, "dirty": dirty,
           "repeat": args.repeat, "machine": machine_info(), "scenarios": {}}
    print(f"godot: {godot}   commit: {sha}{' (dirty)' if dirty else ''}   repeats: {args.repeat}")
    failed = []
    for name in scenarios:
        try:
            runs = [run_once(godot, name, args.timeout) for _ in range(max(1, args.repeat))]
        except (RuntimeError, subprocess.TimeoutExpired) as exc:
            print(f"  FAIL {name}: {exc}")
            failed.append(name)
            continue
        merged = median_merge(runs)
        run["godot"] = merged.pop("godot", "")
        run["debug_build"] = merged.pop("debug_build", None)
        merged.pop("scenario", None)
        run["scenarios"][name] = merged
        f, t = merged["frame_ms"], merged["factory_tick_ms"]
        print(f"  {name:<34} frame p50 {f['p50']:7.3f}  p99 {f['p99']:7.3f}  max {f['max']:7.3f} | "
              f"tick p50 {t['p50']:7.3f}  p99 {t['p99']:7.3f} ms")
    os.makedirs(RESULTS_DIR, exist_ok=True)
    out = os.path.join(RESULTS_DIR, f"{run['timestamp']}_{sha}{'-dirty' if dirty else ''}.json")
    with open(out, "w", encoding="utf-8") as handle:
        json.dump(run, handle, indent=2)
    append_history(run)
    baseline = None
    if os.path.exists(BASELINE):
        with open(BASELINE, encoding="utf-8") as handle:
            baseline = json.load(handle)
    regressions = write_latest(run, baseline)
    if args.update_baseline:
        with open(BASELINE, "w", encoding="utf-8") as handle:
            json.dump(run, handle, indent=2)
        print(f"baseline updated -> {os.path.relpath(BASELINE, REPO_ROOT)}")
    print(f"results : {os.path.relpath(out, REPO_ROOT)}")
    print(f"summary : {os.path.relpath(LATEST, REPO_ROOT)}")
    bad = {k: v for k, v in regressions.items() if v}
    for name, flags in bad.items():
        print(f"  REGRESSION {name}: " + "; ".join(flags))
    if failed:
        return 1
    if args.gate and bad:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
