#!/usr/bin/env python3
"""Compile ScriptClockTest's source_bytes reads against the Qt 5 headers.

QJsonValue::toInteger is a Qt 6 API. The project supports Qt 5.15 as its
default configuration (CMakeLists.txt), and the hosted Qt 5 lanes compile
tests/src/core/ScriptClockTest.cpp, so a direct toInteger call there is a build
break, not a runtime failure. This check extracts the two reads from the test
and syntax-checks them against Qt5Core, with a negative control that proves the
probe really is using the Qt 5 header (a direct toInteger call must be rejected).
"""

import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


def fail(message):
    print(f"FAIL {message}")
    return 1


def run(command, cwd=None):
    return subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=False)


def source_reads(root):
    path = root / "tests/src/core/ScriptClockTest.cpp"
    if not path.is_file():
        raise RuntimeError(f"required source is missing: {path}")
    reads = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if "source_bytes" in stripped and "value(" in stripped:
            reads.append(stripped)
    if len(reads) != 2:
        raise RuntimeError(
            f"expected the two source_bytes reads in {path}, found {len(reads)}: {reads}")
    return reads


def qt5_cflags():
    tool = shutil.which("pkg-config")
    if tool is None:
        raise RuntimeError("required tool is missing: pkg-config")
    result = run([tool, "--cflags", "Qt5Core", "Qt5Gui", "Qt5Xml"])
    if result.returncode:
        raise RuntimeError(f"pkg-config Qt5Core failed: {result.stderr.strip()}")
    flags = shlex.split(result.stdout)
    if not any(flag.startswith("-I") for flag in flags):
        raise RuntimeError(f"pkg-config Qt5Core returned no include path: {result.stdout.strip()}")
    return flags


def compiler():
    tool = shutil.which("c++")
    if tool is None:
        raise RuntimeError("required tool is missing: c++")
    return tool


def syntax_check(command, expected_success, label, diagnostic=None):
    result = run(command)
    if (result.returncode == 0) != expected_success:
        outcome = "succeeded" if result.returncode == 0 else result.stderr.strip()
        raise RuntimeError(f"{label}: expected "
                           f"{'success' if expected_success else 'failure'}, got {outcome}")
    if diagnostic is not None and diagnostic not in result.stderr:
        raise RuntimeError(f"{label}: expected the compiler to name {diagnostic!r}, "
                           f"got {result.stderr.strip()}")
    print(f"PASS {label}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path,
                        default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.source_root.resolve()

    try:
        first, second = source_reads(root)
        flags = qt5_cflags()
        cxx = compiler()
    except RuntimeError as error:
        return fail(str(error))

    fixture = f'''#include "DeprecationHelper.h"
using lmms::jsonInteger;
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
struct ClockProxy {{
    QJsonObject scheduleState(const QString&) const {{ return {{}}; }}
    QJsonObject stateJson() const {{ return {{}}; }}
}};
int main() {{
    ClockProxy clock;
    const QString id;
    {first}
    qint64 inState = -1;
    const QJsonArray schedules;
    for (const QJsonValue& value : schedules) {{
        const QJsonObject one = value.toObject();
        {second}
    }}
}}
'''
    negative = """#include <QJsonValue>
int main() {
    const QJsonValue value;
    return static_cast<int>(value.toInteger(-1));
}
"""

    scratch = os.environ.get("TMPDIR") or None
    try:
        with tempfile.TemporaryDirectory(prefix="qt5-json-integer-", dir=scratch) as temp:
            directory = Path(temp)
            good = directory / "source-reads.cpp"
            good.write_text(fixture, encoding="utf-8")
            base = [cxx, "-std=c++20", "-fPIC", "-I" + str(root / "include"), *flags, "-fsyntax-only"]
            syntax_check([*base, str(good)], True, "Qt5 source_bytes reads compile")
            bad = directory / "tointeger-negative-control.cpp"
            bad.write_text(negative, encoding="utf-8")
            syntax_check([*base, str(bad)], False, "Qt5 rejects a direct toInteger call",
                         diagnostic="toInteger")
    except (OSError, RuntimeError) as error:
        return fail(str(error))
    return 0


if __name__ == "__main__":
    sys.exit(main())
