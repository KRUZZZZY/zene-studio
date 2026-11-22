#!/usr/bin/env python3
"""Compile MsFriendLookupProbe.cpp with clang in MS compatibility mode.

RED regression probe for the ClipView/AutomationTrackView/PatternClipView
friend-lookup ambiguity. It reuses the TouchLayoutTest flags from
compile_commands.json and exits with clang's status, so the test stays red
until the headers resolve the three names unqualified.
"""
import json
import os
import shlex
import shutil
import subprocess
import sys

TARGET = "tests/src/gui/TouchLayoutTest.cpp"

# GCC or driver flags from the TouchLayoutTest entry that clang in MS mode
# cannot use. -c and -o are dropped because this probe is syntax-only.
DROP_FLAGS = {
    "-c",
    "-mno-direct-extern-access",
    "-Wno-error=sfinae-incomplete",
    "-Wno-stringop-overread",
}

# p1 showed that clang reproduces the CI ambiguity only with these flags:
# MS mode plus the GCC version macros the headers branch on, and the
# __GCC_ATOMIC_* macros -fms-compatibility drops but libstdc++ needs.
MS_FLAGS = [
    "-fsyntax-only",
    "-std=gnu++20",
    "-fms-compatibility",
    "-fms-extensions",
    "-fms-compatibility-version=19.40",
    "-D__GNUC__=4",
    "-D__GNUC_MINOR__=2",
    "-D__GNUC_PATCHLEVEL__=1",
    "-D__GNUC_STDC_INLINE__=1",
]
ATOMIC_NAMES = (
    "BOOL", "CHAR", "CHAR8_T", "CHAR16_T", "CHAR32_T", "INT",
    "LLONG", "LONG", "POINTER", "SHORT", "WCHAR_T",
)


def find_entry(entries):
    for entry in entries:
        if entry["file"].endswith(TARGET):
            return entry
    sys.stderr.write("error: no compile_commands.json entry for %s\n" % TARGET)
    sys.exit(2)


def entry_flags(entry):
    tokens = shlex.split(entry["command"])
    args = []
    i = 1  # drop the compiler
    while i < len(tokens):
        token = tokens[i]
        if token == "-o":
            i += 2
        elif token in DROP_FLAGS:
            i += 1
        else:
            args.append(token)
            i += 1
    return [arg for arg in args if not arg.endswith(TARGET)]


def check_paths(cc_json, probe, clang):
    for path in (cc_json, probe):
        if not os.path.isfile(path):
            sys.stderr.write("error: missing %s\n" % path)
            return False
    if not os.path.exists(clang) and shutil.which(clang) is None:
        sys.stderr.write("error: clang not found: %s\n" % clang)
        return False
    return True


def main(argv):
    if len(argv) != 4:
        sys.stderr.write(
            "usage: %s <compile_commands.json> <probe.cpp> <clang>\n" % argv[0])
        return 2
    cc_json, probe, clang = argv[1:]
    if not check_paths(cc_json, probe, clang):
        return 2
    with open(cc_json) as handle:
        entry = find_entry(json.load(handle))
    flags = list(MS_FLAGS)
    flags += ["-D__GCC_ATOMIC_%s_LOCK_FREE=2" % name for name in ATOMIC_NAMES]
    flags.append("-D__GCC_ATOMIC_TEST_AND_SET_TRUEVAL=1")
    flags.append("-Wno-unknown-warning-option")
    cmd = [clang] + flags + entry_flags(entry) + [probe]
    proc = subprocess.run(
        cmd, cwd=entry["directory"], text=True, capture_output=True)
    sys.stdout.write(proc.stdout)
    sys.stderr.write(proc.stderr)
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv))
