#!/usr/bin/env python3
"""Fail when first-party code constructs a QStringList from a count and a value.

Cluster ci-b71ed146fd535ea5 (2026-11-16), hosted `build / linux-x86_64` job at
fa4a5f2e785c73e32216931e33cdbe140c06647d.  That job builds with Qt 5.15, where
QStringList is its own class: its only two-argument constructor is the inherited
iterator-pair template, so

    tests/src/gui/MixerSendsOnFadersTest.cpp:278
        QStringList(channels, QStringLiteral("volume"))

stopped the whole build:

    error: no matching function for call to 'QStringList::QStringList(const int&, QString)'
    note: candidate template ignored: deduced conflicting types for parameter
          'InputIterator' ('int' and 'QString')

Qt 6 aliases QStringList to QList<QString>, whose size+value constructor exists, so
every Qt 6 host here compiled the same line.  The portable spelling appends to an
existing list - `QStringList names; for (int i = 0; i < channels; ++i) { names.append(
QStringLiteral("volume")); }` - which both versions declare (Qt 5's QList has no
fill() either).

The check has two deterministic halves:

  * a source scan of every first-party C/C++ file for a two-argument QStringList
    construction that is not an iterator pair.  It reads sources only, so it is red
    on the Qt 6 hosts where the defect compiles.
  * a compile probe: where pkg-config finds a Qt 5 tree, tests/src/gui/
    MixerSendsOnFadersTest.cpp is recompiled `-fsyntax-only` through this build's
    compile_commands.json against the Qt 5 headers (and the Qt 5 moc).  That is the
    hosted job's compiler input, so a regression here is the hosted failure.  A scan
    finding whose own translation unit compiles clean is a false positive and is not
    reported.

The scan's own fixtures prove both directions.  The probe reports itself skipped
(never passed) when the host has no Qt 5 development tree.

Usage:
  Qt5QStringListSizeCheck.py [--root DIR] [--build-dir DIR] [--cxx CXX]
  Qt5QStringListSizeCheck.py --self-test

Exit codes: 0 = clean (or skipped on a host without Qt 5), 1 = finding or failed
fixture, 2 = usage error.
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

SOURCE_SUFFIXES = (".h", ".hpp", ".hxx", ".cpp", ".cc", ".cxx")

# Vendored third-party trees are not this repo's code (tests/all-sources.txt's list).
EXCLUDED_PREFIXES = (
    "src/3rdparty/",
    "tests/reference/",
    "plugins/CarlaBase/carla/",
    "plugins/NeuralAmp/rtneural/",
    "plugins/NeuralAmp/nam/",
    "plugins/NeuralAmp/tests/",
    "plugins/RnnoiseDenoiser/rnnoise/",
    "plugins/SfzPlayer/sfizz/",
)

SKIP_DIRS = {".git", "build", "__pycache__"}

# Comments and string/char literals are blanked before the shape scan (line numbers kept).
NON_CODE = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
CONSTRUCTION = re.compile(r"\bQStringList\s*\(")

# These arguments are an iterator range, the one two-argument form Qt 5 declares.
ITERATOR_MARKERS = (
    ".begin()", ".end()", ".cbegin()", ".cend()", ".constBegin()", ".constEnd()",
    "std::begin(", "std::end(", "nullptr", "NULL",
)

BAD_FIXTURE = """\
#include <QStringList>
QStringList expected(int channels)
{
    return QStringList(channels, QStringLiteral("volume"));
}
"""

GOOD_FIXTURE = """\
#include <QStringList>
QStringList expected(int channels)
{
    QStringList names;
    for (int index = 0; index < channels; ++index) { names.append(QStringLiteral("volume")); }
    return names;
}
"""

BAD_PROBE = """\
#include <QStringList>
QStringList expected(int channels)
{
    return QStringList(channels, QStringLiteral("volume"));
}
"""

GOOD_PROBE = """\
#include <QStringList>
QStringList expected(int channels)
{
    QStringList names;
    for (int index = 0; index < channels; ++index) { names.append(QStringLiteral("volume")); }
    return names;
}
"""


def run(command, cwd=None, timeout=600):
    try:
        return subprocess.run(command, cwd=cwd, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.SubprocessError):
        return None


def blank_non_code(text):
    """Comments and literals become spaces; every newline and offset is kept."""
    return NON_CODE.sub(lambda match: re.sub(r"[^\n]", " ", match.group(0)), text)


def matching_paren(text, start):
    """Index of the ')' closing the '(' at @a start, or -1."""
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                return index
    return -1


def split_arguments(text):
    """Top-level comma-separated arguments of an argument list, whitespace-stripped."""
    parts, depth, current = [], 0, ""
    for char in text:
        if char in "([{<":
            depth += 1
        elif char in ")]}>":
            depth -= 1
        if char == "," and depth == 0:
            parts.append(current.strip())
            current = ""
        else:
            current += char
    parts.append(current.strip())
    return parts


def scan_text(path, text):
    """(path, line) for every count+value QStringList construction in @a text."""
    code = blank_non_code(text)
    findings = []
    for match in CONSTRUCTION.finditer(code):
        close = matching_paren(code, match.end() - 1)
        if close < 0:
            continue
        arguments = split_arguments(code[match.end():close])
        if len(arguments) != 2:
            continue
        if any(marker in argument for argument in arguments for marker in ITERATOR_MARKERS):
            continue
        findings.append((path, code[:match.start()].count("\n") + 1))
    return findings


def source_files(root):
    for directory, subdirs, names in os.walk(root):
        subdirs[:] = sorted(d for d in subdirs if d not in SKIP_DIRS and not d.startswith("."))
        for name in sorted(names):
            if not name.endswith(SOURCE_SUFFIXES):
                continue
            path = os.path.relpath(os.path.join(directory, name), root)
            if path.startswith(EXCLUDED_PREFIXES):
                continue
            yield path


def read_text(root, path):
    with open(os.path.join(root, path), "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def scan_tree(root):
    findings = []
    for path in source_files(root):
        findings.extend(scan_text(path, read_text(root, path)))
    return findings


def self_test_fixtures():
    failures = []
    if len(scan_text("bad.cpp", BAD_FIXTURE)) != 1:
        failures.append("the scan did not report the count+value fixture")
    if scan_text("good.cpp", GOOD_FIXTURE):
        failures.append("the scan reported the append-loop fixture")
    return failures


def qt5_flags():
    """Compile flags of a Qt 5 development tree, or None when pkg-config has none."""
    if not shutil.which("pkg-config"):
        return None
    if run(["pkg-config", "--exists", "Qt5Widgets"]).returncode != 0:
        return None
    result = run(["pkg-config", "--cflags", "Qt5Widgets", "Qt5Xml", "Qt5Network", "Qt5Test"])
    if result is None or result.returncode != 0:
        return None
    return shlex.split(result.stdout)


def is_qt5_moc(candidate):
    version = run([candidate, "-v"])
    return version is not None and version.stdout.strip().startswith("moc 5")


def qt5_moc():
    """The Qt 5 moc, or None. A Qt 6 moc cannot pre-process a Qt 5 translation unit."""
    result = run(["pkg-config", "--variable=host_bins", "Qt5Core"])
    if result is not None and result.returncode == 0 and result.stdout.strip():
        candidate = os.path.join(result.stdout.strip(), "moc")
        if os.path.isfile(candidate) and is_qt5_moc(candidate):
            return candidate
    for candidate in ("moc-qt5", "moc"):
        found = shutil.which(candidate)
        if found is not None and is_qt5_moc(found):
            return found
    return None


def compile_fixture(cxx, flags, workdir, name, text):
    path = os.path.join(workdir, name)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    return run([cxx, "-std=gnu++20", "-fPIC", "-fsyntax-only", *flags, path])


def fixture_verdict(cxx, flags, workdir):
    """(usable, verdict): usable only when Qt 5 rejects BAD and accepts GOOD."""
    bad = compile_fixture(cxx, flags, workdir, "bad.cpp", BAD_PROBE)
    good = compile_fixture(cxx, flags, workdir, "good.cpp", GOOD_PROBE)
    if bad is None or bad.returncode == 0:
        return False, "no fixture"
    if good is None or good.returncode != 0:
        return False, "the Qt 5 tree rejected the append spelling"
    combined = bad.stdout + bad.stderr
    if not any(marker in combined for marker in ("InputIterator", "no matching", "deduced conflicting")):
        return False, "the Qt 5 tree rejected the count+value probe for another reason"
    return True, "probe: Qt 5 rejects QStringList(count, value) and accepts the append loop"


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


def is_qt6_include(directory):
    parts = [part for part in re.split(r"[\\/]+", directory.rstrip("\\/")) if part]
    return "qt6" in parts


def is_qt6_compact(argument):
    """Whether a joined -I/-isystem option names a Qt 6 include directory."""
    for prefix in ("-isystem", "-I"):
        if argument.startswith(prefix):
            return is_qt6_include(argument[len(prefix):])
    return False


def retarget_step(arguments, index, source):
    """(kept arguments, consumed count) for one recorded compile argument."""
    argument = arguments[index]
    if argument in ("-o", "-MF", "-MT"):
        return [], 2
    if argument in ("-c", "-MD", "-MMD", "-MP", "-Werror"):
        return [], 1
    if argument in ("-I", "-isystem") and index + 1 < len(arguments):
        value = arguments[index + 1]
        return ([] if is_qt6_include(value) else [argument, value]), 2
    if argument == source or is_qt6_compact(argument):
        return [], 1
    return [argument], 1


def retarget(entry, flags, source, include_dir):
    """The recorded Qt 6 compile line retargeted at the Qt 5 headers."""
    arguments = entry.get("arguments")
    if arguments is None:
        arguments = shlex.split(entry.get("command", ""))
    out = [arguments[0]]
    index = 1
    while index < len(arguments):
        kept, consumed = retarget_step(arguments, index, source)
        out += kept
        index += consumed
    return out[:1] + ["-I" + include_dir] + out[1:] + list(flags) + ["-fsyntax-only", source]


def generate_moc(moc, source, workdir, relative):
    generated = os.path.join(workdir, os.path.basename(source).replace(".cpp", ".moc"))
    made = run([moc, source, "-o", generated])
    if made is not None and made.returncode == 0:
        return None
    return f"{relative}: the Qt 5 moc failed"


def failure_diagnostic(result):
    combined = result.stdout + result.stderr
    diagnostic = "\n".join(line for line in combined.splitlines()
                           if "error:" in line or "note:" in line)[:2000]
    if any(marker in combined for marker in ("InputIterator", "no matching", "deduced conflicting")):
        return f"Qt 5 rejected it ({diagnostic})"
    return "Qt 5 rejected it for another reason (" + diagnostic + ")"


def probe_translation_unit(flags, root, build, moc, workdir, relative):
    """Compile a first-party translation unit against Qt 5; None when it is clean."""
    entries = compile_database(build)
    if entries is None:
        return f"no compile database at {os.path.join(build, 'compile_commands.json')}"
    entry = entry_for(entries, root, relative)
    if entry is None:
        return f"{relative}: no entry in the compile database"
    source = os.path.realpath(os.path.join(root, relative))
    failure = generate_moc(moc, source, workdir, relative)
    if failure is not None:
        return failure
    result = run(retarget(entry, flags, source, workdir), cwd=entry.get("directory") or root)
    if result is None:
        return f"{relative}: the Qt 5 probe could not run"
    if result.returncode == 0:
        return None
    return f"{relative}: " + failure_diagnostic(result)


def confirm_findings(findings, flags, root, build, moc, workdir):
    """Keep a finding unless its translation unit compiles clean against Qt 5."""
    if flags is None or moc is None:
        return findings, 0
    kept, confirmed = [], 0
    probes = {}
    for path, line in findings:
        relative = path.replace(os.sep, "/")
        if relative not in probes:
            probes[relative] = probe_translation_unit(flags, root, build, moc, workdir, relative)
        if probes[relative] is None:
            continue
        kept.append((path, line, probes[relative]))
        if "Qt 5 rejected it" in probes[relative]:
            confirmed += 1
    return kept, confirmed


def run_self_test(cxx, workdir):
    failures = self_test_fixtures()
    flags = qt5_flags()
    if flags is None:
        print("probe: skipped (no Qt 5 development tree found)")
        return failures
    usable, verdict = fixture_verdict(cxx, flags, workdir)
    print(verdict)
    if not usable:
        failures.append(verdict)
    return failures


def report_findings(findings, confirmed):
    for path, line, why in findings:
        print(f"{path}:{line}: QStringList(count, value) is not Qt 5 portable")
        print(f"  {why}")
    if not findings:
        print("Qt5QStringListSizeCheck: PASS (no QStringList count+value construction)")
        return 0
    print(f"Qt5QStringListSizeCheck: {len(findings)} finding(s), {confirmed} confirmed by a Qt 5 compile")
    return 1


def check_tree(root, build, workdir):
    findings = scan_tree(root)
    flags = qt5_flags()
    moc = qt5_moc()
    if flags is not None and moc is None:
        print("probe: skipped (no Qt 5 moc found)")
    findings, confirmed = confirm_findings(findings, flags, root, build, moc, workdir)
    return report_findings(findings, confirmed)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    parser.add_argument("--build-dir")
    parser.add_argument("--cxx", default=os.environ.get("CXX") or shutil.which("c++") or "c++")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    root = os.path.abspath(args.root)
    build = os.path.abspath(args.build_dir or os.path.join(root, "build"))
    workdir = tempfile.mkdtemp(prefix="qt5-qstringlist-")
    try:
        failures = run_self_test(args.cxx, workdir)
        if failures:
            for failure in failures:
                print("FAIL: " + failure)
            return 1
        if args.self_test:
            print("Qt5QStringListSizeCheck self-test: PASS")
            return 0
        return check_tree(root, build, workdir)
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
