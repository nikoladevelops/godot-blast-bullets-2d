#!/usr/bin/env python3
"""Editor smoke run for BlastBullets2D: the headless test suites run with
Engine.is_editor_hint() == false, so editor-only paths (the spawner's live
previews) are exercised here instead. Each scene under
test_project/tests/editor_smoke/ is opened in a headless EDITOR; a @tool
probe inside it drives the editor paths and prints "SMOKE OK" (or
"SMOKE FAIL: <step>" lines) before quitting.

Green when, for every scene: the editor exits 0, the probe printed
SMOKE OK, and no SCRIPT ERROR / ERROR: / crash line appeared.

Usage:
    python3 tools/run_editor_smoke.py
"""

import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_tests import PROJECT, REPO_ROOT, godot_binary, refresh_class_cache, stale_binary_reason  # noqa: E402

SMOKE_DIR = os.path.join(PROJECT, "tests", "editor_smoke")
BAD_LINE = re.compile(r"SCRIPT ERROR|^\s*ERROR:|Parse Error|Failed to load|Segmentation fault|signal 11|Program crashed")


def scenes():
    for name in sorted(os.listdir(SMOKE_DIR)):
        if name.endswith(".tscn"):
            yield "res://tests/editor_smoke/" + name


def main():
    reason = stale_binary_reason()
    if reason:
        print("refusing to run: " + reason)
        return 1
    godot = godot_binary()
    if not refresh_class_cache(godot):
        return 1
    ok = True
    for scene in scenes():
        proc = subprocess.run([godot, "--headless", "--editor", "--path", PROJECT, scene, "--quit-after", "900"],
                              cwd=REPO_ROOT, capture_output=True, text=True, errors="replace", timeout=300)
        lines = (proc.stdout + proc.stderr).splitlines()
        smoke = [line for line in lines if line.startswith("SMOKE")]
        bad = [line for line in lines if BAD_LINE.search(line)]
        passed = proc.returncode == 0 and "SMOKE OK" in smoke and not bad and not any(s.startswith("SMOKE FAIL") for s in smoke)
        print(("PASS  " if passed else "FAIL  ") + scene)
        for line in smoke + bad:
            print("    " + line)
        ok = ok and passed
    print("EDITOR SMOKE OK" if ok else "EDITOR SMOKE FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
