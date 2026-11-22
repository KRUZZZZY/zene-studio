#!/usr/bin/env python3
"""QFileInfo date accessors must have <QDateTime> in scope by name.

Cluster ci-84e380615213284e (2026-11-14), hosted `build / windows-arm64`:
plugins/CarlaBase/CarlaVst2Discovery.cpp:204 calls
`info.lastModified().toMSecsSinceEpoch()` but includes <QFileInfo> without
<QDateTime>. That builds only where QFileInfo pulls QDateTime in - Qt 6.12 does,
Qt 5.15 (the hosted Qt 5 jobs) and Qt 6.8 (macOS) only forward-declare it:

    CarlaVst2Discovery.cpp:204:31: error: calling 'lastModified' with incomplete
        return type 'QDateTime'
    CarlaVst2Discovery.cpp:204:45: error: member access into incomplete type 'QDateTime'

Every first-party translation unit that calls a QFileInfo date accessor
(lastModified, birthTime, metadataChangeTime, lastRead, fileTime) is compiled
through its OWN line in build/compile_commands.json, against the failing header
shape: the real header where it already is that shape, a shadow of
qfileinfo.h/qdirlisting.h where Qt 6.12 leaks the include, and a real Qt 5 tree
when one is installed (--qt5-root, QT5_INCLUDE_DIR or pkg-config Qt5Core).
Compiles use the configured compiler's syntax-only switch: -fsyntax-only, or
/Zs for cl.exe and clang-cl (the msvc-x64 job runs this test).

A unit that does not build here for an unrelated reason is SKIP; a baseline
compile whose diagnostic is the CI's own (an INCOMPLETE QDateTime, in clang's,
gcc's or MSVC's spelling) is FAIL, never SKIP - on the failing shape that error
IS the baseline compile, and skipping it would pass while the CI job is red.
Fixtures prove both directions, and the file is named after the CTest test it
registers so the negative-control harness pairs them.

Usage: Qt5QFileInfoDateCheck.py --build-dir <build> [--root <src>] [--cxx <cxx>]
       [--moc <moc>] [--qt5-root <root>]
Exit codes: 0 = clean, 1 = a date accessor lacks <QDateTime>, 2 = cannot run.
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

# QFileInfo's date accessors. `created()` is Qt 5 only and is left out on purpose: the
# check is about the API both supported Qt versions have.
ACCESSOR = re.compile(r"\.(?:lastModified|birthTime|metadataChangeTime|lastRead|fileTime)\s*\(")

SOURCE_SUFFIXES = (".cpp", ".cc", ".cxx")

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

# The Qt Core headers that supply QDateTime transitively in some Qt builds and not in
# others. qfileinfo.h is the one the CI failure names; qdirlisting.h is what QDir pulls
# in on Qt 6, and Qt 5 has no QDirListing at all.
LEAKING_HEADERS = ("qfileinfo.h", "qdirlisting.h")

BAD_FIXTURE = """\
#include <QFileInfo>
qint64 modified(const QFileInfo& info) { return info.lastModified().toMSecsSinceEpoch(); }
"""

GOOD_FIXTURE = """\
#include <QDateTime>
#include <QFileInfo>
qint64 modified(const QFileInfo& info) { return info.lastModified().toMSecsSinceEpoch(); }
"""


WINDOWS_COMMAND_RE = re.compile(r"(?:^|[\s=\"'])[A-Za-z]:[\\/]|\\\\[^\"']")


def split_command(entry):
    if entry.get("command"):
        command = entry["command"]
        if WINDOWS_COMMAND_RE.search(command):
            escaped_quote = "\uE000"
            command = command.replace('\\"', escaped_quote)
            lexer = shlex.shlex(command, posix=True)
            lexer.whitespace_split = True
            lexer.commenters = ""
            lexer.escape = ""
            return [arg.replace(escaped_quote, '"') for arg in lexer]
        return shlex.split(command)
    return list(entry.get("arguments") or [])


def compile_flags(entry):
    """The recorded command without its output/depfile arguments or compile action."""
    args = split_command(entry)
    out, index = [], 0
    while index < len(args):
        arg = args[index]
        if arg in ("-o", "-MF", "-MT", "/Fo"):
            index += 2
            continue
        if arg.startswith("/Fo") or arg in ("-MD", "-MMD", "-MP", "-c", "/c"):
            index += 1
            continue
        out.append(arg)
        index += 1
    return out


def is_msvc(compiler):
    """True for the MSVC driver family: cl.exe and clang-cl (both take /Zs, /I, /std:)."""
    return os.path.basename(compiler.replace("\\", "/")).lower() in (
        "cl", "cl.exe", "clang-cl", "clang-cl.exe")


def switch(compiler, msvc, other):
    """MSVC's spelling of a switch on cl/clang-cl, the gcc/clang one elsewhere."""
    return msvc if is_msvc(compiler) else other


def include_dirs(args):
    out, index = [], 0
    while index < len(args):
        arg = args[index]
        if arg in ("-I", "-isystem", "/I", "/external:I") and index + 1 < len(args):
            out.append(args[index + 1])
            index += 2
            continue
        if arg.startswith(("-I", "/I", "/external:I")):
            value = arg[len("/external:I"):] if arg.lower().startswith("/external:i") else arg[2:]
            if value:
                out.append(value)
        elif arg.startswith("-isystem") and len(arg) > len("-isystem"):
            out.append(arg[len("-isystem"):])
        index += 1
    return out


def source_of(entry):
    path = entry["file"]
    return os.path.normpath(path if os.path.isabs(path) else os.path.join(entry["directory"], path))


def find_qtcore(args, directory):
    """The first Qt root on the recorded include path that carries QtCore/qfileinfo.h."""
    for include in include_dirs(args):
        root = include if os.path.isabs(include) else os.path.join(directory, include)
        if os.path.isfile(os.path.join(root, "QtCore", "qfileinfo.h")):
            return os.path.normpath(root)
        if os.path.isfile(os.path.join(root, "qfileinfo.h")):
            return os.path.normpath(os.path.dirname(root))
    return None


def shadow_shape(real):
    """A qfileinfo/qdirlisting header without its QDateTime include, or None if already so."""
    text = open(real, encoding="utf-8", errors="replace").read()
    if not re.search(r"^#\s*include\s*[<\"]QtCore/qdatetime\.h[>\"]", text, re.M):
        return None
    text = re.sub(r"^#\s*include\s*[<\"]QtCore/qdatetime\.h[>\"].*$",
                  "// shadow: this Qt header must not supply QDateTime", text, flags=re.M)
    text = re.sub(r"^#\s*include\s*[<\"]QtCore/qtimezone\.h[>\"].*$",
                  "// shadow: QTimeZone stays forward-declared", text, flags=re.M)
    if not re.search(r"\bclass QDateTime;", text):
        text = text.replace("QT_BEGIN_NAMESPACE\n",
                            "QT_BEGIN_NAMESPACE\n\nclass QDateTime;\nclass QTimeZone;\n", 1)
    # Qt 6 defines the accessors inline; Qt 5 only declares them.
    text = re.sub(
        r"(QDateTime (?:birthTime|metadataChangeTime|lastModified|lastRead)\([^)]*\))"
        r"\s*(?:const\s*)?\{ return fileTime\([^)]*\); \}",
        r"\1 const;", text)
    return text


def build_shadow(qtcore, shadow):
    """Write the shadow headers; return their module-relative names."""
    written = []
    for name in LEAKING_HEADERS:
        real = os.path.join(qtcore, "QtCore", name)
        if not os.path.isfile(real):
            continue
        text = shadow_shape(real)
        if text is None:
            continue
        destination = os.path.join(shadow, "QtCore", name)
        os.makedirs(os.path.dirname(destination), exist_ok=True)
        with open(destination, "w", encoding="utf-8") as handle:
            handle.write(text)
        written.append(name)
    return written


def run(command, directory, timeout=600):
    return subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=timeout)


def errors_of(result):
    output = (result.stderr or "") + "\n" + (result.stdout or "")
    return [line.strip() for line in output.splitlines() if "error" in line.lower()]


def date_diagnostic(result):
    """The first compiler error of the CI's class - an INCOMPLETE QDateTime - or None.

    clang spells it "incomplete return type 'QDateTime'", gcc "invalid use of incomplete
    type", MSVC "use of undefined type"; a baseline error that merely mentions
    QDateTime otherwise is a different host problem and stays skippable.
    """
    for line in errors_of(result):
        lowered = line.lower()
        if "qdatetime" in lowered and ("incomplete" in lowered or "undefined type" in lowered):
            return line
    return None


def substitute_paths(args, substitutions):
    """Rewrite the recorded toolchain roots, so the same TU can be built against Qt 5."""
    def rebase(path, old, new):
        normalized = path.replace("\\", "/")
        root = old.replace("\\", "/").rstrip("/")
        if normalized.casefold() == root.casefold():
            suffix = ""
        elif normalized.casefold().startswith((root + "/").casefold()):
            suffix = normalized[len(root) + 1:]
        else:
            return None
        rebased = new.replace("\\", "/").rstrip("/")
        if suffix:
            rebased += "/" + suffix
        return rebased.replace("/", os.sep) if os.name == "nt" else rebased

    out = []
    for arg in args:
        for old, new in substitutions:
            prefix = next((item for item in ("-I", "/I", "/external:I")
                           if arg.lower().startswith(item.lower())), "")
            path = arg[len(prefix):] if prefix else arg
            rebased = rebase(path, old, new)
            if rebased is not None:
                arg = prefix + rebased
        out.append(arg)
    return out


def tu_command(entry, includes, substitutions=()):
    """The recorded compile line, made syntax-only and preceded by @includes."""
    args = substitute_paths(compile_flags(entry), substitutions)
    return ([args[0], switch(args[0], "/Zs", "-fsyntax-only")]
            + [switch(args[0], "/I", "-I") + path for path in includes] + args[1:])


def compile_tu(entry, includes, substitutions=()):
    command = tu_command(entry, includes, substitutions)
    return run(command, entry["directory"])


def moc_include(entry, source, tempdir, moc):
    """Generate the .moc a test TU includes itself; (directory or None, detail)."""
    match = re.search(r'#include\s+"([\w.+-]+\.moc)"', open(source, encoding="utf-8", errors="replace").read())
    if match is None:
        return None, ""
    if not moc:
        return None, "includes %s but no moc was given" % match.group(1)
    output = os.path.join(tempdir, match.group(1))
    command = [moc, source, "-o", output]
    for directory in include_dirs(compile_flags(entry)):
        command += ["-I", directory]
    produced = run(command, entry["directory"])
    if produced.returncode != 0:
        detail = (produced.stderr or produced.stdout).strip().splitlines()
        return None, "moc failed: " + (detail[-1] if detail else "no output")
    return tempdir, ""


def fixture_command(cxx, includes, source):
    return ([cxx, switch(cxx, "/std:c++20", "-std=gnu++20"), switch(cxx, "/Zs", "-fsyntax-only")]
            + [switch(cxx, "/I", "-I") + path for path in includes] + [source])


def compile_fixture(cxx, includes, source, directory):
    return run(fixture_command(cxx, includes, source), directory, timeout=300)


def fixtures(cxx, includes, tempdir):
    """(bad rejected with the QDateTime diagnostic, good accepted)."""
    bad = os.path.join(tempdir, "bad.cpp")
    good = os.path.join(tempdir, "good.cpp")
    for path, text in ((bad, BAD_FIXTURE), (good, GOOD_FIXTURE)):
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
    bad_run = compile_fixture(cxx, includes, bad, tempdir)
    good_run = compile_fixture(cxx, includes, good, tempdir)
    hidden = bad_run.returncode != 0 and date_diagnostic(bad_run) is not None
    print("fixture without <QDateTime>: " + ("rejected with the CI's QDateTime error" if hidden
                                               else "compiled (the shape does not reproduce)"))
    print("fixture with <QDateTime>: " + ("compiled" if good_run.returncode == 0 else "FAILED"))
    return hidden, good_run.returncode == 0


def candidate_tus(root):
    """Tracked first-party translation units that name a QFileInfo date accessor."""
    for directory, subdirs, files in os.walk(root):
        subdirs[:] = sorted(d for d in subdirs if d != ".git" and not d.startswith("build"))
        for name in sorted(files):
            path = os.path.join(directory, name)
            relative = os.path.relpath(path, root)
            if not relative.endswith(SOURCE_SUFFIXES) or relative.startswith(EXCLUDED_PREFIXES):
                continue
            try:
                text = open(path, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            if ACCESSOR.search(text):
                yield relative


def load_build(build):
    """(entries, entries by source path (first line wins), Qt root) or (None, error)."""
    commands = os.path.join(build, "compile_commands.json")
    if not os.path.isfile(commands):
        return None, "no compile_commands.json in " + build
    entries = json.load(open(commands))
    if not entries:
        return None, "compile_commands.json is empty"
    by_source = {source_of(entry): entry for entry in reversed(entries)}
    for entry in entries:
        qtcore = find_qtcore(split_command(entry), entry["directory"])
        if qtcore:
            return (entries, by_source, qtcore), ""
    return None, "no QtCore/qfileinfo.h on any recorded include path"


def prepare_shape(qtcore, cxx, tempdir):
    """Build the failing header shape and prove it reproduces the CI error: (extra, ok, detail)."""
    shadow = os.path.join(tempdir, "shadow")
    written = build_shadow(qtcore, shadow)
    extra = [shadow] if written else []
    print("shape:", ("shadow of " + ", ".join(written)) if written else "this host's real headers")
    include_roots = extra + [qtcore, os.path.join(qtcore, "QtCore")]
    hidden, good_ok = fixtures(cxx, include_roots, tempdir)
    if not hidden or not good_ok:
        return extra, False, ("the fixture does not reproduce the CI diagnostic, so the check "
                              "cannot distinguish the defect")
    if written:
        host_run = compile_fixture(cxx, [qtcore, os.path.join(qtcore, "QtCore")],
                                   os.path.join(tempdir, "bad.cpp"), tempdir)
        print("host headers supply QDateTime transitively:", "yes" if host_run.returncode == 0 else "no")
    return extra, True, ""


def baseline_verdict(result, detail_limit):
    """('fail', diagnostic) when the baseline compile is already the CI's error, else None.

    On the failing header shape nothing supplies QDateTime, so the incomplete-type error
    comes from the BASELINE compile: that is the regression failing, not a host limit.
    """
    diagnostic = date_diagnostic(result)
    if diagnostic is None:
        return None
    return "fail", "baseline compile reports the CI's error: " + diagnostic[:detail_limit]


def windows_command_self_test():
    """Windows command parsing and include rebasing, covered on every host."""
    failures = []
    windows_command = split_command({"command":
        '"C:\\Program Files\\Microsoft Visual Studio\\cl.exe" '
        '/I"C:\\Program Files\\Qt\\include" /DVALUE=\\"test\\" /c source.cpp'})
    expected = [r"C:\Program Files\Microsoft Visual Studio\cl.exe",
                r"/IC:\Program Files\Qt\include", '/DVALUE="test"', "/c", "source.cpp"]
    if windows_command != expected:
        failures.append("Windows compile command did not preserve quoted paths")
    rebased = substitute_paths([r"/IC:\Qt6\include\QtCore"],
                               ((r"C:\Qt6\include", r"C:\Qt5\include"),))[0]
    if rebased.replace("\\", "/") != "/IC:/Qt5/include/QtCore":
        failures.append("Windows include root was not substituted")
    return failures


MSVC_ENTRY = {"directory": r"C:\build", "file": r"C:\src\CarlaVst2Discovery.cpp",
              "command": r'"C:\VC\bin\cl.exe" /nologo /c /FoCMakeFiles\x.obj '
                         r'/IC:\Qt6\include /IC:\Qt6\include\QtCore '
                         r'/external:IC:\deps\include /std:c++20 /EHsc C:\src\CarlaVst2Discovery.cpp'}
MSVC_REBASE = ((r"C:\Qt6\include", r"C:\Qt5\include"),)


def switch_self_test():
    """The switches cl.exe, clang-cl and gcc/clang must each receive."""
    expected = ((r"C:\Qt\bin\cl.exe", "/Zs", "/I", "/std:c++20"),
                ("clang-cl", "/Zs", "/I", "/std:c++20"),
                ("/usr/bin/c++", "-fsyntax-only", "-I", "-std=gnu++20"))
    failures = [] if is_msvc(r"C:\Program Files\Microsoft Visual Studio\cl.exe") \
        else ["MSVC compiler was not identified"]
    for compiler, syntax, include, standard in expected:
        if (switch(compiler, "/Zs", "-fsyntax-only") != syntax
                or switch(compiler, "/I", "-I") != include
                or switch(compiler, "/std:c++20", "-std=gnu++20") != standard):
            failures.append("wrong switches for " + compiler)
    return failures


def msvc_command_self_test():
    """A recorded MSVC compile line must become a /Zs check rebased for Qt 5."""
    failures = []
    args = tu_command(MSVC_ENTRY, [r"C:\shadow"], MSVC_REBASE)
    joined = " ".join(args).replace("\\", "/")
    if "/Zs" not in args:
        failures.append("an MSVC compile line is not turned into a /Zs syntax check")
    if "/c" in args or any(a.startswith("/Fo") for a in args):
        failures.append("an MSVC syntax check still carries /c or /Fo")
    if "/IC:/Qt5/include" not in joined or "/IC:/Qt6/include" in joined:
        failures.append("an MSVC include root is not rebased for the Qt 5 sweep")
    if include_dirs(["/I", r"C:\Qt\include"]) != [r"C:\Qt\include"]:
        failures.append("a separate /I argument is not read")
    fixture = fixture_command("cl.exe", [r"C:\Qt5\include"], r"C:\tmp\bad.cpp")
    if fixture[1:] != ["/std:c++20", "/Zs", r"/IC:\Qt5\include", r"C:\tmp\bad.cpp"]:
        failures.append("the MSVC fixture command is not /std:c++20 /Zs /I")
    return failures


def classifier_self_test():
    """Prove baseline classification for clang, gcc, MSVC, and unrelated errors."""
    def result(rc, text, stderr=True):
        return subprocess.CompletedProcess(["c++"], rc, "" if stderr else text, text if stderr else "")
    must_fail = {  # every supported spelling of the CI's own error
        "clang": result(1, "x.cpp:204:31: error: calling 'lastModified' with incomplete return type 'QDateTime'\n"),
        "gcc": result(1, "x.cpp:204:50: error: invalid use of incomplete type 'class QDateTime'\n"),
        "msvc": result(1, "x.cpp(204): error C2027: use of undefined type 'QDateTime'\n", stderr=False)}
    may_skip = {  # a different host problem, not this defect
        "missing header": result(1, "x.cpp:12:10: fatal error: QWidget: No such file or directory\n"),
        "other QDateTime use": result(1, "x.cpp:9:5: error: no matching function for call to 'QDateTime::fromString'\n"),
        "success": result(0, "")}
    failures = ["%s spelling of the incomplete-QDateTime error is not classified FAIL" % name
                for name, run in must_fail.items() if (baseline_verdict(run, 200) or ("",))[0] != "fail"]
    failures += ["%s is not skippable" % name
                 for name, run in may_skip.items() if baseline_verdict(run, 200) is not None]
    return failures


def self_test():
    """Prove the Windows/MSVC command path and the classifier before the sweep."""
    failures = windows_command_self_test() + switch_self_test() + msvc_command_self_test() + classifier_self_test()
    for failure in failures:
        print("SELF-TEST FAIL: " + failure)
    if not failures:
        print("self-test: baseline QDateTime error fails; unrelated baseline error is skippable")
    return not failures


def check_tu(relative, entry, extra, moc, tempdir, root, substitutions=()):
    """('pass' | 'fail' | 'skip', detail) for one translation unit."""
    source = os.path.normpath(os.path.join(root, relative))
    # Test translation units include their own .moc, so generate it exactly as automoc
    # would; without one the unit is skipped, never failed.
    moc_dir, moc_detail = moc_include(entry, source, tempdir, moc)
    if moc_dir is None and moc_detail:
        return "skip", moc_detail
    includes = [moc_dir] if moc_dir else []
    base = compile_tu(entry, includes, substitutions)
    if base.returncode != 0:
        verdict = baseline_verdict(base, 220)
        if verdict is not None:
            return verdict
        detail = errors_of(base)
        return "skip", "does not build on this host: " + (detail[0][:160] if detail else "exit %d" % base.returncode)
    strict = compile_tu(entry, extra + includes, substitutions)
    if strict.returncode == 0:
        return "pass", ""
    diagnostic = date_diagnostic(strict)
    if diagnostic is None:
        return "skip", "fails under the shadow for an unrelated reason"
    return "fail", diagnostic[:220]


def sweep(root, by_source, extra, moc, tempdir, substitutions=()):
    """Check every candidate TU; (failures, checked, skipped), printing each verdict."""
    failures, checked, skipped = [], 0, 0
    for relative in candidate_tus(root):
        entry = by_source.get(os.path.normpath(os.path.join(root, relative)))
        if entry is None:
            print("SKIP %s (not in compile_commands.json)" % relative)
            skipped += 1
            continue
        state, detail = check_tu(relative, entry, extra, moc, tempdir, root, substitutions)
        if state == "skip":
            print("SKIP %s (%s)" % (relative, detail))
            skipped += 1
            continue
        checked += 1
        if state == "pass":
            print("PASS %s" % relative)
            continue
        failures.append(relative)
        print("FAIL %s: date accessor without <QDateTime>: %s" % (relative, detail))
    return failures, checked, skipped


def report(failures, checked, skipped):
    print("checked %d translation unit(s), skipped %d" % (checked, skipped))
    if failures:
        print("FAIL: %d translation unit(s) use a QFileInfo date accessor without <QDateTime>"
              % len(failures))
        return 1
    if checked == 0:
        print("FAIL: no translation unit could be checked on this host")
        return 2
    print("PASS: every checked QFileInfo date accessor has <QDateTime> in scope")
    return 0


def qt5_include_root(requested):
    """The Qt 5 include root to sweep through: --qt5-root, QT5_INCLUDE_DIR, or pkg-config."""
    if requested:
        return os.path.normpath(os.path.abspath(requested))
    if os.environ.get("QT5_INCLUDE_DIR"):
        return os.path.normpath(os.path.abspath(os.environ["QT5_INCLUDE_DIR"]))
    if not shutil.which("pkg-config"):
        return None
    produced = run(["pkg-config", "--variable=includedir", "Qt5Core"], os.getcwd(), timeout=60)
    if produced.returncode != 0 or not produced.stdout.strip():
        return None
    return os.path.normpath(os.path.abspath(produced.stdout.strip()))


def substitutions_for(qtcore, qt5, entries):
    """Rewrite every recorded Qt 6 include root to its Qt 5 counterpart.

    A Qt 5 development tree is where the defect actually bites: nothing supplies
    QDateTime, so the BASELINE compile of a translation unit raises the CI's error and
    the classification above must fail it. Where one is installed, running the sweep
    through its real headers is the hosted configuration, not a shadow of it.
    """
    substitutions = [(qtcore, qt5)]
    for entry in entries:
        for arg in include_dirs(compile_flags(entry)):
            counterpart = substitute_paths([arg], ((qtcore, qt5),))[0]
            if counterpart != arg and os.path.isdir(counterpart) and (arg, counterpart) not in substitutions:
                substitutions.append((arg, counterpart))
    return tuple(substitutions)


def toolchains(qtcore, entries, requested):
    """[(root, substitutions)] to sweep; the host's own root first, a real Qt 5 second."""
    found = [(qtcore, ())]
    qt5 = qt5_include_root(requested)
    if qt5 is None:
        print("real Qt 5 tree: none on this host (the shadow is the only shape)")
        return found
    if qt5 == qtcore or not os.path.isfile(os.path.join(qt5, "QtCore", "qfileinfo.h")):
        if requested:
            print("FAIL: no QtCore/qfileinfo.h under " + qt5)
            return None
        print("real Qt 5 tree: none on this host (the shadow is the only shape)")
        return found
    print("real Qt 5 tree:", qt5)
    return found + [(qt5, substitutions_for(qtcore, qt5, entries))]


def sweep_toolchain(entries, by_source, qtcore, root, options, tempdir, substitutions=()):
    """(failures, checked, skipped) for one toolchain root, printing its verdicts."""
    extra, ok, detail = prepare_shape(qtcore, options.cxx, tempdir)
    if not ok:
        print("FAIL: " + detail)
        return ["<the failing header shape does not reproduce>"], 0, 0
    return sweep(root, by_source, extra, options.moc, tempdir, substitutions)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    parser.add_argument("--cxx", default=os.environ.get("CXX") or "c++")
    parser.add_argument("--moc", default=os.environ.get("MOC") or "")
    parser.add_argument("--qt5-root", default="",
                        help="a real Qt 5 include root to build the recorded commands against")
    options = parser.parse_args()

    if not self_test():
        return 2

    load, error = load_build(os.path.abspath(options.build_dir))
    if error:
        print("FAIL: " + error)
        return 2
    entries, by_source, qtcore = load
    root = os.path.abspath(options.root)
    print("Qt root:", qtcore)

    sweeps = toolchains(qtcore, entries, options.qt5_root)
    if sweeps is None:
        return 2

    failures, checked, skipped = [], 0, 0
    with tempfile.TemporaryDirectory(prefix="zene-qfiledate-") as tempdir:
        for toolchain, substitutions in sweeps:
            print("sweep against:", toolchain,
                  "(real Qt 5, substituted)" if substitutions else "")
            found, seen, missed = sweep_toolchain(entries, by_source, toolchain, root,
                                                  options, tempdir, substitutions)
            failures += found
            checked += seen
            skipped += missed
    return report(failures, checked, skipped)


if __name__ == "__main__":
    sys.exit(main())
