#!/usr/bin/env python3
"""Editor smoke run for BlastBullets2D: the headless test suites run with
Engine.is_editor_hint() == false, so editor-only paths (the spawner's live
previews) are exercised here instead. Each scene under
test_project/tests/editor_smoke/ is opened in a headless EDITOR; a @tool
probe inside it drives the editor paths and prints "SMOKE OK" (or
"SMOKE FAIL: <step>" lines) before quitting.

The editor runs on a THROWAWAY COPY of test_project (no .godot/editor
session state, log file in the copy): an editor run on the real project
saves its open scenes, and the developer's next editor launch would reopen
the smoke scene. The probes only act when started with --blast-editor-smoke,
and each scene is also opened WITHOUT it to prove the probe stays inert.

Green when, for every scene: the flagged run exits 0 with SMOKE OK and no
SCRIPT ERROR / ERROR: / crash line, and the unflagged run prints no SMOKE
line at all.

Usage:
    python3 tools/run_editor_smoke.py
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_tests import PROJECT, REPO_ROOT, godot_binary, refresh_class_cache, stale_binary_reason  # noqa: E402

SMOKE_DIR = os.path.join(PROJECT, "tests", "editor_smoke")
RUNNER_FLAG = "--blast-editor-smoke"
BAD_LINE = re.compile(r"SCRIPT ERROR|^\s*ERROR:|Parse Error|Failed to load|Segmentation fault|signal 11|Program crashed")


def scenes():
    for name in sorted(os.listdir(SMOKE_DIR)):
        if name.endswith(".tscn"):
            yield "res://tests/editor_smoke/" + name


def copy_project(dest):
    """test_project without bulky results and without the editor session."""
    def ignore(directory, names):
        skip = set()
        rel = os.path.relpath(directory, PROJECT)
        if rel == ".":
            skip.add("test_results")
        if rel == ".godot":
            skip.add("editor")
        return skip & set(names)
    shutil.copytree(PROJECT, dest, ignore=ignore, symlinks=True)


def run_editor(godot, project, scene, flagged, frames, log_file):
    cmd = [godot, "--headless", "--editor", "--path", project, "--log-file", log_file, scene, "--quit-after", str(frames)]
    if flagged:
        cmd += ["--", RUNNER_FLAG]
    proc = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True, errors="replace", timeout=300)
    return proc.returncode, (proc.stdout + proc.stderr).splitlines()


def main():
    reason = stale_binary_reason()
    if reason:
        print("refusing to run: " + reason)
        return 1
    godot = godot_binary()
    if not refresh_class_cache(godot):
        return 1
    ok = True
    work = tempfile.mkdtemp(prefix="blast_editor_smoke_")
    try:
        project = os.path.join(work, "test_project")
        copy_project(project)
        log_file = os.path.join(work, "editor.log")
        for scene in scenes():
            code, lines = run_editor(godot, project, scene, True, 900, log_file)
            smoke = [line for line in lines if line.startswith("SMOKE")]
            bad = [line for line in lines if BAD_LINE.search(line)]
            passed = code == 0 and "SMOKE OK" in smoke and not bad and not any(s.startswith("SMOKE FAIL") for s in smoke)
            # Without the runner flag the probe must not act (a developer's
            # editor reopening or previewing the scene must never be touched).
            code_inert, lines_inert = run_editor(godot, project, scene, False, 240, log_file)
            stray = [line for line in lines_inert if line.startswith("SMOKE") or BAD_LINE.search(line)]
            inert = code_inert == 0 and not stray
            print(("PASS  " if passed and inert else "FAIL  ") + scene)
            for line in smoke + bad:
                print("    " + line)
            if not inert:
                print("    probe acted without " + RUNNER_FLAG + ": " + "; ".join(stray or ["exit %d" % code_inert]))
            ok = ok and passed and inert
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("EDITOR SMOKE OK" if ok else "EDITOR SMOKE FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
