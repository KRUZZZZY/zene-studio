#!/usr/bin/env python3
"""Static guard that the windows-arm64 / Qt 5.15 build fixes stay in place.

Cluster ci-8fceff87a3151aad-port. Four source-level checks, no compiler:
  qt5-overload-followaction  QSpinBox::valueChanged is wrapped in QOverload<int>::of
  qt5-qstringlist-ctor       no QStringList(count, value) constructor (absent in Qt 5.15)
  windows-aligned-alloc      posix_memalign only on the non-_WIN32 side of a conditional
  sfzplayer-abseil-include   SfzPlayerTest gets the bundled Abseil include directory

Usage: windows-arm64-port-check.py --source-root <dir>
"""

# Copyright (c) 2026 Zene Studio contributors
#
# This file is part of LMMS - https://lmms.io
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.

import argparse
from pathlib import Path
import re
import sys

FOLLOW = "src/gui/FollowActionEditor.cpp"
FADERS = "tests/src/gui/MixerSendsOnFadersTest.cpp"
DISTO = "tests/src/plugins/SfzDistoBuffersTest.cpp"
CMAKE = "tests/CMakeLists.txt"
REQUIRED = (FOLLOW, FADERS, DISTO, CMAKE)

OVERLOAD = "QOverload<int>::of("
SIGNAL = "&QSpinBox::valueChanged"
ABSEIL = "plugins/SfzPlayer/sfizz/external/abseil-cpp"
LIST_CTOR = re.compile(r"\bQStringList\b\s*(?:[A-Za-z_]\w*\s*)?\(")
TYPED_PARAM = re.compile(r"\w\s*[&*]?\s+[&*]?\w")
COMMENT = re.compile(r'("(?:\\.|[^"\\\n])*")|//[^\n]*|/\*.*?\*/', re.S)
COND_OPEN = re.compile(r"^\s*#\s*(if|ifdef|ifndef)\b(.*)")
COND_OTHER = re.compile(r"^\s*#\s*(elif|else|endif)\b(.*)")
SFZ_BLOCK = re.compile(r'^\s*if\s*\(\s*LMMS_TEST_NAME\s+STREQUAL\s+"SfzPlayerTest"\s*\)')


def read_lines(root, rel):
    return (root / rel).read_text(encoding="utf-8", errors="replace").splitlines()


def check_overload(root):
    lines = read_lines(root, FOLLOW)
    uses = [n for n, line in enumerate(lines, 1) if SIGNAL in line]
    if not any(OVERLOAD + SIGNAL + ")" in line for line in lines):
        return f"{FOLLOW}: no {OVERLOAD}{SIGNAL}) found"
    for n in uses:
        if OVERLOAD + SIGNAL + ")" not in lines[n - 1]:
            return f"{FOLLOW}:{n}: bare {SIGNAL} (overloaded in Qt 5.15) is not wrapped in {OVERLOAD}"
    return None


def top_level_commas(text, start):
    """Count commas at depth 1 after the '(' just before text[start]; None if unbalanced."""
    depth, commas, quote = 1, 0, None
    for pos in range(start, len(text)):
        ch = text[pos]
        if quote:
            quote = None if ch == quote and text[pos - 1] != "\\" else quote
        elif ch in "\"'":
            quote = ch
        elif ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
            if depth == 0:
                return commas, text[start:pos]
        elif ch == "," and depth == 1:
            commas += 1
    return None


def count_value_ctor(match, text):
    parsed = top_level_commas(text, match.end())
    if parsed is None or parsed[0] != 1:
        return False
    first = parsed[1].split(",", 1)[0].strip()
    return bool(first) and not TYPED_PARAM.search(first)


def strip_comments(text):
    """Blank out // and /* */ comments, keeping line numbers intact."""
    blank = lambda m: re.sub(r"[^\n]", " ", m.group(0))
    return COMMENT.sub(lambda m: m.group(0) if m.group(1) else blank(m), text)


def check_qstringlist(root):
    text = strip_comments("\n".join(read_lines(root, FADERS)))
    for match in LIST_CTOR.finditer(text):
        if count_value_ctor(match, text):
            n = text.count("\n", 0, match.start()) + 1
            return f"{FADERS}:{n}: QStringList(count, value) constructor does not exist in Qt 5.15"
    return None


def update_stack(stack, line):
    opened = COND_OPEN.match(line)
    if opened:
        stack.append([opened.group(2), False])
        return
    other = COND_OTHER.match(line)
    if other and stack:
        if other.group(1) == "endif":
            stack.pop()
        else:
            stack[-1][1] = True


def in_win32_else(stack):
    return any("_WIN32" in cond and negated for cond, negated in stack)


def check_aligned_alloc(root):
    lines = read_lines(root, DISTO)
    if not any("_aligned_malloc" in line for line in lines):
        return f"{DISTO}: no _aligned_malloc use (windows has no posix_memalign)"
    stack = []
    for n, line in enumerate(lines, 1):
        update_stack(stack, line)
        if "posix_memalign" in line and not in_win32_else(stack):
            return f"{DISTO}:{n}: posix_memalign is not inside the #else of an _WIN32 conditional"
    return None


def block_end(lines, start):
    depth = 0
    for n in range(start, len(lines)):
        stripped = lines[n].strip()
        depth += bool(re.match(r"if\s*\(", stripped))
        depth -= bool(re.match(r"endif\s*\(", stripped))
        if depth == 0:
            return n
    return len(lines) - 1


def check_abseil(root):
    lines = read_lines(root, CMAKE)
    starts = [n for n, line in enumerate(lines) if SFZ_BLOCK.match(line)]
    if not starts:
        return f'{CMAKE}: no if(LMMS_TEST_NAME STREQUAL "SfzPlayerTest") block'
    for start in starts:
        body = "\n".join(lines[start:block_end(lines, start) + 1])
        if "target_include_directories" in body and ABSEIL in body:
            return None
    return f"{CMAKE}:{starts[0] + 1}: SfzPlayerTest block lacks a target_include_directories for {ABSEIL}"


CHECKS = (
    ("qt5-overload-followaction", check_overload),
    ("qt5-qstringlist-ctor", check_qstringlist),
    ("windows-aligned-alloc", check_aligned_alloc),
    ("sfzplayer-abseil-include", check_abseil),
)


def missing_files(root):
    return [rel for rel in REQUIRED if not (root / rel).is_file()]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source-root", required=True, type=Path)
    root = parser.parse_args(argv).source_root
    missing = missing_files(root)
    if missing:
        for rel in missing:
            print(f"MISSING required file: {root / rel}")
        return 2
    failed = 0
    for name, check in CHECKS:
        reason = check(root)
        print(f"PASS {name}" if reason is None else f"FAIL {name}: {reason}")
        failed += reason is not None
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
