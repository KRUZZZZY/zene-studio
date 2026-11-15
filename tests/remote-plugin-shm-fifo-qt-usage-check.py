#!/usr/bin/env python3
"""Regression for cluster ci-a0493fd29d4e9b4b: remote FIFO compile-check Qt usage.

RemotePluginShmFifoCompileCheck compiles src/common/RemotePluginBase.cpp, which
includes <QString>. The target used to copy only lmmsobjs's include directories,
so it never consumed Qt's usage requirements: no QT_CORE_LIB, and on MSVC no
/Zc:__cplusplus from Qt6::Platform. cl.exe then rejected the Qt header with
"Qt requires a C++17 compiler" (msvc-x64, run 43681793608).

This check fails on the pre-fix target two ways: its configured compile command
lacks QT_CORE_LIB, and a synthetic MSVC model built from the target's own CMake
block does not inherit the Qt option. The unlinked block is the negative control
and must not inherit it.
"""

# Copyright (c) 2026 Zene Studio contributors
#
# This file is part of LMMS - https://lmms.io
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
# Street, Fifth Floor, Boston, MA 02110-1301 USA.

import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile


def fail(message):
    raise RuntimeError(message)


def require_file(path):
    if not path.is_file():
        fail(f"required source or build file is missing: {path}")


def invoke(command, cwd=None):
    return subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=False)


def compile_probe(command, cwd, expect_success, label):
    result = invoke(command, cwd)
    if (result.returncode == 0) != expect_success:
        outcome = "succeeded" if result.returncode == 0 else result.stderr.strip()
        fail(f"{label}: expected {'success' if expect_success else 'failure'}, got {outcome}")


def compile_entry(build, root):
    path = build / "compile_commands.json"
    require_file(path)
    try:
        entries = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        fail(f"invalid compilation database {path}: {error}")
    if not isinstance(entries, list):
        fail(f"invalid compilation database: {path}")
    source = (root / "src/common/RemotePluginBase.cpp").resolve()
    for entry in entries:
        if Path(entry.get("file", "")).resolve() != source:
            continue
        command = entry.get("arguments") or shlex.split(entry.get("command", ""))
        rendered = " ".join(command)
        if "RemotePluginShmFifoCompileCheck.dir" in rendered:
            return entry, command
    fail(f"no RemotePluginShmFifoCompileCheck command found for {source}")


def is_msvc(command):
    executable = Path(command[0]).name.lower()
    return executable in {"cl", "cl.exe", "clang-cl", "clang-cl.exe"}


def has_define(command, name):
    return any(token in {f"-D{name}", f"/D{name}"} or
               token.startswith((f"-D{name}=", f"/D{name}=")) for token in command)


def is_qt_core_define(token):
    return token in {"-DQT_CORE_LIB", "/DQT_CORE_LIB"} or token.startswith(
        ("-DQT_CORE_LIB=", "/DQT_CORE_LIB="))


def discard_compile_token(token, with_qt_macro):
    if token == "-c":
        return True
    if token.startswith(("-o", "-MF", "-MT", "-MQ", "/Fo", "/Fd")):
        return True
    return not with_qt_macro and is_qt_core_define(token)


def compile_db_probe(entry, command, tmp, with_qt_macro):
    msvc = is_msvc(command)
    source = tmp / "qt-usage.cpp"
    source.write_text("#ifndef QT_CORE_LIB\n#error QT_CORE_LIB missing\n#endif\n",
                      encoding="utf-8")
    original = Path(entry["file"]).resolve()
    adjusted = []
    skip = False
    for token in command:
        if skip:
            skip = False
            continue
        if token in {"-o", "-MF", "-MT", "-MQ"}:
            skip = True
            continue
        if discard_compile_token(token, with_qt_macro):
            continue
        if Path(token).resolve() == original:
            adjusted.append(str(source))
            continue
        adjusted.append(token)
    adjusted.extend(["/Zs"] if msvc else ["-fsyntax-only"])
    return adjusted


def probe_compile_database(build, root, tmp):
    entry, command = compile_entry(build, root)
    if not has_define(command, "QT_CORE_LIB"):
        fail("RemotePluginShmFifoCompileCheck compile command lacks propagated QT_CORE_LIB")
    if is_msvc(command) and not any(token.lower() == "/zc:__cplusplus" for token in command):
        fail("MSVC RemotePluginShmFifoCompileCheck command lacks /Zc:__cplusplus")
    success = compile_db_probe(entry, command, tmp, True)
    compile_probe(success, entry["directory"], True, "target Qt usage compile control")
    missing = compile_db_probe(entry, command, tmp, False)
    compile_probe(missing, entry["directory"], False, "missing QT_CORE_LIB negative control")


def qt_usage_model(root, tmp, block, label, cmake):
    source = tmp / label
    source.mkdir()
    (source / "src/common").mkdir(parents=True)
    (source / "src/common/RemotePluginBase.cpp").write_text("// compile command probe\n")
    prelude = '''cmake_minimum_required(VERSION 3.22)
project(QtUsageModel LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_library(lmmsobjs INTERFACE)
add_library(QtContract INTERFACE)
target_compile_definitions(QtContract INTERFACE QT_CORE_LIB)
target_compile_options(QtContract INTERFACE /Zc:__cplusplus)
set(QT_LIBRARIES QtContract)
'''
    (source / "CMakeLists.txt").write_text(prelude + block)
    # Let CMake pick the host's default generator. The model only needs to
    # generate compile_commands.json; a valid build/test host is not required to
    # have Ninja (qa review ci-a0493fd29d4e9b4b.r1).
    result = invoke([cmake, "-S", str(source), "-B", str(source / "build")])
    if result.returncode:
        fail(f"Qt usage model configure failed: {result.stderr}")
    commands = json.loads((source / "build/compile_commands.json").read_text())
    return commands[0]["command"]


def target_block(root):
    path = root / "tests/CMakeLists.txt"
    require_file(path)
    text = path.read_text(encoding="utf-8")
    begin = text.index("add_library(RemotePluginShmFifoCompileCheck OBJECT")
    end = text.index("add_dependencies(RemotePluginShmFifoTest", begin)
    return text[begin:end]


def probe_msvc_usage_model(root, tmp, cmake):
    block = target_block(root)
    command = qt_usage_model(root, tmp, block, "qt-linked", cmake)
    if "/Zc:__cplusplus" not in command:
        fail("actual compile-check target does not inherit the modeled MSVC Qt option")
    original = re.sub(r"target_link_libraries\(RemotePluginShmFifoCompileCheck[^)]*\)", "", block)
    missing = qt_usage_model(root, tmp, original, "qt-unlinked", cmake)
    if "/Zc:__cplusplus" in missing:
        fail("unlinked target negative control unexpectedly inherited the Qt option")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--cmake", required=True)
    args = parser.parse_args()
    root, build = args.source_root.resolve(), args.build_dir.resolve()
    with tempfile.TemporaryDirectory(prefix="fifo-qt-usage-") as temporary:
        tmp = Path(temporary)
        probe_compile_database(build, root, tmp)
        probe_msvc_usage_model(root, tmp, args.cmake)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"remote-plugin-shm-fifo qt usage check: {error}", file=sys.stderr)
        sys.exit(1)
