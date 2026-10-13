#!/usr/bin/env python3
"""Undefined-behavior run for BlastBullets2D: rebuilds the debug extension
with GCC's UndefinedBehaviorSanitizer (signed overflow, shifts, bounds,
invalid enum/bool loads, float -> int casts that overflow, null and
misaligned accesses), swaps it in for the installed library, runs the test
suites against it and ALWAYS puts the original library back.

The instrumented objects come from compile_commands.json (written by every
debug build), so build first. Nothing in SConstruct changes: the objects
live under test_project/test_results/ubsan/ (gitignored).

Green when the suites pass and no process wrote a UBSan report. The leak
gate is off on purpose: the sanitizer runtime keeps the library loaded past
Godot's StringName teardown, so every file reports "Orphan StringName"
(the NODELETE effect described in AGENTS.md §14), which says nothing about
the plugin.

Usage:
    python3 tools/run_ubsan_tests.py                 # every suite
    python3 tools/run_ubsan_tests.py --suite volley   # any run_tests.py filter
"""

import concurrent.futures
import filecmp
import glob
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_tests import ADDON_BIN_DIR, PROJECT, REPO_ROOT, stale_binary_reason  # noqa: E402

WORK_DIR = os.path.join(PROJECT, "test_results", "ubsan")
SANITIZE = ["-fsanitize=undefined", "-fsanitize=float-cast-overflow", "-fno-sanitize=vptr"]
EXTRA_CFLAGS = ["-DUBSAN_ENABLED", "-fno-omit-frame-pointer", "-g1"] + SANITIZE
REPORT = re.compile(r"^(?P<where>[^\s:]+):(?P<line>\d+):\d+: runtime error: (?P<what>.*)$")


def plugin_compile_commands():
    path = os.path.join(REPO_ROOT, "compile_commands.json")
    if not os.path.exists(path):
        sys.exit("compile_commands.json is missing: run GODOTPP_NONINTERACTIVE=1 python3 tools/compile_debug_build.py")
    commands = []
    for entry in json.load(open(path)):
        if entry["file"].startswith("src/") or entry["file"] == "gen/doc_data.gen.cpp":
            commands.append(entry)
    if not commands:
        sys.exit("compile_commands.json lists no plugin sources")
    return commands


def compile_one(entry):
    args = shlex.split(entry["command"])
    out = args.index("-o")
    obj = os.path.join(WORK_DIR, "obj", entry["file"].replace("/", "__") + ".o")
    args[out + 1] = obj
    proc = subprocess.run(args + EXTRA_CFLAGS, capture_output=True, text=True, cwd=entry["directory"])
    return entry["file"], obj, proc.returncode, proc.stderr


def debug_libraries():
    return sorted(glob.glob(os.path.join(ADDON_BIN_DIR, "**", "*template_debug*.so"), recursive=True))


def godot_cpp_library():
    found = sorted(glob.glob(os.path.join(REPO_ROOT, "godot-cpp", "bin", "libgodot-cpp.*template_debug*.a")))
    if len(found) != 1:
        sys.exit("expected one godot-cpp template_debug library, found: %s" % found)
    return found[0]


def build_instrumented():
    os.makedirs(os.path.join(WORK_DIR, "obj"), exist_ok=True)
    started = time.time()
    objects, failed = [], False
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        for source, obj, code, err in pool.map(compile_one, plugin_compile_commands()):
            if code != 0:
                failed = True
                print("COMPILE FAILED %s\n%s" % (source, err[-2000:]))
            objects.append(obj)
    if failed:
        sys.exit(1)
    library = os.path.join(WORK_DIR, "libblastbullets2d_ubsan.so")
    link = ["g++", "-o", library, "-shared", "-Wl,-R,$ORIGIN", "-m64", "-march=x86-64",
            "-static-libstdc++", "-static-libgcc"] + SANITIZE + objects + [godot_cpp_library()]
    proc = subprocess.run(link, capture_output=True, text=True, cwd=REPO_ROOT)
    if proc.returncode != 0:
        sys.exit("link failed:\n" + proc.stderr[-3000:])
    print("instrumented build: %d objects in %.0f s" % (len(objects), time.time() - started), flush=True)
    return library


def collect_reports(log_dir):
    reports = {}
    for path in sorted(glob.glob(os.path.join(log_dir, "ub.*"))):
        lines = open(path, errors="replace").read().splitlines()
        for i, line in enumerate(lines):
            m = REPORT.match(line)
            if not m:
                continue
            key = "%s:%s: %s" % (m["where"], m["line"], m["what"])
            if key not in reports:
                frames = [l.strip() for l in lines[i + 1:i + 6] if l.strip().startswith("#")]
                reports[key] = frames
    return reports


def main():
    if sys.platform != "linux":
        sys.exit("run_ubsan_tests.py supports Linux (GCC) only")
    stale = stale_binary_reason()
    if stale:
        sys.exit("refusing to run: %s" % stale)
    installed = debug_libraries()
    if len(installed) != 1:
        sys.exit("expected one installed template_debug library, found: %s" % installed)
    installed = installed[0]
    instrumented = build_instrumented()

    log_dir = os.path.join(WORK_DIR, "logs")
    shutil.rmtree(log_dir, ignore_errors=True)
    os.makedirs(log_dir)
    backup = os.path.join(WORK_DIR, "original.so")
    shutil.copy2(installed, backup)
    env = dict(os.environ)
    env["UBSAN_OPTIONS"] = "log_path=%s:print_stacktrace=1" % os.path.join(log_dir, "ub")
    try:
        shutil.copyfile(instrumented, installed)
        runner = [sys.executable, os.path.join(REPO_ROOT, "tools", "run_tests.py"), "--no-leaks"] + sys.argv[1:]
        suites_code = subprocess.run(runner, env=env, cwd=REPO_ROOT).returncode
    finally:
        shutil.copy2(backup, installed)
    if not filecmp.cmp(backup, installed, shallow=False):
        sys.exit("FAILED TO RESTORE %s: copy %s back by hand" % (installed, backup))
    os.remove(backup)

    reports = collect_reports(log_dir)
    print("=" * 72)
    for key, frames in reports.items():
        print("UB  " + key)
        for frame in frames:
            print("      " + frame[:200])
    if reports:
        print("%d distinct undefined-behavior report(s) (logs: %s)" % (len(reports), log_dir))
    else:
        print("no undefined behavior reported")
    if suites_code != 0:
        print("the suites failed under the instrumented build (exit %d)" % suites_code)
    return 1 if reports or suites_code != 0 else 0


if __name__ == "__main__":
    sys.exit(main())
