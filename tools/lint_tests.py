#!/usr/bin/env python3
"""Static lint for the BlastBullets2D GUT suites (run automatically by
tools/run_tests.py; exit code 1 on any violation).

Rules (each one exists because a past change silently weakened the suites):
  R1  no `extends SceneTree` scripts under test_project/ (legacy print-based
      suites bypass GUT, strict errors and the leak gate).
  R2  every tests/**/test_*.gd extends BlastTest (tests_meta/ canaries are
      exempt and extend GutTest on purpose).
  R3  no GUT `wait_physics_frames(` / `wait_process_frames(` /
      `wait_idle_frames(` in suites: they resume after N+1 frames. Use
      BlastTest.physics(n) / idle(n).
  R4  `swallow_errors()` only in the allow-listed fuzz/crash suites below.
      Everywhere else pin the exact text with expect_error_sequence().
  R5  `expect_errors_containing(..., N, ..., true)` (at-least mode) needs a
      `# lint: at-least <reason>` comment on the same line.
  R6  every suite is listed in tests/README.md (by its tests/-relative path).
  R7  no `print(` debugging left in suites, except lines starting with
      print("BENCH (benchmark output parsed by humans/tools).

Usage:
    python3 tools/lint_tests.py            # lint, exit 1 on violations
    python3 tools/lint_tests.py --list-allow
"""

import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(REPO_ROOT, "test_project")
TESTS_DIR = os.path.join(PROJECT, "tests")
README = os.path.join(TESTS_DIR, "README.md")

# R4: suites whose contract is "survive hostile input" (not wording).
SWALLOW_ALLOW = {
    "volley/test_volley_crash_proof.gd",
}

GUT_WAIT = re.compile(r"\bwait_(physics|process|idle)_frames\s*\(")
AT_LEAST = re.compile(r"expect_errors_containing\([^#]*,\s*true\s*\)")


def code_part(line):
    """Line without its trailing comment (good enough: suites never put '#'
    inside strings that matter for these rules)."""
    stripped = line.lstrip()
    if stripped.startswith("#"):
        return ""
    idx = line.find(" #")
    return line if idx < 0 else line[:idx]


def suites():
    for dirpath, _, files in os.walk(TESTS_DIR):
        for name in sorted(files):
            if name.startswith("test_") and name.endswith(".gd"):
                full = os.path.join(dirpath, name)
                yield os.path.relpath(full, TESTS_DIR).replace(os.sep, "/"), full


def lint():
    problems = []
    # R1 over the whole project (addons excluded).
    for dirpath, dirs, files in os.walk(PROJECT):
        dirs[:] = [d for d in dirs if d not in ("addons", ".godot")]
        for name in files:
            if not name.endswith(".gd"):
                continue
            full = os.path.join(dirpath, name)
            with open(full, encoding="utf-8") as handle:
                head = handle.read(4096)
            if re.search(r"^extends\s+SceneTree\b", head, re.M):
                problems.append(f"R1 {os.path.relpath(full, REPO_ROOT)}: legacy `extends SceneTree` suite; port it to BlastTest")

    readme = open(README, encoding="utf-8").read() if os.path.exists(README) else ""
    for rel, full in suites():
        with open(full, encoding="utf-8") as handle:
            lines = handle.read().split("\n")
        first_code = next((ln for ln in lines if ln.strip() and not ln.startswith("#")), "")
        if not re.match(r"extends\s+BlastTest\b", first_code):
            problems.append(f"R2 tests/{rel}: must start with `extends BlastTest` (got `{first_code.strip()}`)")
        for no, line in enumerate(lines, 1):
            code = code_part(line)
            if GUT_WAIT.search(code):
                problems.append(f"R3 tests/{rel}:{no}: GUT wait_*_frames resumes after N+1 frames; use physics(n)/idle(n)")
            if "swallow_errors()" in code and rel not in SWALLOW_ALLOW:
                problems.append(f"R4 tests/{rel}:{no}: swallow_errors() outside the fuzz allowlist; pin errors with expect_error_sequence()")
            if AT_LEAST.search(code) and "# lint: at-least" not in line:
                problems.append(f"R5 tests/{rel}:{no}: at-least expectation needs `# lint: at-least <reason>`")
            if re.search(r"^\s*print\(", code) and not re.search(r'print\("BENCH', code):
                problems.append(f"R7 tests/{rel}:{no}: stray print(); assert instead (or prefix BENCH for benchmark output)")
        if f"`{rel}`" not in readme:
            problems.append(f"R6 tests/{rel}: not listed in tests/README.md (add a `{rel}` entry)")
    return problems


def main():
    if "--list-allow" in sys.argv:
        print("\n".join(sorted(SWALLOW_ALLOW)))
        return 0
    problems = lint()
    if problems:
        print(f"test lint: {len(problems)} violation(s)")
        for p in problems:
            print("  " + p)
        return 1
    print("test lint: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
