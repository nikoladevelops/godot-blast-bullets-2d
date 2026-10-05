#!/usr/bin/env python3
"""Behavior snapshot for BlastBullets2D refactors (the "did anything change?" proof).

A pure refactor must not change anything a user can observe. This tool runs
test_project/snapshot/api_snapshot.tscn headless and stores what the plugin
does as short hashes plus the exact error/warning texts each call produced:

  surface   ClassDB surface of every plugin class (methods + argument names,
            types and default values, properties with hints/usage in order,
            signals, enum constants)
  patterns  every static BulletPatterns2D helper over defaults, amounts,
            markers, one-argument perturbations and seeded combinations
  generate  BulletPatterns2D.generate for every shape id
  spawner   every pattern source x every inspector-visible knob (spawn
            transforms + preview dots/track/rings) and every preset
  setters   hostile setter probes (NaN, INF, negatives, huge) for spawner,
            volley and every data resource: value read back + errors
  traces    deterministic volley flights (factory.debug_advance_time) for
            plain, accel, rotation, curves, wobble, gravity, homing (per
            bullet, shared, mixed), orbit, Path2D patterns and pool reuse

Usage:
    python3 tools/api_snapshot.py save <label>          # -> test_project/test_results/snapshots/<label>.json
    python3 tools/api_snapshot.py save <label> --only spawner,traces
    python3 tools/api_snapshot.py diff <label_a> <label_b>   # exit 1 when anything differs
    python3 tools/api_snapshot.py list

Workflow for a refactor: build, `save before`, change code, build,
`save after`, `diff before after` must print "IDENTICAL". A deliberate
behavior change shows up as a listed difference: name each one in the commit.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))
from run_tests import godot_binary, refresh_class_cache, stale_binary_reason  # noqa: E402

PROJECT = os.path.join(REPO_ROOT, "test_project")
SNAP_DIR = os.path.join(PROJECT, "test_results", "snapshots")


def snap_path(label):
    return os.path.join(SNAP_DIR, f"{label}.json")


def save(label, only, timeout):
    reason = stale_binary_reason()
    if reason:
        print(f"refusing to snapshot: {reason}")
        return 2
    godot = godot_binary()
    if not refresh_class_cache(godot):
        print("class cache refresh (godot --import) failed")
        return 2
    os.makedirs(SNAP_DIR, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="blast_snap_") as tmp:
        out = os.path.join(tmp, "snapshot.json")
        cmd = [godot, "--headless", "--fixed-fps", "60", "--quit-after", "2000000", "--path", PROJECT,
               "res://snapshot/api_snapshot.tscn", "--", f"--out={out}"]
        if only:
            cmd.append(f"--only={only}")
        proc = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True, errors="replace", timeout=timeout)
        if proc.returncode != 0 or not os.path.exists(out):
            print("\n".join((proc.stdout + proc.stderr).splitlines()[-40:]))
            print(f"snapshot failed (exit {proc.returncode})")
            return 2
        for line in (proc.stdout + proc.stderr).splitlines():
            if "SCRIPT ERROR" in line or "Parse Error" in line:
                print("snapshot script error:", line)
                return 2
        with open(out, encoding="utf-8") as handle:
            data = json.load(handle)
    with open(snap_path(label), "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=1, sort_keys=True)
    counts = ", ".join(f"{k}={len(v)}" for k, v in data.items())
    print(f"saved {snap_path(label)} ({counts})")
    return 0


def describe(entry):
    errs = entry.get("e", [])
    tail = f" errors={errs}" if errs else ""
    return f"{entry.get('s', '')}{tail}"


def diff(label_a, label_b, limit):
    with open(snap_path(label_a), encoding="utf-8") as handle:
        a = json.load(handle)
    with open(snap_path(label_b), encoding="utf-8") as handle:
        b = json.load(handle)
    total = 0
    for section in sorted(set(a) | set(b)):
        sa, sb = a.get(section), b.get(section)
        if sa is None or sb is None:
            print(f"[{section}] only in {'b' if sa is None else 'a'} (sections must match: same --only)")
            total += 1
            continue
        changed = [k for k in sorted(set(sa) & set(sb)) if sa[k].get("h") != sb[k].get("h") or sa[k].get("e") != sb[k].get("e")]
        only_a = sorted(set(sa) - set(sb))
        only_b = sorted(set(sb) - set(sa))
        n = len(changed) + len(only_a) + len(only_b)
        total += n
        if n == 0:
            print(f"[{section}] identical ({len(sa)} entries)")
            continue
        print(f"[{section}] {len(changed)} changed, {len(only_a)} removed, {len(only_b)} added (of {len(sa)})")
        shown = 0
        for k in changed:
            if shown >= limit:
                break
            print(f"  ~ {k}\n      a: {describe(sa[k])}\n      b: {describe(sb[k])}")
            shown += 1
        for k in only_a[: max(0, limit - shown)]:
            print(f"  - {k}: {describe(sa[k])}")
        for k in only_b[: max(0, limit - shown)]:
            print(f"  + {k}: {describe(sb[k])}")
    print("IDENTICAL" if total == 0 else f"{total} DIFFERENCE(S)")
    return 0 if total == 0 else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    p_save = sub.add_parser("save")
    p_save.add_argument("label")
    p_save.add_argument("--only", default="", help="comma-separated sections")
    p_save.add_argument("--timeout", type=int, default=1800)
    p_diff = sub.add_parser("diff")
    p_diff.add_argument("a")
    p_diff.add_argument("b")
    p_diff.add_argument("--limit", type=int, default=25, help="differences printed per section")
    sub.add_parser("list")
    args = parser.parse_args()
    if args.cmd == "save":
        return save(args.label, args.only, args.timeout)
    if args.cmd == "diff":
        return diff(args.a, args.b, args.limit)
    for name in sorted(os.listdir(SNAP_DIR)) if os.path.isdir(SNAP_DIR) else []:
        print(name[:-5])
    return 0


if __name__ == "__main__":
    sys.exit(main())
