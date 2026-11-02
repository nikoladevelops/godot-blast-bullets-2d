#!/usr/bin/env python3
"""Runs the test suites against the RELEASE (template_release, LTO) library:
the binary an exported game ships, not the debug one the suites normally load.

Builds the release library (unless --no-build), swaps it in for the installed
template_debug library, runs tools/run_tests.py with every other argument
passed through, and ALWAYS puts the original library back.

Why: optimizer and LTO differences (undefined behavior the debug build hides,
inlining across translation units, a symbol only one TU defines) show up only
in the release binary. The plugin binds the same API in both builds (no
DEBUG_ENABLED gates), so every suite runs unchanged.

The release build shares SCons object files with the debug build, so the next
debug build recompiles everything. compile_commands.json is restored to the
debug flags afterwards (tools/run_sanitizer_tests.py reads it).

Usage:
    python3 tools/run_release_tests.py                    # build + every suite
    python3 tools/run_release_tests.py --no-build         # reuse the last release build (refused when stale)
    python3 tools/run_release_tests.py --suite volley     # any run_tests.py filter
    python3 tools/run_release_tests.py --no-build --benchmarks --scenario volley_10k   # release numbers (run_benchmarks.py args)
"""

import argparse
import filecmp
import glob
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_tests import ADDON_BIN_DIR, PROJECT, REPO_ROOT  # noqa: E402

WORK_DIR = os.path.join(PROJECT, "test_results", "release")
SRC_DIR = os.path.join(REPO_ROOT, "src")


def newest_source_mtime():
    newest = 0.0
    for root, _dirs, files in os.walk(SRC_DIR):
        for name in files:
            if name.endswith((".cpp", ".hpp", ".h", ".inc")):
                newest = max(newest, os.path.getmtime(os.path.join(root, name)))
    return newest


def build_release():
    commands = os.path.join(REPO_ROOT, "compile_commands.json")
    saved = os.path.join(WORK_DIR, "compile_commands.debug.json")
    if os.path.exists(commands):
        shutil.copy2(commands, saved)
    env = dict(os.environ, GODOTPP_NONINTERACTIVE="1")
    try:
        code = subprocess.run([sys.executable, os.path.join(REPO_ROOT, "tools", "compile_release_build.py")], env=env, cwd=REPO_ROOT).returncode
    finally:
        if os.path.exists(saved):
            shutil.copy2(saved, commands)
            os.remove(saved)
    if code != 0:
        sys.exit("release build failed (exit %d)" % code)


def one_library(pattern):
    found = sorted(glob.glob(os.path.join(ADDON_BIN_DIR, "**", pattern), recursive=True))
    if len(found) != 1:
        sys.exit("expected one installed %s library, found: %s" % (pattern, found))
    return found[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--no-build", action="store_true", help="reuse the installed release library")
    ap.add_argument("--benchmarks", action="store_true", help="run tools/run_benchmarks.py instead of the suites")
    args, runner_args = ap.parse_known_args()
    if sys.platform != "linux":
        sys.exit("run_release_tests.py supports Linux only")
    if args.benchmarks and ({"--gate", "--update-baseline"} & set(runner_args)):
        # The benchmark log has no build label: the baseline is a debug run.
        sys.exit("--gate / --update-baseline compare against the DEBUG baseline: run them without --benchmarks")
    os.makedirs(WORK_DIR, exist_ok=True)
    if not args.no_build:
        build_release()
    release = one_library("*template_release*.so")
    if os.path.getmtime(release) < newest_source_mtime():
        sys.exit("refusing to run: %s is older than src/ (build it, or drop --no-build)" % release)
    installed = one_library("*template_debug*.so")

    backup = os.path.join(WORK_DIR, "original.so")
    shutil.copy2(installed, backup)
    try:
        shutil.copyfile(release, installed)
        tool = "run_benchmarks.py" if args.benchmarks else "run_tests.py"
        runner = [sys.executable, os.path.join(REPO_ROOT, "tools", tool)] + runner_args
        code = subprocess.run(runner, cwd=REPO_ROOT).returncode
    finally:
        shutil.copy2(backup, installed)
    if not filecmp.cmp(backup, installed, shallow=False):
        sys.exit("FAILED TO RESTORE %s: copy %s back by hand" % (installed, backup))
    os.remove(backup)
    print("release library: %s" % ("ALL GREEN" if code == 0 else "FAILED (exit %d)" % code))
    return code


if __name__ == "__main__":
    sys.exit(main())
