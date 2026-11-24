#!/usr/bin/env python3
"""Compile RemotePluginAudioPortsTest.cpp the way a host without <sys/ipc.h> does.

Cluster ci-539b88713935565b-port: the hosted mingw64 job selects
SYNC_WITH_SHM_FIFO in RemotePluginBase.h, which compiles the socket-only tests
out, so an unguarded helper (countCriticals) is -Werror=unused-function and the
build stops.  The RemotePluginAudioPortsShmCompileCheck object library compiles
the file with SYNC_WITH_SHM_FIFO but without that error and is not a ctest, so
removing the guard stayed green on Linux.  This check reuses that target's
compile command from compile_commands.json, adds -Werror=unused-function, and
fails on any non-zero compiler exit or any warning/error text.
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
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

TARGET = "RemotePluginAudioPortsShmCompileCheck"
RELATIVE_SOURCE = "tests/src/core/RemotePluginAudioPortsTest.cpp"
MACRO = "SYNC_WITH_SHM_FIFO"


def fail(message):
    raise RuntimeError(message)


def entry_words(entry):
    if "arguments" in entry:
        return list(entry["arguments"])
    return shlex.split(entry["command"])


def is_target_entry(entry, source):
    try:
        same = Path(entry["directory"], entry["file"]).resolve() == source
    except KeyError:
        return False
    return same and TARGET in " ".join(entry_words(entry))


def find_entry(build_dir, source):
    database = build_dir / "compile_commands.json"
    if not database.is_file():
        fail(f"{database} is missing; configure the build with "
             "CMAKE_EXPORT_COMPILE_COMMANDS=ON")
    entries = json.loads(database.read_text(encoding="utf-8"))
    matches = [entry for entry in entries if is_target_entry(entry, source)]
    if not matches:
        fail(f"{database} has no {TARGET} entry for {RELATIVE_SOURCE}")
    preferred = [entry for entry in matches if MACRO in entry_words(entry)
                 or f"-D{MACRO}" in entry_words(entry)]
    return (preferred or matches)[0]


def strip_outputs(words, source):
    """Drop the compiler, -c, -o <file> and the input file; keep every other flag."""
    kept = []
    skip = False
    for word in words[1:]:
        if skip:
            skip = False
        elif word == "-o":
            skip = True
        elif word == "-c" or word.startswith("-o"):
            continue
        elif Path(word).name != source.name:
            kept.append(word)
    return kept


def build_command(compiler, entry, canonical, source, stub_dir):
    """Flags of the real target plus the checks.

    The test ends with #include "<name>.moc".  The object is discarded and never
    linked, so the check needs no moc code: an empty stand-in goes first on the
    quote include path.  It wins over a generated file, which matches only the
    tree's own source and not a --source copy, so the result is the same
    whether or not AUTOMOC has run.
    The object is discarded (-o /dev/null) rather than -fsyntax-only, because
    GCC reports unused static functions only once it generates code.
    """
    flags = strip_outputs(entry_words(entry), canonical)
    return [compiler, "-iquote", str(stub_dir), *flags, f"-D{MACRO}",
            "-Werror=unused-function", "-O0", "-g0", "-iquote", str(canonical.parent),
            "-c", str(source), "-o", os.devnull]


def run_compile(command, directory):
    return subprocess.run(command, cwd=directory, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, check=False)


def diagnostic_in(output):
    return "warning:" in output or "error:" in output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--cxx", required=True)
    parser.add_argument("--source", type=Path)
    args = parser.parse_args()
    canonical = (args.root / RELATIVE_SOURCE).resolve()
    source = (args.source or canonical).resolve()
    if not source.is_file():
        fail(f"source file is missing: {source}")
    entry = find_entry(args.build_dir.resolve(), canonical)
    with tempfile.TemporaryDirectory(prefix="shm-unused-helper-") as stub_dir:
        (Path(stub_dir) / f"{canonical.stem}.moc").write_text("", encoding="utf-8")
        command = build_command(args.cxx, entry, canonical, source, stub_dir)
        result = run_compile(command, entry["directory"])
    if result.returncode != 0 or diagnostic_in(result.stdout):
        print(result.stdout, end="")
        fail(f"{source.name} does not compile cleanly with {MACRO} and "
             f"-Werror=unused-function (compiler exit {result.returncode})")
    print(f"PASS {source.name}: compiles with {MACRO} and -Werror=unused-function")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"remote plugin shm unused-helper check: {error}", file=sys.stderr)
        sys.exit(1)
