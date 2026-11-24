#!/usr/bin/env python3
"""Fail when ScriptClockTest's two source_bytes reads stop using jsonInteger.

Cluster ci-e58078851126eac9-port, hosted `build / linux-x86_64` at
dec3f1d45c62b26bce955875521780db173a0228. That job builds against Qt 5.15
(build.yml sources /opt/qt5*/bin/qt5*-env.sh and passes no -DWANT_QT6=ON), where
QJsonValue declares no toInteger(), so the build stopped at

    tests/src/core/ScriptClockTest.cpp:408:92:
        error: 'class QJsonValue' has no member named 'toInteger'
    tests/src/core/ScriptClockTest.cpp:415:69:
        error: 'class QJsonValue' has no member named 'toInteger'

The portable read is jsonInteger(value, -1) from include/DeprecationHelper.h:
on Qt 6 it is value.toInteger(default) and on Qt 5 it is the equivalent checked
whole-number conversion of the value's double.

This port check is deterministic and has two halves:

  * both sites: the source must still contain exactly the two `source_bytes`
    reads (scheduleState(id) and stateJson()) and each one must go through
    jsonInteger(value, -1). A repair that misses the second site, or that keeps
    the adapter but changes its default, is a finding.
  * the hosted compiler input: the real tests/src/core/ScriptClockTest.cpp is
    recompiled -fsyntax-only through this build's compile_commands.json command
    line, retargeted at the Qt 5 headers with a Qt 5 moc standing in for the Qt 6
    autogen output (tests/Qt5QJsonValueIntegerCheck.py holds that probe). Before
    the fix that is the hosted error, after it the compile is clean.

Both halves prove themselves first: --self-test feeds the site scan a reverted
pair, a half-reverted pair and a wrong default (each must be a finding) and the
portable pair (must be clean); the Qt 5 half compiles a known-bad toInteger
control, which the Qt 5 tree must reject, and a known-good jsonInteger control,
which it must accept. Without a Qt 5 development tree the compile half reports
itself skipped (77 -> CTest Skipped), never passed.

Usage:
  ScriptClockQt5JsonIntegerPortCheck.py [--root DIR] [--build-dir DIR] [--cxx CXX]
  ScriptClockQt5JsonIntegerPortCheck.py --self-test

Exit codes: 0 = clean, 1 = finding (or an unusable fixture), 2 = usage error,
77 = skipped (no Qt 5 development tree or no Qt 5 moc).
"""

import argparse
import os
import re
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
# The compile_commands.json probe this check drives, so the two ports cannot drift.
import Qt5QJsonValueIntegerCheck as qt5probe  # noqa: E402

TEST_SOURCE = "tests/src/core/ScriptClockTest.cpp"
EXPECTED_SITES = 2
SKIP_RETURN_CODE = 77

TO_INTEGER = re.compile(r"(?:\.|->)\s*toInteger\s*\(")
ADAPTER = re.compile(r"\bjsonInteger\s*\(")
ADAPTER_DEFAULT = re.compile(r"\bjsonInteger\s*\([^;]*,\s*-1\s*\)")

BAD_BOTH_FIXTURE = """\
void f()
{
\tconst qint64 perId = clock.scheduleState(id).value(QStringLiteral("source_bytes")).toInteger(-1);
\tinState = one.value(QStringLiteral("source_bytes")).toInteger(-1);
}
"""
BAD_SECOND_FIXTURE = """\
void f()
{
\tconst qint64 perId = jsonInteger(clock.scheduleState(id).value(QStringLiteral("source_bytes")), -1);
\tinState = one.value(QStringLiteral("source_bytes")).toInteger(-1);
}
"""
BAD_DEFAULT_FIXTURE = """\
void f()
{
\tconst qint64 perId = jsonInteger(clock.scheduleState(id).value(QStringLiteral("source_bytes")), 0);
\tinState = jsonInteger(one.value(QStringLiteral("source_bytes")), 0);
}
"""
GOOD_FIXTURE = """\
void f()
{
\tconst qint64 perId = jsonInteger(clock.scheduleState(id).value(QStringLiteral("source_bytes")), -1);
\tinState = jsonInteger(one.value(QStringLiteral("source_bytes")), -1);
}
"""

BAD_FIXTURES = (("both-reverted", BAD_BOTH_FIXTURE),
                ("half-reverted", BAD_SECOND_FIXTURE),
                ("wrong-default", BAD_DEFAULT_FIXTURE))


def read_sites(text):
    """[(line number, stripped line)] for every source_bytes read in the source."""
    return [(number, line.strip())
            for number, line in enumerate(text.splitlines(), 1)
            if "source_bytes" in line and "value(" in line]


def site_findings(sites):
    """What stops the two reads compiling, or reading the same value, on Qt 5."""
    findings = []
    if len(sites) != EXPECTED_SITES:
        findings.append(f"expected {EXPECTED_SITES} source_bytes reads, found {len(sites)}")
    for number, line in sites:
        if TO_INTEGER.search(line):
            findings.append(f"line {number}: reads source_bytes through QJsonValue::toInteger, "
                            f"which Qt 5.15 does not declare: {line}")
        elif not ADAPTER.search(line):
            findings.append(f"line {number}: source_bytes is not read through "
                            f"jsonInteger(value, -1): {line}")
        elif not ADAPTER_DEFAULT.search(line):
            findings.append(f"line {number}: jsonInteger keeps a default other than -1, so a "
                            f"missing value no longer reports itself: {line}")
    return findings


def self_test():
    """The site scan must flag every broken shape before the source is trusted."""
    failed = False
    for name, text in BAD_FIXTURES:
        if not site_findings(read_sites(text)):
            print(f"SELF-TEST FAIL: the scan accepted the {name} fixture")
            failed = True
    if site_findings(read_sites(GOOD_FIXTURE)):
        print("SELF-TEST FAIL: the scan rejected the portable jsonInteger fixture")
        failed = True
    if failed:
        return 1
    print("self-test: the scan flags a reverted, a half-reverted and a wrong-default read "
          "and accepts jsonInteger(value, -1) at both sites")
    return 0


def check_source(root):
    """The two sites, read from the real test source."""
    path = os.path.join(root, TEST_SOURCE)
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
    except OSError as error:
        print(f"FAIL: cannot read {TEST_SOURCE}: {error}")
        return 1
    sites = read_sites(text)
    findings = site_findings(sites)
    if findings:
        for finding in findings:
            print(f"{TEST_SOURCE}: {finding}")
        print("FAIL: the source_bytes reads are not both jsonInteger(value, -1); Qt 5.15 has no "
              "QJsonValue::toInteger, and a changed default changes what a missing value reports")
        return 1
    for number, line in sites:
        print(f"{TEST_SOURCE}:{number}: {line}")
    print(f"PASS: both source_bytes reads go through jsonInteger(value, -1)")
    return 0


def check_compile(root, build, cxx):
    """The hosted Qt 5 compile of the real translation unit."""
    flags = qt5probe.qt5_flags()
    if flags is None:
        print("probe: skipped (no Qt 5 development tree found)")
        return SKIP_RETURN_CODE
    moc = qt5probe.qt5_moc()
    if moc is None:
        print("probe: skipped (no Qt 5 moc found)")
        return SKIP_RETURN_CODE
    scratch = os.environ.get("TMPDIR") or None
    with tempfile.TemporaryDirectory(prefix="port-qt5-qjson-", dir=scratch) as workdir:
        usable, verdict = qt5probe.fixture_verdict(cxx, flags, workdir,
                                                   os.path.join(root, "include"))
        if not usable:
            print(f"FAIL: compatibility fixture unusable ({verdict})")
            return 1
        print(verdict)
        failure = qt5probe.probe_translation_unit(cxx, flags, root, build, moc, workdir)
    if failure:
        print(failure)
        print("FAIL: ScriptClockTest does not compile against Qt 5.15; the hosted "
              "build / linux-x86_64 job stops here")
        return 1
    print(f"PASS: {TEST_SOURCE} compiles against Qt 5 and reads JSON integers through jsonInteger")
    return 0


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=os.path.dirname(HERE))
    parser.add_argument("--build-dir")
    parser.add_argument("--cxx", default=os.environ.get("CXX") or "c++")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv[1:])

    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print(f"error: not a directory: {root}", file=sys.stderr)
        return 2
    if args.self_test:
        return self_test()

    # Both halves always run, so one finding never hides the other: a repair that
    # misses a site still has to show whether the hosted compile passes.
    source = check_source(root)
    build = os.path.abspath(args.build_dir or os.path.join(root, "build"))
    compile_half = check_compile(root, build, args.cxx)
    if source or compile_half == 1:
        return 1
    return compile_half


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except (OSError, RuntimeError, ValueError) as error:
        print(f"ScriptClock Qt 5 jsonInteger port check: {error}", file=sys.stderr)
        sys.exit(1)
