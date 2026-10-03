#!/usr/bin/env python3
"""Headless GUT test runner for BlastBullets2D (leak-checked).

Every test file runs in its own headless Godot process, in parallel:

    godot --headless --verbose --fixed-fps 60 --path test_project \\
          -s addons/gut/gut_cmdln.gd -gconfig= -gtest=<file> -gexit \\
          -gjunit_xml_file=<tmp>

(no -d: the local debugger would block on stdin at the first script error;
--fixed-fps 60: simulated time, frame-identical and ~10x faster.)

A file is green only when ALL of these hold:
  1. the process exits 0,
  2. GUT wrote its JUnit report with 0 failures (a missing report means the
     extension or GUT never loaded - reported as CRASH, never as PASS),
  3. the --verbose exit report shows no leaks (ObjectDB instances, leaked
     RIDs, resources still in use, orphan StringNames),
  4. no SCRIPT ERROR / parse error was printed.

GUT itself fails a test on any unexpected engine error or push_error
(strict mode), and the BlastTest base class asserts zero new orphans and a
dangling-free factory after every test.

Usage:
    python3 tools/run_tests.py                 # every test file
    python3 tools/run_tests.py --suite volley  # substring/glob filter (repeatable)
    python3 tools/run_tests.py --list          # discover only
    python3 tools/run_tests.py --changed-only  # files plausibly affected by uncommitted changes
    python3 tools/run_tests.py --jobs 4        # parallel processes (default: CPU count)
    python3 tools/run_tests.py --fail-fast     # stop scheduling after the first red file
    python3 tools/run_tests.py --no-leaks      # skip --verbose leak checking (faster, not "green")
    python3 tools/run_tests.py --verbose       # print full output of red files
    python3 tools/run_tests.py --self-test     # prove failure + leak detection still fire
    python3 tools/run_tests.py --allow-stale   # run even if src/ is newer than the built .so
    python3 tools/run_tests.py --report        # write test_project/test_results/summary.json
    python3 tools/run_tests.py --realtime      # real-time pacing (default: simulated --fixed-fps 60)
    python3 tools/run_tests.py --no-lint       # skip tools/lint_tests.py (not green)

Godot binary: $GODOT_BIN, else tools/config.json "godotActivePath", else `godot`.
"""

import argparse
import concurrent.futures
import fnmatch
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(REPO_ROOT, "test_project")
TESTS_DIR = os.path.join(PROJECT, "tests")
META_DIR = os.path.join(PROJECT, "tests_meta")
GUT_CLI = "addons/gut/gut_cmdln.gd"
ADDON_BIN_DIR = os.path.join(PROJECT, "addons", "blastbullets2d", "bin")

# Godot 4.5+ exit-time leak report lines (printed with --verbose; the summary
# lines appear even without it). Any match makes the file red.
LEAK_PATTERNS = [
    re.compile(r"ObjectDB instances (?:were )?leaked at exit"),
    re.compile(r"^Leaked instance:", re.M),
    re.compile(r"RID allocations of type .* were leaked at exit"),
    re.compile(r"RIDs? of type .* (?:was|were) leaked"),
    re.compile(r"resources? still in use at exit", re.I),
    re.compile(r"Orphan StringName", re.I),
]
SCRIPT_ERROR_PATTERNS = [
    re.compile(r"SCRIPT ERROR"),
    re.compile(r"Parse Error"),
    re.compile(r"Failed to load script"),
    # Extension registration errors (e.g. ADD_PROPERTY before its setter was
    # bound) only print at class registration and never fail a test: the
    # property silently does not exist. Treat them as red.
    re.compile(r"godot-cpp/src/core/class_db\.cpp"),
]

# C++ stems -> test areas, for --changed-only. Deliberately coarse.
AREA_BY_STEM = {
    "bullet_spawner2d": ("spawner", "integration"),
    "volley_tracker2d": ("spawner",),
    "bullet_factory2d": ("factory", "pooling", "spawner", "volley", "integration", "fuzz"),
    "volley_pool2d": ("pooling", "factory"),
    "volley_pool_key2d": ("pooling",),
    "bullet_attachment": ("pooling", "volley"),
}


def godot_binary():
    env = os.environ.get("GODOT_BIN")
    if env:
        return env
    try:
        with open(os.path.join(REPO_ROOT, "tools", "config.json"), encoding="utf-8") as handle:
            path = json.load(handle).get("godotActivePath")
        if path and os.path.exists(path):
            return path
    except (OSError, ValueError):
        pass
    return "godot"


def discover(root=TESTS_DIR):
    found = []
    for dirpath, _, files in os.walk(root):
        for name in files:
            if name.startswith("test_") and name.endswith(".gd"):
                found.append(os.path.relpath(os.path.join(dirpath, name), PROJECT).replace(os.sep, "/"))
    return sorted(found)


def changed_files():
    try:
        out = subprocess.run(["git", "status", "--porcelain", "--", "src", "test_project/tests"],
                             cwd=REPO_ROOT, capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.SubprocessError):
        return set()
    files = set()
    for line in out.splitlines():
        if len(line) < 4:
            continue
        rel = line[3:].strip().strip('"')
        if " -> " in rel:
            rel = rel.split(" -> ")[-1]
        files.add(rel)
    return files


def filter_changed(tests):
    changed = changed_files()
    if not changed:
        print("note: no uncommitted changes under src/ or tests/; running everything")
        return tests
    areas = set()
    for rel in changed:
        if rel.startswith("src/"):
            stem = os.path.splitext(os.path.basename(rel))[0]
            hit = [a for s, a in AREA_BY_STEM.items() if stem.startswith(s)]
            if not hit:
                return tests  # shared/core code: everything is suspect
            for group in hit:
                areas.update(group)
    picked = [t for t in tests
              if ("test_project/" + t) in changed or any(f"/{a}/" in t or t.startswith(f"tests/{a}/") for a in areas)]
    print(f"note: {len(changed)} changed file(s); {len(tests)} test file(s) narrowed to {len(picked)}")
    return picked


def newest_mtime(root, exts):
    newest = 0.0
    for dirpath, _, files in os.walk(root):
        for name in files:
            if name.endswith(exts):
                newest = max(newest, os.path.getmtime(os.path.join(dirpath, name)))
    return newest


def stale_binary_reason():
    built = newest_mtime(ADDON_BIN_DIR, (".so", ".dll", ".dylib"))
    if built == 0.0:
        return "no compiled extension found in test_project/addons/blastbullets2d/bin"
    # SCons recompiles objects (.os) after any source edit but skips the link
    # when the object bytes are unchanged (comment-only edits): the newest
    # object therefore also proves the build ran after the edit.
    built = max(built, newest_mtime(os.path.join(REPO_ROOT, "src"), (".os", ".o", ".obj")))
    source = newest_mtime(os.path.join(REPO_ROOT, "src"), (".cpp", ".hpp", ".h"))
    if source > built:
        return "src/ is newer than the compiled extension (rebuild: GODOTPP_NONINTERACTIVE=1 python3 tools/compile_debug_build.py)"
    return None


def refresh_class_cache(godot):
    """New/renamed `class_name` scripts (BlastTest, GutTest, fixtures) only
    resolve after Godot rebuilds .godot/global_script_class_cache.cfg. A
    headless --import does that; run it whenever any project script is newer
    than the cache (cheap check, slow-ish import only when needed)."""
    cache = os.path.join(PROJECT, ".godot", "global_script_class_cache.cfg")
    cache_mtime = os.path.getmtime(cache) if os.path.exists(cache) else 0.0
    newest = 0.0
    for sub in ("tests", "tests_meta", "addons", "shared", "benchmarks"):
        root = os.path.join(PROJECT, sub)
        if os.path.isdir(root):
            newest = max(newest, newest_mtime(root, (".gd",)))
    if newest <= cache_mtime:
        return True
    print("note: scripts changed since the last import; refreshing the class cache (godot --import)")
    proc = subprocess.run([godot, "--headless", "--path", PROJECT, "--import"],
                          cwd=REPO_ROOT, capture_output=True, text=True, errors="replace", timeout=600)
    if proc.returncode != 0:
        print(proc.stdout[-2000:] + proc.stderr[-2000:])
        return False
    # Touch so an import that rewrote nothing still marks the cache fresh.
    if os.path.exists(cache):
        os.utime(cache, None)
    return True


# Failure text is clipped per test unless --full (parameterized tests put
# every failing parameter into one message).
DETAIL_LIMIT = 400


def parse_junit(path):
    """-> (tests, failures, [(test_name, message)]) or None when missing/unreadable."""
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError):
        return None
    tests = failures = 0
    details = []
    for case in root.iter("testcase"):
        tests += 1
        # Parameterized tests record one <failure> per failing parameter.
        nodes = case.findall("failure") + case.findall("error")
        if nodes or case.get("status") == "fail":
            failures += 1
            texts = [(n.text or n.get("message") or "failed").strip() for n in nodes] or ["failed"]
            flat = " | ".join(" ".join(t.split()) for t in texts)
            details.append((case.get("name", "?"), flat if DETAIL_LIMIT is None else flat[:DETAIL_LIMIT]))
    return tests, failures, details


def run_file(godot, test, timeout, check_leaks, fixed_fps=60):
    with tempfile.TemporaryDirectory(prefix="blast_gut_") as tmp:
        junit = os.path.join(tmp, "results.xml")
        cmd = [godot, "--headless"]
        if check_leaks:
            cmd.append("--verbose")
        # Simulated time: every frame advances exactly 1/fixed_fps seconds and
        # the main loop runs as fast as the CPU allows (no real-time pacing).
        # Frame-counted tests behave identically, wall time drops ~10x.
        if fixed_fps > 0:
            cmd += ["--fixed-fps", str(fixed_fps)]
        # No "-d": the local debugger would stop on the first script error
        # and wait for stdin forever (a hang, not a failure).
        cmd += ["--path", PROJECT, "-s", GUT_CLI, "-gconfig=", f"-gtest=res://{test}",
                "-gexit", "-gdisable_colors", f"-gjunit_xml_file={junit}"]
        start = time.monotonic()
        try:
            proc = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True,
                                  errors="replace", timeout=timeout)
            output, code = proc.stdout + proc.stderr, proc.returncode
        except subprocess.TimeoutExpired as exc:
            out = (exc.stdout or b"") + (exc.stderr or b"")
            output = out.decode("utf-8", "replace") if isinstance(out, bytes) else out
            return {"test": test, "status": "TIMEOUT", "elapsed": time.monotonic() - start,
                    "tests": 0, "failures": 0, "details": [], "leaks": [], "script_errors": [], "output": output}
        elapsed = time.monotonic() - start
        report = parse_junit(junit)

    leaks = []
    if check_leaks:
        for line in output.splitlines():
            if any(p.search(line) for p in LEAK_PATTERNS):
                leaks.append(line.strip())
    script_errors = [ln.strip() for ln in output.splitlines() if any(p.search(ln) for p in SCRIPT_ERROR_PATTERNS)]

    if report is None:
        status, tests, failures, details = "CRASH", 0, 0, []
    else:
        tests, failures, details = report
        if failures or code != 0:
            status = "FAIL"
        elif tests == 0:
            status = "EMPTY"
        elif leaks:
            status = "LEAK"
        elif script_errors:
            status = "SCRIPT"
        else:
            status = "PASS"
    return {"test": test, "status": status, "elapsed": elapsed, "tests": tests, "failures": failures,
            "details": details, "leaks": leaks, "script_errors": script_errors, "output": output, "code": code}


def run_many(godot, tests, jobs, timeout, check_leaks, fail_fast, verbose, fixed_fps=60):
    results = []
    stop = False
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        pending = {}
        queue = list(tests)
        while queue or pending:
            while queue and len(pending) < jobs and not stop:
                test = queue.pop(0)
                pending[pool.submit(run_file, godot, test, timeout, check_leaks, fixed_fps)] = test
            if not pending:
                break
            done, _ = concurrent.futures.wait(pending, return_when=concurrent.futures.FIRST_COMPLETED)
            for fut in done:
                pending.pop(fut)
                r = fut.result()
                results.append(r)
                extra = ""
                if r["failures"]:
                    extra = f"{r['failures']} failed"
                elif r["leaks"]:
                    extra = f"{len(r['leaks'])} leak line(s)"
                print(f"[{len(results):>3}/{len(tests)}] {r['status']:<7} {r['elapsed']:>6.1f}s  "
                      f"{r['test']}  ({r['tests']} tests) {extra}".rstrip(), flush=True)
                if verbose and r["status"] != "PASS":
                    print(r["output"])
                if r["status"] != "PASS" and fail_fast:
                    stop = True
                    queue.clear()
    return results


def report(results):
    print("-" * 72)
    counts = {}
    for r in results:
        counts[r["status"]] = counts.get(r["status"], 0) + 1
    for status in ("PASS", "FAIL", "LEAK", "SCRIPT", "CRASH", "TIMEOUT", "EMPTY"):
        if status in counts:
            print(f"{status:<8} {counts[status]}")
    total_tests = sum(r["tests"] for r in results)
    print(f"tests    {total_tests}")
    broken = [r for r in results if r["status"] != "PASS"]
    if broken:
        print("=" * 72)
        for r in sorted(broken, key=lambda x: x["test"]):
            print(f"\n### {r['status']}  {r['test']}  (exit={r.get('code')}, {r['elapsed']:.1f}s)")
            for name, msg in r["details"][:40]:
                print(f"  FAIL {name}: {msg}")
            for line in r["leaks"][:20]:
                print(f"  LEAK {line}")
            for line in r["script_errors"][:10]:
                print(f"  SCRIPT {line}")
            if r["status"] in ("CRASH", "TIMEOUT", "EMPTY"):
                tail = [ln for ln in r["output"].splitlines() if ln.strip()][-15:]
                for ln in tail:
                    print(f"  | {ln}")
        print("=" * 72)
        print(f"\n{len(broken)} file(s) not green")
        return 1
    print(f"\nALL {len(results)} TEST FILES PASSED ({total_tests} tests, no leaks)")
    return 0


def write_report(path, results, fixed_fps):
    """Machine-readable run summary (slowest files first) for agents/CI."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    rows = [{"file": r["test"], "status": r["status"], "seconds": round(r["elapsed"], 3),
             "tests": r["tests"], "failures": [{"test": n, "message": m} for n, m in r["details"]],
             "leaks": r["leaks"][:20]} for r in sorted(results, key=lambda x: -x["elapsed"])]
    payload = {"generated_unix": int(time.time()), "fixed_fps": fixed_fps,
               "files": len(results), "tests": sum(r["tests"] for r in results),
               "green": all(r["status"] == "PASS" for r in results), "results": rows}
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2)
    print(f"report  : {path}")


def self_test(godot, timeout):
    """The canaries in tests_meta/ MUST be detected: a failing assert, an
    unexpected push_error, leaked Object/Node/RID at exit, and every precise
    error helper (expect_error_sequence / exact expect_errors_containing /
    expect_no_errors) failing on a mismatch."""
    ok = True
    strict = run_file(godot, "tests_meta/test_strict_helpers_canary.gd", timeout, True)
    if strict["status"] != "FAIL" or strict["failures"] != strict["tests"] or strict["tests"] < 5:
        print(f"SELF-TEST BROKEN: strict-helper canary reported {strict['status']} "
              f"({strict['failures']}/{strict['tests']} failing; every test must fail)")
        ok = False
    fail = run_file(godot, "tests_meta/test_fail_canary.gd", timeout, True)
    if fail["status"] != "FAIL" or fail["failures"] < 2:
        print(f"SELF-TEST BROKEN: failure canary reported {fail['status']} ({fail['failures']} failures)")
        ok = False
    leak = run_file(godot, "tests_meta/test_leak_canary.gd", timeout, True)
    if leak["status"] != "LEAK" or not leak["leaks"]:
        print(f"SELF-TEST BROKEN: leak canary reported {leak['status']}")
        ok = False
    print("SELF-TEST OK: failures, strict-helper mismatches and leaks are detected" if ok else "SELF-TEST FAILED")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description="Run the BlastBullets2D GUT suites headless with leak checks.")
    ap.add_argument("--suite", action="append", help="only test files whose path contains/matches this (repeatable)")
    ap.add_argument("--list", action="store_true", help="list test files and exit")
    ap.add_argument("--changed-only", action="store_true", help="only files plausibly affected by uncommitted changes")
    ap.add_argument("--jobs", type=int, default=max(1, os.cpu_count() or 1), help="parallel Godot processes")
    ap.add_argument("--timeout", type=int, default=600, help="per-file wall clock seconds")
    ap.add_argument("--fail-fast", action="store_true", help="stop scheduling after the first red file")
    ap.add_argument("--no-leaks", action="store_true", help="skip --verbose leak checking")
    ap.add_argument("--verbose", action="store_true", help="print full output of red files")
    ap.add_argument("--self-test", action="store_true", help="verify failure and leak detection, then exit")
    ap.add_argument("--allow-stale", action="store_true", help="run even when src/ is newer than the built extension")
    ap.add_argument("--no-lint", action="store_true", help="skip tools/lint_tests.py (NOT green)")
    ap.add_argument("--realtime", action="store_true",
                    help="real-time frame pacing instead of simulated --fixed-fps time (slow; for timing-sensitive debugging)")
    ap.add_argument("--fixed-fps", type=int, default=60, help="simulated frames per second (default 60 = physics tick rate)")
    ap.add_argument("--report", metavar="PATH", nargs="?", const=os.path.join(PROJECT, "test_results", "summary.json"),
                    help="write per-file/per-test durations + statuses as JSON (default test_project/test_results/summary.json)")
    ap.add_argument("--full", action="store_true", help="print every failure message in full (no clipping)")
    args = ap.parse_args()
    if args.full:
        global DETAIL_LIMIT
        DETAIL_LIMIT = None
    fixed_fps = 0 if args.realtime else max(0, args.fixed_fps)

    godot = godot_binary()
    if not refresh_class_cache(godot):
        print("class cache refresh (godot --import) failed")
        return 2
    if args.self_test:
        return self_test(godot, args.timeout)

    tests = discover()
    if args.suite:
        tests = [t for t in tests if any(p in t or fnmatch.fnmatch(t, p) for p in args.suite)]
    if args.changed_only:
        tests = filter_changed(tests)
    if args.list:
        print("\n".join(tests))
        return 0
    if not tests:
        print("no test files matched")
        return 1
    if not args.no_lint and not args.list:
        lint = subprocess.run([sys.executable, os.path.join(REPO_ROOT, "tools", "lint_tests.py")],
                              cwd=REPO_ROOT, capture_output=True, text=True)
        if lint.returncode != 0:
            print(lint.stdout + lint.stderr)
            print("refusing to run: test lint failed (fix the violations above; --no-lint skips, but the run is not green)")
            return 3
    stale = stale_binary_reason()
    if stale and not args.allow_stale:
        print(f"refusing to run: {stale}\n(pass --allow-stale to run anyway)")
        return 2

    print(f"godot   : {godot}")
    print(f"files   : {len(tests)}   jobs: {args.jobs}   leak check: {'off' if args.no_leaks else 'on (--verbose)'}"
          f"   time: {'real-time' if fixed_fps == 0 else f'simulated --fixed-fps {fixed_fps}'}")
    print("-" * 72)
    results = run_many(godot, tests, args.jobs, args.timeout, not args.no_leaks, args.fail_fast, args.verbose, fixed_fps)
    if args.report:
        write_report(args.report, results, fixed_fps)
    return report(results)


if __name__ == "__main__":
    sys.exit(main())
