#!/usr/bin/env python3
"""Fail when ScriptClockTest reads JSON numbers through QJsonValue::toInteger.

Cluster ci-bbbeb2c8d6e707a8, hosted `build / macos-x86_64` at
dec3f1d45c62b26bce955875521780db173a0228: that job builds against Qt 5.15, where
`QJsonValue` has no `toInteger` member (it is Qt 6 only), so

    tests/src/core/ScriptClockTest.cpp:408 and :415
        clock.scheduleState(id).value(QStringLiteral("source_bytes")).toInteger(-1)

stopped the build:

    error: no member named 'toInteger' in 'QJsonValue'

The portable spelling is the project's `jsonInteger` adapter in
include/DeprecationHelper.h, which keeps the Qt 6 whole-number semantics on Qt 5.

The check has two deterministic halves:

  * fixtures: a Qt 5 tree must reject a raw `QJsonValue::toInteger` call and accept
    `jsonInteger`. A tree that accepts `toInteger`, or rejects the adapter, cannot
    tell the trap from the fix and fails the check instead of passing it.
  * the real translation unit: tests/src/core/ScriptClockTest.cpp is recompiled
    `-fsyntax-only` through this build's compile_commands.json against the Qt 5
    headers (and the Qt 5 moc), retargeting the recorded Qt include directories.
    That is the hosted job's compiler input, so this check is the hosted build.

The probe returns a dedicated skip code (77, mapped to CTest's Skipped) only when
a Qt 5 prerequisite is absent: pkg-config finds no Qt 5 development tree or no Qt 5
moc. The hosted macos-x86_64 job is Qt 5, so it is red there before the fix.

Usage:
  Qt5QJsonValueIntegerCheck.py [--root DIR] [--build-dir DIR] [--cxx CXX]
  Qt5QJsonValueIntegerCheck.py --self-test

Exit codes: 0 = clean, 1 = finding or unusable compatibility fixture, 2 = usage
error, 77 = skipped (Qt 5 development tree or moc unavailable).
"""

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

TEST_SOURCE = "tests/src/core/ScriptClockTest.cpp"
SKIP_RETURN_CODE = 77

# Qt modules whose include directories must give way to the Qt 5 tree.
QT_MODULES = {"qtcore", "qtgui", "qtwidgets", "qtxml", "qtnetwork", "qttest",
              "qtsvg", "qtdbus", "qtconcurrent"}

BAD_FIXTURE = """\
#include <QJsonValue>

int main()
{
    const QJsonValue value(14);
    return static_cast<int>(value.toInteger(-1));
}
"""

GOOD_FIXTURE = """\
#include <QJsonValue>

#include "DeprecationHelper.h"

int main()
{
    const QJsonValue value(14);
    return lmms::jsonInteger(value, -1) == 14 ? 0 : 1;
}
"""


def run(command, cwd=None, timeout=600):
    try:
        return subprocess.run(command, cwd=cwd, capture_output=True, text=True,
                              timeout=timeout)
    except (OSError, subprocess.SubprocessError):
        return None


def qt5_flags():
    """Compile flags of a Qt 5 development tree, or None when pkg-config has none."""
    if not shutil.which("pkg-config"):
        return None
    if run(["pkg-config", "--exists", "Qt5Widgets"]).returncode != 0:
        return None
    for packages in (["Qt5Widgets", "Qt5Xml", "Qt5Network", "Qt5Test"],
                     ["Qt5Widgets", "Qt5Xml", "Qt5Network"],
                     ["Qt5Widgets"]):
        result = run(["pkg-config", "--cflags", *packages])
        if result is not None and result.returncode == 0:
            return shlex.split(result.stdout)
    return None


def qt5_moc():
    """The Qt 5 moc, or None. A Qt 6 moc cannot pre-process a Qt 5 translation unit."""
    result = run(["pkg-config", "--variable=host_bins", "Qt5Core"])
    if result is not None and result.returncode == 0 and result.stdout.strip():
        candidate = os.path.join(result.stdout.strip(), "moc")
        if os.path.isfile(candidate):
            version = run([candidate, "-v"])
            if version is not None and version.stdout.strip().startswith("moc 5"):
                return candidate
    return None


def compile_fixture(cxx, flags, workdir, name, text):
    path = os.path.join(workdir, name)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    return run([cxx, "-std=gnu++20", "-fPIC", "-fsyntax-only", *flags, path])


def fixture_verdict(cxx, flags, workdir, include_dir):
    """(usable, verdict) for the tree: usable only when it rejects BAD and accepts GOOD."""
    bad = compile_fixture(cxx, [*flags, "-I" + include_dir], workdir, "bad.cpp", BAD_FIXTURE)
    good = compile_fixture(cxx, [*flags, "-I" + include_dir], workdir, "good.cpp", GOOD_FIXTURE)
    if bad is None or bad.returncode == 0:
        return False, "no fixture"
    if good is None or good.returncode != 0:
        return False, "the Qt 5 tree rejects the jsonInteger adapter"
    return True, "probe: Qt 5 rejects QJsonValue::toInteger and accepts jsonInteger"


def compile_database(build):
    path = os.path.join(build, "compile_commands.json")
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8", errors="replace") as handle:
        entries = json.load(handle)
    return entries if isinstance(entries, list) else None


def entry_for(entries, root, relative):
    wanted = os.path.realpath(os.path.join(root, relative))
    for entry in entries:
        if os.path.realpath(entry.get("file", "")) == wanted:
            return entry
    return None


def is_qt_include(directory):
    """True for any Qt include root, so the recorded Qt (5 or 6) cannot leak into the probe."""
    parts = [part.lower() for part in re.split(r"[\\/]+", directory.rstrip("\\/")) if part]
    for part in parts:
        if part in QT_MODULES or part in {"qt", "qt5", "qt6"}:
            return True
        if part.startswith("qt") and part.endswith(".framework"):
            return True
    return False


def qt5_command(entry, flags, extra):
    """The recorded compile line retargeted at the Qt 5 headers, without -Werror."""
    arguments = entry.get("arguments")
    if arguments is None:
        arguments = shlex.split(entry.get("command", ""))
    out = []
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        if argument in ("-o", "-c", "-MD", "-MMD", "-MP"):
            index += 2 if argument in ("-o", "-c") else 1
            continue
        if argument in ("-isystem", "-I") and index + 1 < len(arguments):
            if not is_qt_include(arguments[index + 1]):
                out += [argument, arguments[index + 1]]
            index += 2
            continue
        if argument == "-Werror":
            index += 1
            continue
        out.append(argument)
        index += 1
    # The Qt 5 moc output must win over the recorded Qt 6 autogen directory, so the extra
    # include directories go first.
    return out[:1] + list(extra) + out[1:] + list(flags) + ["-fsyntax-only"]


def probe_translation_unit(cxx, flags, root, build, moc, workdir):
    entries = compile_database(build)
    if entries is None:
        return f"no compile database at {os.path.join(build, 'compile_commands.json')}"
    entry = entry_for(entries, root, TEST_SOURCE)
    if entry is None:
        return f"{TEST_SOURCE}: no entry in the compile database"
    source = os.path.realpath(os.path.join(root, TEST_SOURCE))
    generated = os.path.join(workdir, "ScriptClockTest.moc")
    made = run([moc, source, "-o", generated])
    if made is None or made.returncode != 0:
        return f"{TEST_SOURCE}: the Qt 5 moc failed"
    command = qt5_command(entry, flags, ["-I" + workdir])
    command.append(source)
    result = run(command, cwd=entry.get("directory") or root)
    if result is None:
        return f"{TEST_SOURCE}: the Qt 5 compile probe could not run"
    if result.returncode != 0:
        diagnostic = "\n".join(line for line in (result.stdout + result.stderr).splitlines()
                               if re.search(r"error:|note:", line))[:3000]
        return f"{TEST_SOURCE}: Qt 5 rejected it\n{diagnostic}"
    return None


def self_test(cxx, flags, root):
    """The fixtures must be distinguishable before the real translation unit is trusted."""
    with tempfile.TemporaryDirectory(prefix="qt5-qjson-fixtures-") as workdir:
        return fixture_verdict(cxx, flags, workdir, os.path.join(root, "include"))


def run_self_test(cxx, root):
    """The fixtures still need a Qt 5 tree; they prove the instrument, never the product."""
    flags = qt5_flags()
    if flags is None:
        print("self-test: skipped (no Qt 5 development tree found)")
        return SKIP_RETURN_CODE
    usable, verdict = self_test(cxx, flags, root)
    if not usable:
        print(f"SELF-TEST FAIL: {verdict}")
        return 1
    print(verdict)
    return 0


def check_product(cxx, root, build):
    """The fixture verdict, then the real translation unit when this host has Qt 5."""
    flags = qt5_flags()
    if flags is None:
        print("probe: skipped (no Qt 5 development tree found)")
        return SKIP_RETURN_CODE
    moc = qt5_moc()
    if moc is None:
        print("probe: skipped (no Qt 5 moc found)")
        return SKIP_RETURN_CODE
    with tempfile.TemporaryDirectory(prefix="qt5-qjson-") as workdir:
        usable, verdict = fixture_verdict(cxx, flags, workdir, os.path.join(root, "include"))
        if not usable:
            print(f"FAIL: compatibility fixture unusable ({verdict})")
            return 1
        print(verdict)
        failure = probe_translation_unit(cxx, flags, root, build, moc, workdir)
    if not failure:
        print(f"PASS: {TEST_SOURCE} compiles against Qt 5 and reads JSON integers through jsonInteger")
        return 0
    print(failure)
    print("FAIL: ScriptClockTest reads JSON numbers through QJsonValue::toInteger, which Qt 5.15 "
          "does not have; use jsonInteger from DeprecationHelper.h")
    return 1


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    parser.add_argument("--build-dir")
    parser.add_argument("--cxx", default=os.environ.get("CXX") or shutil.which("c++") or "c++")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv[1:])

    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print(f"error: not a directory: {root}", file=sys.stderr)
        return 2
    if args.self_test:
        return run_self_test(args.cxx, root)
    return check_product(args.cxx, root, os.path.abspath(args.build_dir or os.path.join(root, "build")))


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"Qt 5 QJsonValue integer check: {error}", file=sys.stderr)
        sys.exit(1)
