#!/usr/bin/env python3
"""Compiler regressions for Qt5 JSON, Windows timeout, and Qt target usage."""

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
import shutil
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


def compiler():
    tool = shutil.which("c++")
    if tool is None:
        fail("required tool is missing: c++")
    return tool


def compile_probe(command, cwd, expect_success, label):
    result = invoke(command, cwd)
    if (result.returncode == 0) != expect_success:
        outcome = "succeeded" if result.returncode == 0 else result.stderr.strip()
        fail(f"{label}: expected {'success' if expect_success else 'failure'}, got {outcome}")


def qt5_flags():
    tool = shutil.which("pkg-config")
    if tool is None:
        fail("required tool is missing: pkg-config")
    result = invoke([tool, "--cflags", "Qt5Core", "Qt5Gui", "Qt5Xml"])
    if result.returncode:
        fail(f"pkg-config Qt5Core failed: {result.stderr.strip()}")
    return shlex.split(result.stdout)


def source_statements(root):
    path = root / "tests/src/core/ScriptClockTest.cpp"
    require_file(path)
    text = path.read_text(encoding="utf-8")
    first = re.search(r"^\s*const qint64 perId = .*source_bytes.*;$", text, re.M)
    second = re.search(r"^\s*inState = .*source_bytes.*;$", text, re.M)
    if not first or not second:
        fail(f"could not extract both source_bytes reads from {path}")
    return first.group(0).strip(), second.group(0).strip()


def probe_json(root, tmp):
    first, second = source_statements(root)
    flags = qt5_flags()
    compiler_tool = compiler()
    fixture = '''#include "DeprecationHelper.h"
using lmms::jsonInteger;
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
struct ClockProxy {
    QJsonObject scheduleState(const QString&) const { return {}; }
    QJsonObject stateJson() const { return {}; }
};
int main() {
    ClockProxy clock;
    const QString id;
    %s
    qint64 inState = -1;
    const QJsonArray schedules;
    for (const QJsonValue& value : schedules) {
        const QJsonObject one = value.toObject();
        %s
    }
}
'''
    good = tmp / "qt5-json.cpp"
    good.write_text(fixture % (first, second), encoding="utf-8")
    command = [compiler_tool, "-std=c++20", "-fPIC", "-I" + str(root / "include"), *flags,
               "-fsyntax-only", str(good)]
    compile_probe(command, root, True, "Qt5 source_bytes statements")
    bad = tmp / "qt5-json-original.cpp"
    bad.write_text(fixture % ('const qint64 perId = clock.scheduleState(id).value(QStringLiteral("source_bytes")).toInteger(-1);',
                              'inState = one.value(QStringLiteral("source_bytes")).toInteger(-1);'), encoding="utf-8")
    compile_probe([*command[:-1], str(bad)], root, False,
                  "Qt5 toInteger negative control")


def timeout_expressions(root):
    path = root / "tests/src/core/ControlShutdownHookTestParts1.h"
    require_file(path)
    text = path.read_text(encoding="utf-8")
    expressions = re.findall(r"(?:ControlServer::)?ClientIdleTimeoutMs\s*[+-]\s*\d+", text)
    if len(expressions) != 6:
        fail(f"expected six timeout expressions in {path}, found {len(expressions)}")
    return expressions


def probe_timeouts(root, tmp):
    expressions = timeout_expressions(root)
    prelude = "struct ControlServer { static constexpr int ClientIdleTimeoutMs = 30000; };\n"
    prelude += "using DWORD = unsigned long;\n"
    good = tmp / "timeouts.cpp"
    good.write_text(prelude + "\n".join(
        f"constexpr auto timeout_{index} = {expression};"
        for index, expression in enumerate(expressions)) + "\n",
        encoding="utf-8")
    tool = compiler()
    command = [tool, "-std=c++17", "-fsyntax-only", str(good)]
    compile_probe(command, root, True, "qualified Windows timeout expressions")
    bad = tmp / "timeouts-original.cpp"
    original = [expression.replace("ControlServer::", "") for expression in expressions]
    bad.write_text(prelude + "\n".join(
        f"constexpr auto timeout_{index} = {expression};"
        for index, expression in enumerate(original)) + "\n",
        encoding="utf-8")
    compile_probe([*command[:-1], str(bad)], root, False,
                  "unqualified timeout negative control")


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
    source = tmp / ("qt-usage.cpp")
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
    msvc = is_msvc(command)
    if not has_define(command, "QT_CORE_LIB"):
        fail("RemotePluginShmFifoCompileCheck compile command lacks propagated QT_CORE_LIB")
    if msvc and not any(token.lower() == "/zc:__cplusplus" for token in command):
        fail("MSVC RemotePluginShmFifoCompileCheck command lacks /Zc:__cplusplus")
    success = compile_db_probe(entry, command, tmp, True)
    compile_probe(success, entry["directory"], True, "target Qt usage compile control")
    missing = compile_db_probe(entry, command, tmp, False)
    compile_probe(missing, entry["directory"], False, "missing QT_CORE_LIB negative control")


def qt_usage_model(root, tmp, block, label):
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
    result = invoke(["cmake", "-S", str(source), "-B", str(source / "build"), "-G", "Ninja"])
    if result.returncode:
        fail(f"Qt usage model configure failed: {result.stderr}")
    commands = json.loads((source / "build/compile_commands.json").read_text())
    return commands[0]["command"]


def probe_msvc_usage_model(root, tmp):
    text = (root / "tests/CMakeLists.txt").read_text()
    begin = text.index("add_library(RemotePluginShmFifoCompileCheck OBJECT")
    end = text.index("add_dependencies(RemotePluginShmFifoTest", begin)
    block = text[begin:end]
    command = qt_usage_model(root, tmp, block, "qt-linked")
    if "/Zc:__cplusplus" not in command:
        fail("actual compile-check target does not inherit the modeled MSVC Qt option")
    original = re.sub(r"target_link_libraries\(RemotePluginShmFifoCompileCheck[^)]*\)", "", block)
    missing = qt_usage_model(root, tmp, original, "qt-unlinked")
    if "/Zc:__cplusplus" in missing:
        fail("unlinked target negative control unexpectedly inherited the Qt option")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    root, build = args.source_root.resolve(), args.build_dir.resolve()
    with tempfile.TemporaryDirectory(prefix="train47-portability-") as temporary:
        tmp = Path(temporary)
        probe_json(root, tmp)
        probe_timeouts(root, tmp)
        probe_compile_database(build, root, tmp)
        probe_msvc_usage_model(root, tmp)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"train-47 portability check: {error}", file=sys.stderr)
        sys.exit(1)
