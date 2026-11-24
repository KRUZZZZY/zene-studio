#!/usr/bin/env python3
"""Fail when first-party code assigns a bare braced list to a QStringList.

Cluster ci-aa1160348d9be203 (2026-11-14), hosted `build / linux-arm64` job at
c20ef591b571e3c3dc7b108107d166497f1b34be.  That job builds with Qt 5 (WANT_QT6
defaults OFF, deps install qtbase5-dev), where QStringList is its own class with
`operator=(const QList<QString>&)`, `operator=(QList<QString>&&)` and the implicit
copy/move assignment.  A bare braced list converts equally well to QList<QString>
and to QStringList, so

    tests/src/core/ImportTrackIoTest.cpp:344
        plan.documents = {UnparsableTrack, trackDocument(Lead)};

stopped the whole build:

    error: ambiguous overload for 'operator=' (operand types are 'QStringList'
           and '<brace-enclosed initializer list>')

Qt 6 aliases QStringList to QList<QString>, whose initializer_list assignment is a
better match, so every Qt 6 host here compiled the same line.  The portable spelling
is an explicit `QStringList{...}` (or QList<QString>{...}), which both versions accept.

Cluster ci-48dcc062d04c645f (fix 6474195bd6, hosted `build / linux-x86_64`): the
fake RecordingMidiClient in ControllerScriptTest, ExternalMidiDeviceTest and
ExternalDeviceCardTest declares its members brace-initialised
(`QStringList readable{QStringLiteral("Pad:in")};`), and the tests assigned
`m_client.readable = {...}`.  The member index only recognised `;`, `=`, `,` and `[`
after a QStringList member name, so those members were never indexed and the nine
assignments passed the scan.  A brace initialiser now records the member too.

The check has two deterministic halves:

  * a source scan of every first-party C/C++ file for an assignment of a bare braced
    list to a QStringList-typed member or local.  It reads sources only, so it is red
    on the Qt 6 hosts where the defect compiles.
  * a compile probe: where pkg-config finds a Qt 5 tree, the known-bad and known-good
    forms are compiled against it; the bad one must be rejected with the ambiguity
    above and the good one accepted.  Where pkg-config finds none, the probe reports
    itself skipped (never passed).

The scan's own fixtures prove both directions.  A finding is confirmed by compiling
its translation unit through build/compile_commands.json against Qt 5 when possible;
a unit that compiles clean is a scan false positive and is not reported.

Usage: Qt5QStringListAssignCheck.py --root <src> [--build-dir <build>] [--cxx <cxx>]
       Qt5QStringListAssignCheck.py --self-test
Exit codes: 0 = clean, 1 = finding (or a failed fixture), 2 = cannot run.
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
    "plugins/NeuralAmp/rtneural/",
    "plugins/NeuralAmp/nam/",
    "plugins/NeuralAmp/tests/",
    "plugins/RnnoiseDenoiser/rnnoise/",
    "plugins/SfzPlayer/sfizz/",
)

SKIP_DIRS = {".git", "build", "__pycache__"}

# Comments and string/char literals are blanked before the shape scan (line numbers kept).
NON_CODE = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
CLASS = re.compile(r'\b(?:class|struct)\s+([A-Za-z_]\w*)[^;{]*\{')
MEMBER = re.compile(r'\bQStringList\s+(?:const\s+)?[&*]?\s*([A-Za-z_]\w*)\s*(?:[;=,\[{])')
DECLARATION = re.compile(
    r'\b(?:const\s+)?(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)\s*'
    r'(?:const\s*)?(?:[&*]\s*)?([A-Za-z_]\w*)\s*(?=[;=,(\[)])')
ASSIGN = re.compile(
    r'(?<![\w.])(?:(?P<receiver>[A-Za-z_]\w*)\s*(?P<op>\.|->)\s*)?'
    r'(?P<name>[A-Za-z_]\w*)\s*=\s*\{')
# Text just before the assigned name: a type spelling means the brace list initialises
# a declaration (`QStringList names = {...}`), which is valid on Qt 5 and not a finding.
DECL_BEFORE = re.compile(
    r'(?:[A-Za-z_]\w*\s*(?:::\s*[A-Za-z_]\w*)*\s*(?:<[^;{}]*>)?\s*'
    r'(?:const\s*)?[&*]*)\s*$')

BAD_FIXTURE = """\
struct Plan { QStringList documents; };
void bad(Plan& plan) { plan.documents = {QStringLiteral("a")}; }
void badLocal() { QStringList names; names = {QStringLiteral("a")}; }
struct Client { QStringList readable{QStringLiteral("x")}; };
void badInit(Client& c) { c.readable = {QStringLiteral("a")}; }
"""

GOOD_FIXTURE = """\
struct Plan { QStringList documents; };
void good(Plan& plan) { plan.documents = QStringList{QStringLiteral("a")}; }
void goodLocal() { QStringList names = {QStringLiteral("a")}; }
struct Client { QStringList readable{QStringLiteral("x")}; };
void goodInit(Client& c) { c.readable = QStringList{QStringLiteral("a")}; }
"""

QT5_BAD_PROBE = """\
#include <QStringList>
void bad(QStringList& names) { names = {QStringLiteral("a")}; }
"""

QT5_GOOD_PROBE = """\
#include <QStringList>
void good(QStringList& names) { names = QStringList{QStringLiteral("a")}; }
"""


def blank_non_code(text):
    """Replace comments and literals with whitespace, keeping newlines and offsets."""
    def blank(match):
        text = match.group(0)
        newlines = text.count("\n")
        return "\n" * newlines + " " * (len(text) - newlines)
    return NON_CODE.sub(blank, text)


def class_members(text):
    """Map each class/struct name to the members it declares as QStringList."""
    members = {}
    for match in CLASS.finditer(text):
        depth, index = 1, match.end()
        while index < len(text) and depth:
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
            index += 1
        names = set(MEMBER.findall(text[match.end():index]))
        if names:
            members.setdefault(match.group(1), set()).update(names)
    return members


def declarations(text):
    """Every (name, type, offset) declaration the scan can resolve locally."""
    return [(m.group(2), m.group(1), m.start()) for m in DECLARATION.finditer(text)]


def is_declaration(text, offset):
    return bool(DECL_BEFORE.search(text[max(0, offset - 120):offset]))


def assigned_type(decls, receiver, name, offset):
    """The type of the nearest declaration of the assigned name before offset."""
    prior = [d for d in decls if d[0] == (receiver or name) and d[2] < offset]
    if not prior:
        return None
    return max(prior, key=lambda d: d[2])[1]


def scan_text(path, text, members):
    """Find assignments of a bare braced list to a QStringList member or local."""
    code = blank_non_code(text)
    decls = declarations(code)
    findings = []
    for match in ASSIGN.finditer(code):
        offset = match.start()
        receiver, name = match.group("receiver"), match.group("name")
        if not receiver and is_declaration(code, offset):
            continue
        declared = assigned_type(decls, receiver, name, offset)
        if declared is None:
            continue
        hit = name in members.get(declared, set()) if receiver else declared == "QStringList"
        if hit:
            findings.append((path, code[:offset].count("\n") + 1, declared))
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
    """Build the class index once, then scan every first-party source."""
    texts = {path: read_text(root, path) for path in source_files(root)}
    members = {}
    for text in texts.values():
        for name, found in class_members(blank_non_code(text)).items():
            members.setdefault(name, set()).update(found)
    findings = []
    for path, text in texts.items():
        findings.extend(scan_text(path, text, members))
    return findings


def self_test_fixtures():
    failures = []
    for path, text, expected in (("bad.cpp", BAD_FIXTURE, 3), ("good.cpp", GOOD_FIXTURE, 0)):
        found = scan_text(path, text, class_members(blank_non_code(text)))
        if len(found) != expected:
            failures.append(f"scan fixture {path}: expected {expected}, found {len(found)}")
    return failures


def pkg_config_flags():
    try:
        result = subprocess.run(
            ["pkg-config", "--cflags", "Qt5Core", "Qt5Gui", "Qt5Widgets", "Qt5Xml", "Qt5Test"],
            capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return None
    if result.returncode != 0:
        return None
    return shlex.split(result.stdout)


def compile_fixture(cxx, flags, workdir, name, source):
    path = os.path.join(workdir, name)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(source)
    return subprocess.run([cxx, "-fsyntax-only", "-fPIC"] + flags + [path],
                          cwd=workdir, capture_output=True, text=True, timeout=300)


def self_test_qt5(cxx, workdir):
    flags = pkg_config_flags()
    if flags is None:
        print("Qt5QStringListAssignCheck: Qt 5 not found; compile probe skipped")
        return []
    bad = compile_fixture(cxx, flags, workdir, "bad_probe.cpp", QT5_BAD_PROBE)
    if bad.returncode == 0 or "ambiguous" not in bad.stderr:
        return ["Qt 5 accepted the bare braced assignment (probe premise broken)"]
    good = compile_fixture(cxx, flags, workdir, "good_probe.cpp", QT5_GOOD_PROBE)
    if good.returncode != 0:
        return ["Qt 5 rejected the explicit QStringList assignment: " + good.stderr.strip()]
    return []


def split_command(entry):
    if entry.get("command"):
        return shlex.split(entry["command"])
    return list(entry.get("arguments") or [])


def compile_flags(args):
    out, index = [], 0
    while index < len(args):
        arg = args[index]
        if arg in ("-o", "-MF", "-MT"):
            index += 2
            continue
        if arg in ("-MD", "-MMD", "-MP", "-c"):
            index += 1
            continue
        out.append(arg)
        index += 1
    return out


def qt5_command(entry, flags, source):
    args = compile_flags(split_command(entry))
    out = [args[0]]
    index = 1
    while index < len(args):
        arg = args[index]
        if arg in ("-I", "-isystem") and index + 1 < len(args):
            if "/qt6" not in args[index + 1]:
                out.extend((arg, args[index + 1]))
            index += 2
            continue
        if arg == source:
            index += 1
            continue
        if "/qt6" in arg:
            index += 1
            continue
        out.append(arg)
        index += 1
    return out + flags + ["-fsyntax-only", source]


def find_entry(database, path):
    target = os.path.realpath(path)
    for entry in database:
        if os.path.realpath(entry.get("file", "")) == target:
            return entry
    return None


def moc_include(source):
    """Generate the Qt 5 .moc a test unit includes, if any; return its include dir."""
    try:
        with open(source, "r", encoding="utf-8", errors="replace") as handle:
            text = handle.read()
    except OSError:
        return None
    match = re.search(r'#include\s+"([\w./-]+\.moc)"', text)
    if not match:
        return ""
    moc = os.environ.get("QT5_MOC") or "moc-qt5"
    if not shutil.which(moc):
        moc = "moc"
    if not shutil.which(moc):
        return None
    workdir = tempfile.mkdtemp(prefix="qt5-moc-")
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(source))))
    command = [moc]
    for directory in ("include", "src"):
        if os.path.isdir(os.path.join(root, directory)):
            command.append("-I" + os.path.join(root, directory))
    command.extend((source, "-o", os.path.join(workdir, match.group(1))))
    result = subprocess.run(command, capture_output=True, text=True, timeout=300)
    return workdir if result.returncode == 0 else None


def probe_finding(entry, flags, source):
    """Compile a flagged unit against Qt5: 'clean', 'confirmed' or 'inconclusive'."""
    moc_dir = moc_include(source)
    if moc_dir is None:
        return "inconclusive"
    command = qt5_command(entry, flags, source)
    if moc_dir:
        command.insert(1, "-I" + moc_dir)
    result = subprocess.run(command, cwd=os.path.dirname(source), capture_output=True,
                            text=True, timeout=600)
    if result.returncode == 0:
        return "clean"
    combined = result.stdout + result.stderr
    return "confirmed" if "ambiguous" in combined and "operator=" in combined else "inconclusive"


def confirm_findings(findings, root, build_dir):
    """Keep a finding unless its own translation unit compiles clean against Qt5."""
    database_path = os.path.join(build_dir, "compile_commands.json")
    flags = pkg_config_flags()
    if flags is None or not os.path.isfile(database_path):
        return findings, len(findings)
    with open(database_path, "r", encoding="utf-8") as handle:
        database = json.load(handle)
    kept, confirmed = [], 0
    for path, line, declared in findings:
        entry = find_entry(database, os.path.join(root, path))
        if entry is None:
            kept.append((path, line, declared))
            continue
        verdict = probe_finding(entry, flags, os.path.join(root, path))
        if verdict != "clean":
            kept.append((path, line, declared))
        if verdict == "confirmed":
            confirmed += 1
    return kept, confirmed


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=".")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        workdir = tempfile.mkdtemp(prefix="qt5-qstringlist-")
        failures = self_test_fixtures() + self_test_qt5(args.cxx, workdir)
        for failure in failures:
            print("FAIL: " + failure)
        print("Qt5QStringListAssignCheck self-test:", "FAIL" if failures else "PASS")
        return 1 if failures else 0

    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print(f"Qt5QStringListAssignCheck: no source root {root}", file=sys.stderr)
        return 2
    workdir = tempfile.mkdtemp(prefix="qt5-qstringlist-")
    failures = self_test_fixtures() + self_test_qt5(args.cxx, workdir)
    findings, confirmed = confirm_findings(scan_tree(root), root, args.build_dir)
    for path, line, declared in findings:
        print(f"{path}:{line}: bare braced list assigned to QStringList ({declared})")
    for failure in failures:
        print("FAIL: " + failure)
    if findings or failures:
        print(f"Qt5QStringListAssignCheck: {len(findings)} finding(s), "
              f"{confirmed} confirmed by a Qt 5 compile")
        return 1
    print("Qt5QStringListAssignCheck: PASS (no bare QStringList brace assignments)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
