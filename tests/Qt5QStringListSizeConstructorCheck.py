#!/usr/bin/env python3
"""Fail when the sends-on-faders assertion uses the Qt 6-only QStringList(size, value) constructor.

Cluster ci-8764ffa3b495466c, hosted `build / windows-arm64` (and the linux/macos Qt 5 jobs) at
fa4a5f2e785c73e32216931e33cdbe140c06647d:

    tests/src/gui/MixerSendsOnFadersTest.cpp:278:
        QVERIFY(faders == QStringList(channels, QStringLiteral("volume")));

Qt 6's QStringList inherits QList<QString>'s constructors, including
`QList(qsizetype size, parameter_type t)`, so the line builds a list of `channels` copies of
"volume" on the Qt 6 hosts. Qt 5.15's QStringList does not inherit that constructor: the only
two-argument candidate left is the iterator-range template, and deduction of `InputIterator`
fails because the first argument is `int` and the second a `QString`:

    error: no matching constructor for initialization of 'QStringList'

Qt 5 then selects the iterator-pair constructor with conflicting deduced types, so only the
first argument needs to be an integer for the trap. The portable spelling appends `channels`
copies of "volume" to an explicitly declared QStringList, which both Qt 5 and Qt 6 accept.

The check derives its fixture from the source statement that compares the answer with the
expected list, so it tests what the test really contains:

  * the derived statement is compiled with the Qt 5 headers - the hosted compiler input.
  * a control statement with the Qt 6-only constructor must be rejected by the same Qt 5 tree;
    a tree that accepts it has no trap and the probe reports itself skipped, never passed.
  * when a Qt 6 tree is installed the derived statement must also compile there.

Usage:
  Qt5QStringListSizeConstructorCheck.py [--root DIR] [--cxx CXX]
  Qt5QStringListSizeConstructorCheck.py --self-test

Exit codes: 0 = clean (or skipped on a host without Qt 5), 1 = finding or failed fixture,
2 = usage error.
"""

import argparse
import os
import shlex
import shutil
import subprocess
import sys
import tempfile

TEST_SOURCE = "tests/src/gui/MixerSendsOnFadersTest.cpp"

QT5_ONLY_FIXTURE = """\
#include <QtTest>
#include <QStringList>

void probe(QStringList faders, int channels)
{
    QStringList expected(channels, QStringLiteral("volume"));
    QVERIFY(faders == expected);
}
"""


def run(command, cwd=None, timeout=300):
    try:
        return subprocess.run(command, cwd=cwd, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.SubprocessError):
        return None


def module_flags(modules):
    """The compiler flags of one Qt development tree, or None when pkg-config has none."""
    if not shutil.which("pkg-config"):
        return None
    for packages in (modules, modules[:1]):
        result = run(["pkg-config", "--cflags", *packages])
        if result is not None and result.returncode == 0:
            return shlex.split(result.stdout)
    return None


def derived_statement(source, relative):
    """The source's comparison against the expected list, plus the lines that build it."""
    lines = source.splitlines()
    matches = [index for index, line in enumerate(lines) if "QVERIFY(faders ==" in line]
    if len(matches) != 1:
        raise RuntimeError(f"{relative}: expected one 'QVERIFY(faders ==' statement, found {len(matches)}")
    verify = matches[0]
    # The comparison's expected list is declared just above it; a declaration of a different
    # QStringList (the faders answer itself) is not the start of the block. Without one the
    # comparison is self-contained, which is the Qt 6-only form this check rejects.
    start = verify
    for index in range(verify - 1, max(verify - 6, -1), -1):
        stripped = lines[index].strip()
        if (stripped.startswith("QStringList ") and stripped.endswith(";")
                and "=" not in stripped and "faders" not in stripped):
            start = index
            break
    return "\n".join(lines[start:verify + 1])


def fixture(statement):
    return ("#include <QtTest>\n#include <QStringList>\n\n"
            "void probe(QStringList faders, int channels)\n{\n"
            f"{statement}\n"
            "    (void)channels;\n}\n")


def compile_fixture(cxx, flags, workdir, name, text):
    path = os.path.join(workdir, name)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    return run([cxx, "-std=gnu++20", "-fPIC", "-fsyntax-only", *flags, path])


def diagnostics(result):
    return "\n".join(line for line in (result.stdout + result.stderr).splitlines()
                     if "error:" in line or "note:" in line)[:3000]


def compile_statement(cxx, flags, workdir, name, statement):
    return compile_fixture(cxx, flags, workdir, name, fixture(statement))


def qt5_control_verdict(cxx, flags, workdir):
    """True when this Qt 5 tree has the trap; False when it accepts the Qt 6-only form."""
    control = compile_fixture(cxx, flags, workdir, "qt6-only.cpp", QT5_ONLY_FIXTURE)
    if control is None or control.returncode == 0:
        return False
    # The control is a whole translation unit, so its only error must be the missing constructor.
    if "QStringList" not in diagnostics(control) or "function-definition" in control.stderr:
        raise RuntimeError(f"the Qt 5 control failed for another reason\n{diagnostics(control)}")
    return True


def require_qt5_derived(cxx, flags, workdir, statement):
    result = compile_statement(cxx, flags, workdir, "derived.cpp", statement)
    if result is None:
        raise RuntimeError(f"{TEST_SOURCE}: the Qt 5 compile probe could not run")
    if result.returncode != 0:
        raise RuntimeError(f"{TEST_SOURCE}: Qt 5 rejected the faders/volume comparison\n"
                           f"{statement}\n{diagnostics(result)}")
    print(f"PASS: {TEST_SOURCE} builds the expected volume list with Qt 5")


def require_qt6_derived(cxx, statement, workdir):
    flags = module_flags(["Qt6Test", "Qt6Core"])
    if flags is None:
        return
    same = compile_statement(cxx, flags, workdir, "derived-qt6.cpp", statement)
    if same is None or same.returncode != 0:
        detail = diagnostics(same) if same is not None else ""
        raise RuntimeError(f"the derived statement does not compile with Qt 6\n{detail}")
    print("PASS: the same statement compiles with Qt 6")


def check_product(cxx, root):
    source_path = os.path.join(root, TEST_SOURCE)
    with open(source_path, encoding="utf-8") as handle:
        statement = derived_statement(handle.read(), TEST_SOURCE)
    flags = module_flags(["Qt5Test", "Qt5Core"])
    if flags is None:
        print("probe: skipped (no Qt 5 development tree found)")
        return 0
    with tempfile.TemporaryDirectory(prefix="qt5-qstringlist-size-") as workdir:
        try:
            if not qt5_control_verdict(cxx, flags, workdir):
                print("probe: skipped (this Qt tree accepts the Qt 6-only size constructor)")
                return 0
            print("probe: Qt 5 rejects QStringList(size, value) and accepts the derived statement")
            require_qt5_derived(cxx, flags, workdir, statement)
            require_qt6_derived(cxx, statement, workdir)
        except RuntimeError as error:
            print(error)
            print("FAIL: build the expected list with append() instead of QStringList(size, value)")
            return 1
    return 0


def self_test(cxx):
    flags = module_flags(["Qt5Test", "Qt5Core"])
    if flags is None:
        print("self-test: skipped (no Qt 5 development tree found)")
        return 0
    with tempfile.TemporaryDirectory(prefix="qt5-qstringlist-size-fixtures-") as workdir:
        control = compile_fixture(cxx, flags, workdir, "qt6-only.cpp", QT5_ONLY_FIXTURE)
        good = compile_statement(cxx, flags, workdir, "fixed.cpp",
                                 "    QStringList expected;\n"
                                 "    for (int index = 0; index < channels; ++index)\n"
                                 "    {\n"
                                 "        expected.append(QStringLiteral(\"volume\"));\n"
                                 "    }\n"
                                 "    QVERIFY(faders == expected);")
    if control is None or control.returncode == 0:
        print("SELF-TEST FAIL: the Qt 5 tree accepts the Qt 6-only size constructor")
        return 1
    if good is None or good.returncode != 0:
        print("SELF-TEST FAIL: the Qt 5 tree rejects the append-built expected list")
        if good is not None:
            print(diagnostics(good))
        return 1
    print("self-test: Qt 5 rejects the size constructor and accepts the append-built expected list")
    return 0


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    parser.add_argument("--cxx", default=os.environ.get("CXX") or shutil.which("c++") or "c++")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv[1:])
    if args.self_test:
        return self_test(args.cxx)
    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print(f"error: not a directory: {root}", file=sys.stderr)
        return 2
    return check_product(args.cxx, root)


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except (OSError, RuntimeError) as error:
        print(f"Qt 5 QStringList size-constructor check: {error}", file=sys.stderr)
        sys.exit(1)
