#!/usr/bin/env python3
"""Sanitizer runs for BlastBullets2D: rebuilds the debug extension with a GCC
sanitizer, swaps it in for the installed library, runs the test suites
against it and ALWAYS puts the original library back.

  undefined  UndefinedBehaviorSanitizer: signed overflow, shifts, bounds,
             invalid enum/bool loads, float -> int casts that overflow.
  address    AddressSanitizer: heap-use-after-free, buffer overflows, double
             frees (the runtime is preloaded into Godot; a tiny shim drops
             the RTLD_DEEPBIND flag Godot loads extensions with, which the
             ASan runtime refuses).

The instrumented objects come from compile_commands.json (written by every
debug build), so build first. Nothing in SConstruct changes: everything
lives under test_project/test_results/sanitizers/ (gitignored).

Green when the suites pass and no process wrote a sanitizer report. The
leak gate is off on purpose: a sanitizer runtime keeps the library loaded
past Godot's StringName teardown, so every file reports "Orphan
StringName" (the NODELETE effect described in AGENTS.md §14). Wall-clock
budgets widen by BLAST_TIME_BUDGET_SCALE (BlastTest.time_budget_ms).

Usage:
    python3 tools/run_sanitizer_tests.py                       # both, every suite
    python3 tools/run_sanitizer_tests.py --sanitizer address   # one of them
    python3 tools/run_sanitizer_tests.py --suite volley        # any run_tests.py filter
"""

import argparse
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

WORK_ROOT = os.path.join(PROJECT, "test_results", "sanitizers")
SANITIZERS = {
    "undefined": {
        "cflags": ["-DUBSAN_ENABLED", "-fsanitize=undefined", "-fsanitize=float-cast-overflow", "-fno-sanitize=vptr"],
        "lflags": ["-fsanitize=undefined", "-fsanitize=float-cast-overflow", "-static-libstdc++", "-static-libgcc"],
        "env": "UBSAN_OPTIONS",
        "options": "print_stacktrace=1",
        "report": re.compile(r"^(?P<where>[^\s:]+):(?P<line>\d+):\d+: runtime error: (?P<what>.*)$"),
        "time_scale": "3",
    },
    "address": {
        "cflags": ["-DASAN_ENABLED", "-fsanitize=address"],
        "lflags": ["-fsanitize=address"],
        "env": "ASAN_OPTIONS",
        "options": "detect_leaks=0:verify_asan_link_order=0:halt_on_error=1",
        "report": re.compile(r"^==\d+==ERROR: AddressSanitizer: (?P<what>[\w-]+)"),
        "time_scale": "6",
    },
}
COMMON_CFLAGS = ["-fno-omit-frame-pointer", "-g1"]
NODEEPBIND_C = r"""
#define _GNU_SOURCE
#include <dlfcn.h>
void *dlopen(const char *file, int mode) {
	static void *(*real_dlopen)(const char *, int) = 0;
	if (!real_dlopen) {
		real_dlopen = (void *(*)(const char *, int))dlsym(RTLD_NEXT, "dlopen");
	}
	return real_dlopen(file, mode & ~RTLD_DEEPBIND);
}
"""


def plugin_compile_commands():
    path = os.path.join(REPO_ROOT, "compile_commands.json")
    if not os.path.exists(path):
        sys.exit("compile_commands.json is missing: run GODOTPP_NONINTERACTIVE=1 python3 tools/compile_debug_build.py")
    commands = [e for e in json.load(open(path)) if e["file"].startswith("src/") or e["file"] == "gen/doc_data.gen.cpp"]
    if not commands:
        sys.exit("compile_commands.json lists no plugin sources")
    return commands


def godot_cpp_library():
    found = sorted(glob.glob(os.path.join(REPO_ROOT, "godot-cpp", "bin", "libgodot-cpp.*template_debug*.a")))
    if len(found) != 1:
        sys.exit("expected one godot-cpp template_debug library, found: %s" % found)
    return found[0]


def build_instrumented(name, work_dir):
    cfg = SANITIZERS[name]
    obj_dir = os.path.join(work_dir, "obj")
    os.makedirs(obj_dir, exist_ok=True)

    def compile_one(entry):
        args = shlex.split(entry["command"])
        out = args.index("-o")
        obj = os.path.join(obj_dir, entry["file"].replace("/", "__") + ".o")
        args[out + 1] = obj
        proc = subprocess.run(args + cfg["cflags"] + COMMON_CFLAGS, capture_output=True, text=True, cwd=entry["directory"])
        return entry["file"], obj, proc.returncode, proc.stderr

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
    library = os.path.join(work_dir, "libblastbullets2d_%s.so" % name)
    link = ["g++", "-o", library, "-shared", "-Wl,-R,$ORIGIN", "-m64", "-march=x86-64"] + cfg["lflags"] + objects + [godot_cpp_library()]
    proc = subprocess.run(link, capture_output=True, text=True, cwd=REPO_ROOT)
    if proc.returncode != 0:
        sys.exit("link failed:\n" + proc.stderr[-3000:])
    print("%s build: %d objects in %.0f s" % (name, len(objects), time.time() - started), flush=True)
    return library


def preload_for(name, work_dir):
    if name != "address":
        return None
    shim_c = os.path.join(work_dir, "nodeepbind.c")
    shim = os.path.join(work_dir, "nodeepbind.so")
    with open(shim_c, "w") as f:
        f.write(NODEEPBIND_C)
    subprocess.run(["gcc", "-shared", "-fPIC", "-O2", "-o", shim, shim_c, "-ldl"], check=True)
    runtime = subprocess.run(["gcc", "-print-file-name=libasan.so"], capture_output=True, text=True, check=True).stdout.strip()
    return "%s %s" % (shim, runtime)


def collect_reports(name, log_dir):
    pattern = SANITIZERS[name]["report"]
    reports = {}
    for path in sorted(glob.glob(os.path.join(log_dir, "log.*"))):
        lines = open(path, errors="replace").read().splitlines()
        for i, line in enumerate(lines):
            m = pattern.match(line)
            if not m:
                continue
            frames = [l.strip() for l in lines[i + 1:i + 30] if re.match(r"\s*#\d+ ", l) and " in " in l]
            if name == "undefined":
                key = "%s:%s: %s" % (m["where"], m["line"], m["what"])
            else:
                plugin = [f for f in frames if " src/" in f]
                key = "%s at %s" % (m["what"], plugin[0].split(" in ", 1)[1] if plugin else "(no plugin frame)")
            reports.setdefault(key, frames[:8])
    return reports


def run_one(name, installed, runner_args):
    work_dir = os.path.join(WORK_ROOT, name)
    instrumented = build_instrumented(name, work_dir)
    log_dir = os.path.join(work_dir, "logs")
    shutil.rmtree(log_dir, ignore_errors=True)
    os.makedirs(log_dir)
    cfg = SANITIZERS[name]
    env = dict(os.environ)
    env[cfg["env"]] = "%s:log_path=%s" % (cfg["options"], os.path.join(log_dir, "log"))
    env["BLAST_TIME_BUDGET_SCALE"] = cfg["time_scale"]
    preload = preload_for(name, work_dir)
    if preload:
        env["LD_PRELOAD"] = preload
    backup = os.path.join(work_dir, "original.so")
    shutil.copy2(installed, backup)
    try:
        shutil.copyfile(instrumented, installed)
        runner = [sys.executable, os.path.join(REPO_ROOT, "tools", "run_tests.py"), "--no-leaks", "--timeout", "1800"] + runner_args
        suites_code = subprocess.run(runner, env=env, cwd=REPO_ROOT).returncode
    finally:
        shutil.copy2(backup, installed)
    if not filecmp.cmp(backup, installed, shallow=False):
        sys.exit("FAILED TO RESTORE %s: copy %s back by hand" % (installed, backup))
    os.remove(backup)

    reports = collect_reports(name, log_dir)
    print("=" * 72)
    for key, frames in reports.items():
        print("%s  %s" % (name.upper(), key))
        for frame in frames:
            print("      " + frame[:200])
    print("%s: %d distinct report(s)%s" % (name, len(reports), " (logs: %s)" % log_dir if reports else ""))
    if suites_code != 0:
        print("%s: the suites failed under the instrumented build (exit %d)" % (name, suites_code))
    return not reports and suites_code == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sanitizer", choices=["all"] + sorted(SANITIZERS), default="all")
    args, runner_args = ap.parse_known_args()
    if sys.platform != "linux":
        sys.exit("run_sanitizer_tests.py supports Linux (GCC) only")
    stale = stale_binary_reason()
    if stale:
        sys.exit("refusing to run: %s" % stale)
    installed = sorted(glob.glob(os.path.join(ADDON_BIN_DIR, "**", "*template_debug*.so"), recursive=True))
    if len(installed) != 1:
        sys.exit("expected one installed template_debug library, found: %s" % installed)
    names = sorted(SANITIZERS) if args.sanitizer == "all" else [args.sanitizer]
    green = [run_one(name, installed[0], runner_args) for name in names]
    return 0 if all(green) else 1


if __name__ == "__main__":
    sys.exit(main())
