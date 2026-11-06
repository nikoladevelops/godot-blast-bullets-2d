#!/usr/bin/env python3
"""Catch, on Linux, the C++ that only other compilers reject (MSVC above all).

Usage:
    python3 tools/check_portability.py        # exit 1 and list every finding

Three checks over src/ (needs compile_commands.json: run a debug build first):
1. A syntax-only GCC pass with the project's own flags plus -Wpedantic and
   -Wnarrowing: GNU extensions MSVC refuses (", ##__VA_ARGS__" with no
   arguments, VLAs, designated initializers before C++20, ...) and narrowing
   inside braces (GCC warns, MSVC and Clang error: C2397).
2. Every std:: name a file uses has its standard header included by the file
   or by a project / godot-cpp header it includes (transitively). Standard
   headers' own includes are never followed: libstdc++ pulls in far more than
   MSVC's STL, so a missing <vector> or <algorithm> breaks only on Windows.
3. Non-ASCII characters inside a string literal not built with String::utf8:
   godot-cpp's String(const char *) reads Latin-1, so they show as mojibake.

godot-cpp's own macros (GDVIRTUAL_*) are skipped: godot-cpp supports MSVC.
"""

import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO_ROOT, "src")
SEARCH = [SRC, os.path.join(REPO_ROOT, "godot-cpp/include"), os.path.join(REPO_ROOT, "godot-cpp/gen/include")]

# Standard header(s) that declare each std:: name the code uses.
NEED = {
    "algorithm": "sort stable_sort find find_if remove remove_if unique lower_bound upper_bound reverse fill fill_n copy copy_n count count_if any_of all_of none_of nth_element partial_sort clamp min_element max_element binary_search transform equal rotate shuffle min max minmax search replace",
    "numeric": "iota accumulate partial_sum inner_product gcd lcm",
    "vector": "vector",
    "memory": "shared_ptr unique_ptr make_unique weak_ptr enable_shared_from_this",
    "functional": "function hash reference_wrapper less greater",
    "unordered_map": "unordered_map",
    "unordered_set": "unordered_set",
    "map": "map",
    "set": "set",
    "deque": "deque",
    "queue": "queue priority_queue",
    "array": "array",
    "string": "string to_string",
    "utility": "pair make_pair move swap forward exchange",
    "cmath": "isfinite isnan isinf sqrt floor ceil pow fabs fmod round trunc lround llround exp log sin cos tan atan2 hypot copysign signbit",
    "cstring": "memcpy memset memmove memcmp strlen strcmp strncmp",
    "limits": "numeric_limits",
    "cstdint": "int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t uintptr_t intptr_t",
    "type_traits": "is_same is_base_of enable_if conditional decay remove_reference is_integral is_floating_point underlying_type is_enum",
    "tuple": "tuple tie make_tuple",
    "iterator": "back_inserter next prev advance distance",
}
# Other standard headers that also declare a name.
ALSO = {
    "swap": {"algorithm"}, "move": {"algorithm"}, "min": {"algorithm"}, "max": {"algorithm"},
    "hash": {"unordered_map", "unordered_set", "memory", "string"}, "pair": {"map", "unordered_map"},
    "abs": {"cmath", "cstdlib"}, "get": {"tuple", "utility", "array"},
    "int8_t": {"stdint.h"}, "int16_t": {"stdint.h"}, "int32_t": {"stdint.h"}, "int64_t": {"stdint.h"},
    "uint8_t": {"stdint.h"}, "uint16_t": {"stdint.h"}, "uint32_t": {"stdint.h"}, "uint64_t": {"stdint.h"},
    "uintptr_t": {"stdint.h"}, "intptr_t": {"stdint.h"},
}
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
STD_USE = re.compile(r"\bstd::([a-z_0-9]+)")
STRING_LITERAL = re.compile(r'"(?:[^"\\\n]|\\.)*"')


def where():
    out = {}
    for header, names in NEED.items():
        for n in names.split():
            out.setdefault(n, set()).add(header)
    for n, headers in ALSO.items():
        out.setdefault(n, set()).update(headers)
    return out


def resolve(name, cur_dir):
    for base in [cur_dir] + SEARCH:
        path = os.path.normpath(os.path.join(base, name))
        if os.path.isfile(path):
            return path
    return None


def std_headers(path, seen):
    if path in seen:
        return set()
    seen.add(path)
    out = set()
    with open(path, errors="replace") as f:
        text = f.read()
    for name in INCLUDE.findall(text):
        found = resolve(name, os.path.dirname(path))
        if found is None:
            out.add(name)  # a standard / system header: never followed
        else:
            out |= std_headers(found, seen)
    return out


def source_files():
    for dirpath, _, files in os.walk(SRC):
        for name in sorted(files):
            if name.endswith((".cpp", ".hpp", ".h", ".inc")):
                yield os.path.join(dirpath, name)


def check_includes(findings):
    names = where()
    for path in source_files():
        with open(path, errors="replace") as f:
            code = re.sub(r"//[^\n]*", "", f.read())
        have = std_headers(path, set())
        for n in sorted(set(STD_USE.findall(code))):
            headers = names.get(n)
            if headers is not None and not (headers & have):
                findings.append(f"{os.path.relpath(path, REPO_ROOT)}: std::{n} without #include <{'> or <'.join(sorted(headers))}>")


def check_literals(findings):
    for path in source_files():
        with open(path, errors="replace") as f:
            lines = f.read().splitlines()
        for i, line in enumerate(lines, 1):
            code = line.split("//", 1)[0]
            for m in STRING_LITERAL.finditer(code):
                if any(ord(c) > 127 for c in m.group(0)) and not code[: m.start()].rstrip().endswith("utf8("):
                    findings.append(f"{os.path.relpath(path, REPO_ROOT)}:{i}: non-ASCII literal outside String::utf8 (read as Latin-1)")


def compile_commands():
    db = os.path.join(REPO_ROOT, "compile_commands.json")
    if not os.path.isfile(db):
        sys.exit("compile_commands.json is missing: run tools/compile_debug_build.py first")
    with open(db) as f:
        entries = json.load(f)
    for e in entries:
        if not e["file"].startswith("src/"):
            continue
        args = shlex.split(e["command"]) if "command" in e else list(e["arguments"])
        cmd, skip = [], False
        for a in args:
            if skip:
                skip = False
            elif a == "-o":
                skip = True
            elif a != "-c":
                cmd.append(a)
        yield e["file"], cmd + ["-fsyntax-only", "-Wpedantic", "-Wnarrowing"]


def check_compiler(findings):
    cache = {}

    def run(item):
        _, cmd = item
        return subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True).stderr

    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        for err in pool.map(run, list(compile_commands())):
            for line in err.splitlines():
                m = re.match(r"^(src/[^:]+):(\d+):\d+: (warning|error): (.*)$", line)
                if not m:
                    continue
                path, lineno, _, text = m.groups()
                if path not in cache:
                    with open(os.path.join(REPO_ROOT, path), errors="replace") as f:
                        cache[path] = f.read().splitlines()
                source = cache[path][int(lineno) - 1] if int(lineno) <= len(cache[path]) else ""
                if "GDVIRTUAL" in source:
                    continue  # godot-cpp's own variadic macros (supported on MSVC)
                entry = f"{path}:{lineno}: {text}"
                if entry not in findings:
                    findings.append(entry)


def main():
    findings = []
    check_includes(findings)
    check_literals(findings)
    check_compiler(findings)
    for f in findings:
        print(f)
    print(f"{len(findings)} portability finding(s)" if findings else "portable: no finding")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
