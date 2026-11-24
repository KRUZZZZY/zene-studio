#!/usr/bin/env python3
"""Red/green compiler regression for the MixerSendsOnFadersTest expected-fader list.

The hosted macos-x86_64 job builds against Qt 5.15, whose QStringList has no
(size, value) fill constructor. The test built its expected list with that
spelling, so the job stopped with "no matching constructor for initialization
of 'QStringList'". This check derives the construction from the current test
source and compiles it against Qt 5, so the old spelling is red and the
portable loop is green; the old spelling is also compiled as a failing control
so a probe that cannot reproduce the defect cannot pass silently.
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
import shlex
import shutil
import subprocess
import sys
import tempfile


def fail(message):
    raise RuntimeError(message)


def invoke(command, cwd=None):
    return subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=False)


def compile_source(compiler, source, directory, label, *, success, flags=(),
                   expected_diagnostic=()):
    path = directory / f"{label}.cpp"
    path.write_text(source, encoding="utf-8")
    result = invoke([compiler, "-std=c++20", "-Werror=unused-function", *flags,
                     "-c", str(path), "-o", str(directory / f"{label}.o")])
    if (result.returncode == 0) != success:
        fail(f"{label}: expected {'compile success' if success else 'compile failure'}; "
             f"exit {result.returncode}: {result.stderr.strip()}")
    missing = [diagnostic for diagnostic in expected_diagnostic
               if diagnostic not in result.stderr]
    if missing:
        fail(f"{label}: expected diagnostic(s) {', '.join(missing)}; "
             f"stderr: {result.stderr.strip()}")
    print(f"PASS {label}: {'compiled' if success else 'failed as expected'}")


def pkg_flags(pkg, modules):
    tool = shutil.which(pkg)
    if tool is None:
        fail("required tool is missing: pkg-config")
    result = invoke([tool, "--cflags", *modules])
    if result.returncode:
        fail(f"pkg-config {' '.join(modules)} failed: {result.stderr.strip()}")
    return shlex.split(result.stdout)


def pkg_exists(pkg, modules):
    tool = shutil.which(pkg)
    return tool is not None and invoke([tool, "--exists", *modules]).returncode == 0


def expected_list_block(source):
    """The lines that build and compare the expected fader levels, from the test itself."""
    marker = "const QStringList faders = drivesOf("
    try:
        start = source.index(marker)
    except ValueError:
        fail("MixerSendsOnFadersTest no longer reads the channel-fader answer")
    start = source.index("\n", start) + 1
    end = source.index("QVERIFY(faders ==", start)
    end = source.index("\n", end) + 1
    block = source[start:end]
    if "QVERIFY(faders ==" not in block:
        fail("MixerSendsOnFadersTest expected-fader assertions were not extracted")
    return block.replace("QVERIFY(", "(void)(")


def probe(root, compiler, temporary):
    source = (root / "tests/src/gui/MixerSendsOnFadersTest.cpp").read_text(encoding="utf-8")
    block = expected_list_block(source)
    fixture = """#include <QString>
#include <QStringList>
void probe(int channels)
{
    const QStringList faders = QStringList() << QStringLiteral("volume");
%s}
""" % block
    qt5 = pkg_flags("pkg-config", ["Qt5Core"])
    compile_source(compiler, fixture, temporary, "faders-qt5-fixed", success=True,
                   flags=[*qt5, "-fPIC"])
    if pkg_exists("pkg-config", ["Qt6Core"]):
        qt6 = pkg_flags("pkg-config", ["Qt6Core"])
        compile_source(compiler, fixture, temporary, "faders-qt6-fixed", success=True,
                       flags=[*qt6, "-fPIC"])
    else:
        print("OMITTED faders-qt6-fixed: Qt6Core unavailable")
    control = """#include <QString>
#include <QStringList>
void probe(int channels)
{
    QStringList expected(channels, QStringLiteral("volume"));
    Q_UNUSED(expected);
}
"""
    compile_source(compiler, control, temporary, "faders-qt5-original", success=False,
                   flags=[*qt5, "-fPIC"],
                   expected_diagnostic=("QStringList", "no matching"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    args = parser.parse_args()
    root = args.source_root.resolve()
    compiler = shutil.which("c++")
    if compiler is None:
        fail("required tool is missing: c++")
    with tempfile.TemporaryDirectory(prefix="ci-d3e29f18-faders-") as directory:
        probe(root, compiler, Path(directory))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"mixer-faders portability check: {error}", file=sys.stderr)
        sys.exit(1)
