#!/usr/bin/env python3
"""Headless test runner for the BlastBullets2D suites.

Every suite is a `SceneTree` script run as
    godot --headless --path test_project --script <path>
and calls `quit(failures)`, so the process exit code IS the failure count.

A zero exit code is NOT sufficient to call a suite green: a stale or missing
GDExtension .so makes Godot abort at startup, and a suite that dies mid-run can
still exit 0 in some crash modes. So each suite must also print its own
completion marker. Suites that print no marker are reported as CRASH, which is
what catches "the binary was never rebuilt" - the failure mode that silently
turns `debug_get_*` assertions into assertions against defaults.

Usage:
    python3 tools/run_tests.py                    # every suite
    python3 tools/run_tests.py --suite 'volley/*' # substring/glob filter
    python3 tools/run_tests.py --list             # show discovered suites
    python3 tools/run_tests.py --changed-only     # suites touching changed C++
    python3 tools/run_tests.py --keep-going       # do not stop at the first failure
    python3 tools/run_tests.py --timeout 300      # per-suite wall clock seconds
"""

import argparse
import fnmatch
import glob
import os
import re
import subprocess
import sys
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(REPO_ROOT, "test_project")
GODOT = os.environ.get("GODOT_BIN", "godot")

# test_edge_fuzz.gd is a multi-group suite: it reads the CASE env var and
# quits(2) on an unknown value, so running it once without CASE is a false
# failure. Each group runs as its own process - a crash kills only its group,
# and the missing GROUP_DONE line attributes it.
EDGE_FUZZ_SUITE = "test_edge_fuzz.gd"
EDGE_FUZZ_GROUPS = (
    "counts",
    "degenerate",
    "nan",
    "twist_extreme",
    "slots_offsets",
    "scales",
    "edge_image",
    "side_spread_skip",
    "semantics",
)

# A suite is only green if it emits its own done-marker. The house style is
# "ALL <NAME> TESTS PASSED"; the legacy root-level suites use GROUP_DONE.
DONE_MARKERS = ("TESTS PASSED", "GROUP_DONE", "ALL TESTS PASSED")

# C++ sources whose changes can invalidate a given suite. Deliberately coarse:
# anything shared touches everything, so map the volatile areas broadly.
VOLATILE_SUFFIXES = (
    "multimesh_bullets2d",
    "directional_bullets2d",
    "block_bullets2d",
    "bullet_factory2d",
    "bullet_spawner2d",
    "bullet_attachment2d",
    "multimesh_object_pool2d",
    "bullet_effect_layer_data2d",
    "bullet_speed_data2d",
    "bullet_rotation_data2d",
    "bullet_wobble_data2d",
    "bullet_curves_data2d",
    "bullet_deque2d",
    "bullet_homing",
    "multimesh_pool_key2d",
    "register_types",
)


def is_suite_script(path):
    """A suite is a SceneTree script. This deliberately skips the shared
    helper (tests/common) and the attachment fixture (tests/scenes), which
    live under tests/ but are libraries, not runnable suites."""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            head = handle.read(4096)
    except OSError:
        return False
    return re.search(r"^\s*extends\s+SceneTree\b", head, re.M) is not None


def discover():
    """Every suite: tests/** plus the legacy root-level scripts."""
    suites = []
    candidates = sorted(glob.glob(os.path.join(PROJECT, "tests", "**", "*.gd"), recursive=True))
    candidates += sorted(glob.glob(os.path.join(PROJECT, "*.gd")))
    for path in candidates:
        if not is_suite_script(path):
            continue
        rel = os.path.relpath(path, PROJECT)
        if rel == EDGE_FUZZ_SUITE:
            # One entry per group, so each runs in its own process with CASE set.
            for group in EDGE_FUZZ_GROUPS:
                suites.append(f"{rel}::{group}")
            continue
        suites.append(rel)
    return suites


def split_suite(entry):
    """-> (script_path, env_overrides)"""
    if "::" in entry:
        script, group = entry.split("::", 1)
        return script, {"CASE": group}
    return entry, {}


def changed_cpp_files():
    try:
        out = subprocess.run(
            ["git", "status", "--porcelain", "--", "src", "test_project"],
            cwd=REPO_ROOT, capture_output=True, text=True, timeout=30,
        ).stdout
    except (OSError, subprocess.SubprocessError):
        return set()
    files = set()
    for line in out.splitlines():
        # porcelain: XY path  (rename entries carry "old -> new")
        if len(line) < 4:
            continue
        rel = line[3:].strip().strip('"')
        if " -> " in rel:
            rel = rel.split(" -> ")[-1]
        files.add(rel)
    return files


def volatile_prefixes(changed):
    """Distinctive stems touched by the change (empty set = everything is suspect)."""
    hits = set()
    for rel in changed:
        stem = os.path.splitext(os.path.basename(rel))[0]
        for suffix in VOLATILE_SUFFIXES:
            if stem == suffix or stem.startswith(suffix):
                hits.add(suffix)
    return hits


def is_suite_affected(entry, changed, prefixes):
    """Conservative: assume a touched C++ area can affect any suite that
    references its class name, and that a touched .gd only affects itself."""
    if not changed:
        return True
    suite = entry.split("::", 1)[0]
    if suite in changed:
        return True
    haystack = suite.replace("/", "_")
    for prefix in prefixes:
        if prefix in haystack:
            return True
    return False


def run_suite(entry, timeout):
    script, env_overrides = split_suite(entry)
    cmd = [GODOT, "--headless", "--path", PROJECT, "--script", script]
    env = dict(os.environ)
    env.update(env_overrides)
    start = time.monotonic()
    try:
        proc = subprocess.run(
            cmd, cwd=REPO_ROOT, capture_output=True, text=True,
            timeout=timeout, errors="replace", env=env,
        )
        stdout, stderr, code = proc.stdout, proc.stderr, proc.returncode
    except subprocess.TimeoutExpired:
        return {
            "suite": entry, "status": "TIMEOUT", "code": None,
            "elapsed": time.monotonic() - start, "output": "",
        }
    except OSError as exc:
        return {
            "suite": entry, "status": "ERROR", "code": None,
            "elapsed": time.monotonic() - start, "output": str(exc),
        }
    elapsed = time.monotonic() - start
    blob = stdout + stderr
    saw_done = any(marker in blob for marker in DONE_MARKERS)

    if code == 0 and saw_done:
        status = "PASS"
    elif code == 0 and not saw_done:
        # Exited clean but never announced completion: the binary probably
        # never loaded, or the script died before its tail. NOT a pass.
        status = "CRASH"
    elif code is not None and code < 0:
        status = "CRASH"
    else:
        status = "FAIL"

    return {
        "suite": entry, "status": status, "code": code,
        "elapsed": elapsed, "output": blob,
    }


def summarise(result):
    lines = result["output"].splitlines()
    fails = [ln for ln in lines if ln.startswith("FAIL")]
    tail = [ln for ln in lines if ln.strip()][-14:]
    return fails, tail


def main():
    ap = argparse.ArgumentParser(description="Run the headless BlastBullets2D suites.")
    ap.add_argument("--suite", action="append", default=None,
                    help="only suites whose path contains this (repeatable)")
    ap.add_argument("--list", action="store_true", help="list suites and exit")
    ap.add_argument("--changed-only", action="store_true",
                    help="only suites plausibly affected by uncommitted changes")
    ap.add_argument("--keep-going", action="store_true", default=True,
                    help="run every suite even after a failure (default)")
    ap.add_argument("--fail-fast", dest="keep_going", action="store_false",
                    help="stop at the first failing suite")
    ap.add_argument("--timeout", type=int, default=600, help="per-suite seconds")
    ap.add_argument("--verbose", action="store_true", help="print full output")
    args = ap.parse_args()

    suites = discover()

    if args.suite:
        patterns = args.suite
        suites = [
            s for s in suites
            if any(p in s or fnmatch.fnmatch(s, p) for p in patterns)
        ]

    if args.changed_only:
        changed = changed_cpp_files()
        prefixes = volatile_prefixes(changed)
        if not changed:
            print("note: no uncommitted changes under src/ or test_project/; running everything")
            suites = discover()
        else:
            before = len(suites)
            suites = [s for s in suites if is_suite_affected(s, changed, prefixes)]
            print(f"note: {len(changed)} changed file(s), stems={sorted(prefixes) or ['(none)']}")
            print(f"note: {before} suite(s) narrowed to {len(suites)}")
        if args.suite:
            pass

    if args.list:
        for s in suites:
            print(s)
        return 0

    if not suites:
        print("no suites matched")
        return 1

    print(f"godot      : {GODOT}")
    print(f"project    : {os.path.relpath(PROJECT, REPO_ROOT)}")
    print(f"suites     : {len(suites)}")
    print("-" * 72)

    results = []
    for i, suite in enumerate(suites, 1):
        result = run_suite(suite, args.timeout)
        results.append(result)
        fails, _ = summarise(result)
        detail = f"{len(fails)} failed" if fails else ""
        if result["status"] == "FAIL" and not detail:
            detail = f"exit {result['code']}"
        print(f"[{i:>2}/{len(suites)}] {result['status']:<7} {result['elapsed']:>6.1f}s  "
              f"{suite}  {detail}".rstrip())
        sys.stdout.flush()
        if args.verbose:
            print(result["output"])
        if result["status"] != "PASS" and not args.keep_going:
            print("stopping (--fail-fast)")
            break

    print("-" * 72)
    by_status = {}
    for r in results:
        by_status.setdefault(r["status"], []).append(r["suite"])

    for status in ("PASS", "FAIL", "CRASH", "TIMEOUT", "ERROR"):
        if status in by_status:
            print(f"{status:<8} {len(by_status[status])}")

    broken = [r for r in results if r["status"] != "PASS"]
    if broken:
        print()
        print("=" * 72)
        for r in broken:
            fails, tail = summarise(r)
            print(f"\n### {r['status']}  {r['suite']}  (exit={r['code']}, {r['elapsed']:.1f}s)")
            if fails:
                print(f"  {len(fails)} FAIL line(s):")
                for ln in fails[:40]:
                    print(f"    {ln}")
            for ln in tail:
                print(f"  | {ln}")
        print("=" * 72)

    total_fails = sum(len(summarise(r)[0]) for r in results)
    print()
    if not broken and total_fails == 0:
        print(f"ALL {len(results)} SUITES PASSED")
        return 0
    print(f"{len(broken)} suite(s) not green, {total_fails} individual FAIL line(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
