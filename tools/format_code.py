#!/usr/bin/env python3
"""Format the C++ sources with clang-format (repo .clang-format, Godot style).

Usage:
    python3 tools/format_code.py              # format every src/**/*.cpp|hpp|h in place
    python3 tools/format_code.py --check      # exit 1 and list files that would change
    python3 tools/format_code.py src/patterns/pattern_layout2d.cpp ...   # only these files

X-macro tables (*.inc) are never touched: their rows are aligned by hand.
Binary: $CLANG_FORMAT, else `clang-format` on PATH.
"""

import argparse
import os
import shutil
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO_ROOT, "src")
EXTS = (".cpp", ".hpp", ".h")


def sources(paths):
    if paths:
        return [os.path.abspath(p) for p in paths if p.endswith(EXTS)]
    found = []
    for dirpath, _, files in os.walk(SRC):
        for name in files:
            if name.endswith(EXTS):
                found.append(os.path.join(dirpath, name))
    return sorted(found)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="report files that need formatting, change nothing")
    parser.add_argument("paths", nargs="*", help="files to format (default: all of src/)")
    args = parser.parse_args()
    binary = os.environ.get("CLANG_FORMAT") or shutil.which("clang-format")
    if not binary:
        print("clang-format not found (install it or set CLANG_FORMAT)")
        return 2
    files = sources(args.paths)
    dirty = []
    for path in files:
        with open(path, encoding="utf-8") as handle:
            before = handle.read()
        proc = subprocess.run([binary, "--style=file", path], cwd=REPO_ROOT, capture_output=True, text=True)
        if proc.returncode != 0:
            print(f"clang-format failed on {path}:\n{proc.stderr}")
            return 2
        if proc.stdout != before:
            dirty.append(os.path.relpath(path, REPO_ROOT))
            if not args.check:
                with open(path, "w", encoding="utf-8") as handle:
                    handle.write(proc.stdout)
    if args.check:
        for rel in dirty:
            print(f"needs formatting: {rel}")
        print(f"{len(dirty)} of {len(files)} file(s) need formatting" if dirty else f"all {len(files)} file(s) formatted")
        return 1 if dirty else 0
    print(f"formatted {len(dirty)} of {len(files)} file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
