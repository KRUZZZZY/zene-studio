#!/usr/bin/env python3
"""Fail when first-party code calls QJsonValue::toInteger, which Qt 6 added.

Cluster ci-e58078851126eac9 (2026-11-15), hosted `build / linux-x86_64` at
dec3f1d45c62b26bce955875521780db173a0228: that job builds Qt 5.15 (build.yml
sources `/opt/qt5*/bin/qt5*-env.sh` and passes no -DWANT_QT6=ON), where
QJsonValue declares no toInteger(), so

    tests/src/core/ScriptClockTest.cpp:408:92:
        error: 'class QJsonValue' has no member named 'toInteger'

stopped the whole build. Qt 6 added the member, and every Qt 6 host here
compiles the same line, so only the hosted Qt 5 job sees the defect.

The portable read is jsonInteger(value, defaultValue) from
include/DeprecationHelper.h: on Qt 6 it is value.toInteger(default) and on Qt 5
it is a checked whole-number conversion of the value's double. That adapter is
already used by the control-suite tests (ControlSuccessPathsTest.cpp,
ArrangingCatchUpTest.cpp, AafImportTest.cpp) and by product code.

This source scan is red on the Qt 6 hosts too, so a reintroduction is caught
before the Qt 5 job. The one exempt site is the adapter itself, whose Qt 6
branch is the only legitimate toInteger call and is guarded by QT_VERSION.

Usage: Qt5QJsonToIntegerCheck.py [--root DIR]
       Qt5QJsonToIntegerCheck.py --self-test
Exit codes: 0 = clean, 1 = finding (or a failed fixture), 2 = usage error.
"""

import argparse
import os
import re
import sys
import tempfile

SOURCE_DIRS = ("src", "include", "plugins", "tests/src")
SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hpp")
# Vendored trees are not this repo's code (tests/fork-sources.txt excludes them).
EXCLUDED_PREFIXES = (
    "src/3rdparty/",
    "plugins/CarlaBase/carla/",
    "plugins/NeuralAmp/nam/",
    "plugins/NeuralAmp/rtneural/",
    "plugins/NeuralAmp/tests/",
    "plugins/RnnoiseDenoiser/rnnoise/",
    "plugins/SfzPlayer/sfizz/",
)
ADAPTER = "include/DeprecationHelper.h"
CALL = re.compile(r"\.\s*toInteger\s*\(")
ADVICE = ("QJsonValue::toInteger is Qt 6 only; read it with "
          "jsonInteger(value, defaultValue) from DeprecationHelper.h")

BAD_FIXTURE = 'int n = value(QStringLiteral("source_bytes")).toInteger(-1);\n'
GOOD_FIXTURE = ('int n = jsonInteger(value(QStringLiteral("source_bytes")), -1);\n')


def scan_text(relative, text):
    """[(line number, line)] for every toInteger call in one source file."""
    if relative == ADAPTER:
        return []
    return [(number, line.strip())
            for number, line in enumerate(text.splitlines(), 1)
            if CALL.search(line)]


def source_files(root):
    """Every first-party C/C++ source under the scanned directories."""
    for directory in SOURCE_DIRS:
        base = os.path.join(root, directory)
        for current, _dirs, names in os.walk(base):
            for name in sorted(names):
                if not name.endswith(SOURCE_SUFFIXES):
                    continue
                path = os.path.join(current, name)
                relative = os.path.relpath(path, root).replace(os.sep, "/")
                if relative.startswith(EXCLUDED_PREFIXES):
                    continue
                yield path, relative


def self_test():
    """The scanner must flag the failing shape and accept the portable one."""
    with tempfile.TemporaryDirectory(prefix="qt5-qjson-tointeger-") as workdir:
        findings = {}
        for name, text in (("bad.cpp", BAD_FIXTURE), ("good.cpp", GOOD_FIXTURE)):
            path = os.path.join(workdir, name)
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(text)
            findings[name] = scan_text(name, text)
        if not findings["bad.cpp"]:
            print("SELF-TEST FAIL: the scanner accepted a QJsonValue::toInteger call")
            return 1
        if findings["good.cpp"]:
            print("SELF-TEST FAIL: the scanner rejected the jsonInteger form")
            return 1
    print("self-test: the scanner flags toInteger and accepts jsonInteger")
    return 0


def check(root):
    findings = []
    scanned = 0
    for path, relative in source_files(root):
        scanned += 1
        try:
            with open(path, encoding="utf-8", errors="replace") as handle:
                text = handle.read()
        except OSError as error:
            print(f"{relative}: cannot read: {error}", file=sys.stderr)
            return 2
        for number, line in scan_text(relative, text):
            findings.append(f"{relative}:{number}: {line}")
    print(f"scanned {scanned} first-party C/C++ source file(s)")
    if not findings:
        print("PASS: no first-party QJsonValue::toInteger call")
        return 0
    for finding in findings:
        print(f"{finding}\n  {ADVICE}")
    print(f"FAIL: {len(findings)} QJsonValue::toInteger call(s) will not compile on Qt 5.15")
    return 1


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root",
                        default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv[1:])
    if args.self_test:
        return self_test()
    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print(f"error: not a directory: {root}", file=sys.stderr)
        return 2
    return check(root)


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except (OSError, ValueError) as error:
        print(f"Qt 5 QJsonValue::toInteger check: {error}", file=sys.stderr)
        sys.exit(1)
