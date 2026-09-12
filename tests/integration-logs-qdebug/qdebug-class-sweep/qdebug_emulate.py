#!/usr/bin/env python3
"""qdebug_emulate.py -- the Qt5 emulation used to sweep the whole tree for the
`invalid use of incomplete type 'class QDebug'` class (v0.2.0-alpha CI failure).

Why an emulation, and why it is sound in one direction
-----------------------------------------------------
<QtGlobal> forward-declares QDebug and defines the qDebug()/qWarning()/... macros;
only <QDebug> *defines* the class.  So `qWarning() << x;` compiles only if some
other header in the translation unit's include set drags qdebug.h in.

Qt 6.4's <QCoreApplication>/<QVariant>/<QLoggingCategory>/<QCborCommon> and
QtGui's <QAccessible>/<QGenericMatrix>/<QVulkanInstance> all `#include
<QtCore/qdebug.h>`.  Qt 5.15's counterparts do not (checked against the 5.15
branch: neither qcoreapplication.h nor qvariant.h includes it).  That single
difference is the whole bug class, and it is why the CI's Qt5 jobs die while a
Qt6-only box builds the same tree.

This script models the Qt5 condition by compiling each translation unit with the
*real* command line from build/compile_commands.json plus a shadow include
directory that holds copies of those seven Qt6 headers with their
`#include <QtCore/qdebug.h>` line removed.  The shadow directory comes first in
the include order, so any *.cpp that reached QDebug only through one of those
seven headers now fails exactly where CI fails.

  rc = 0 under emulation  -> safe: the TU still compiles with the qdebug.h paths
                             Qt5 does not have, so it reaches QDebug (or does not
                             use it) through something Qt5 has too.
  rc != 0 under emulation -> the TU depends on a Qt6-only path; it is expected to
                             fail on the CI's Qt5 jobs.  The fix convention this
                             repo already uses (MainWindow.cpp 7cd9da2b1,
                             ConfigManager.cpp) is to add `#include <QDebug>` to
                             the file that needs it.

The emulation is deliberately one-directional: a stripped header can only remove
a path, never add one, so rc=0 is evidence of Qt5-safety while rc!=0 is a
candidate that needs the include (or, if the file is byte-identical to upstream
and CI was green on it, an over-strict model -- see docs/QDEBUG-CLASS-AND-MIME-RENAME.md).

Self-test (--selftest): a copy of a file known to stream to QDebug, with its
`#include <QDebug>` line deleted, must fail with the CI's exact error text under
the shadow.  If the self-test does not fail, the shadow is not doing its job and
every other verdict in the run is worthless.

Usage:
  python3 qdebug_emulate.py --shadow <dir> [--build-dir build] [file ...]
  python3 qdebug_emulate.py --shadow <dir> --all      # every TU that streams
  python3 qdebug_emulate.py --make-shadow <dir>       # build the shadow dir
  python3 qdebug_emulate.py --selftest --shadow <dir> --build-dir build
"""

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys

QT_INCLUDE_ROOTS = [
    "/usr/include/x86_64-linux-gnu/qt6",
    "/usr/include/aarch64-linux-gnu/qt6",
    "/usr/include/qt6",
]

# The Qt6 headers that add qdebug.h and whose Qt5 counterparts do not.  Deleting
# the include from these is what turns Qt6 into Qt5 for this purpose.
STRIP = [
    "QtCore/qcoreapplication.h",
    "QtCore/qvariant.h",
    "QtCore/qcborcommon.h",
    "QtCore/qloggingcategory.h",
    "QtGui/qaccessible.h",
    "QtGui/qgenericmatrix.h",
    "QtGui/qvulkaninstance.h",
]

STREAM_RE = re.compile(r"\bq(Debug|Warning|Critical|Info|Fatal)\s*\(\s*\)\s*<<|"
                       r"\bQDebug\b|QDebugStateSaver")

# The COMPLETE scope of this defect class: a translation unit can fail on the missing
# definition only if it mentions a q*() logging function or the QDebug type at all.
# Checked over every TU in compile_commands.json (1393 distinct sources here); every
# TU outside this set is textually incapable of the error, so sweeping the set below
# is a whole-tree sweep of the class, not a sample.
#
# Note (measured, not assumed): the `(...)` call form -- `qDebug("x %d", 1);` -- does
# NOT need the complete type.  `printf`-style: compiled by gcc 13 with only <QtGlobal>,
# exit 0.  Only `q*() << x` (and explicit QDebug member use) is the failure class.
CANDIDATE_RE = re.compile(r"\bq(Debug|Warning|Critical|Info|Fatal)\b|\bQDebug\b|"
                          r"QDebugStateSaver")


def qt_root():
    for r in QT_INCLUDE_ROOTS:
        if os.path.isdir(os.path.join(r, "QtCore")):
            return r
    sys.exit("error: no Qt6 include root found (looked in %s)" % ", ".join(QT_INCLUDE_ROOTS))


def make_shadow(shadow):
    root = qt_root()
    if os.path.isdir(shadow):
        shutil.rmtree(shadow)
    stripped = 0
    for rel in STRIP:
        src = os.path.join(root, rel)
        if not os.path.isfile(src):
            print("warning: %s not present; shadow is incomplete for it" % src)
            continue
        dst = os.path.join(shadow, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(src, errors="replace") as fh:
            text = fh.read()
        new = re.sub(r"^[ \t]*#\s*include\s*<QtCore/qdebug\.h>[^\n]*\n", "", text,
                     flags=re.M)
        if new == text:
            print("warning: no qdebug.h include found in %s" % src)
        with open(dst, "w") as fh:
            fh.write(new)
        stripped += 1
    print("shadow: %s (%d header(s) stripped of <QtCore/qdebug.h>; Qt6 root %s)"
          % (shadow, stripped, root))
    return stripped


def load_commands(build_dir):
    path = os.path.join(build_dir, "compile_commands.json")
    if not os.path.isfile(path):
        sys.exit("error: %s not found (configure with CMAKE_EXPORT_COMPILE_COMMANDS)"
                 % path)
    with open(path) as fh:
        return json.load(fh)


def run_entry(entry, shadow, timeout=300, src_override=None):
    """Compile one translation unit with -fsyntax-only, shadow first on the -I path."""
    argv = shlex.split(entry["command"])
    # Map the -o output away and switch to a syntax-only compile.
    out, skip = [], False
    for tok in argv:
        if skip:
            skip = False
            continue
        if tok == "-o":
            skip = True
            continue
        if tok == "-c":
            continue
        out.append(tok)
    if src_override:
        out = [src_override if tok == entry["file"] else tok for tok in out]
    # The shadow dir must be absolute: the entry's cwd is a build subdirectory.
    out.insert(1, "-I%s" % os.path.abspath(shadow))
    out.append("-fsyntax-only")
    try:
        proc = subprocess.run(out, capture_output=True, text=True,
                              cwd=entry["directory"], timeout=timeout)
    except subprocess.TimeoutExpired:
        return 124, "TIMEOUT after %ss" % timeout
    msg = ""
    for line in (proc.stderr or "").splitlines():
        if "error:" in line:
            msg = line.strip()
            break
    return proc.returncode, msg


def pick(entries, want, build_dir):
    """Entries whose source file matches one of the wanted repo-relative suffixes."""
    out = []
    for e in entries:
        f = e["file"]
        try:
            rel = os.path.relpath(f, os.path.dirname(os.path.abspath(build_dir)))
        except ValueError:
            rel = f
        rel = rel.replace(os.sep, "/")
        for w in want:
            if rel.endswith("/" + w) or rel == w:
                out.append((w, e))
                break
    return out


def all_streaming(entries, root):
    """Every TU in the build inside the complete scope of the defect class (CANDIDATE_RE)."""
    out, seen = [], set()
    for e in entries:
        f = e["file"]
        if f in seen or not os.path.isfile(f):
            continue
        seen.add(f)
        with open(f, errors="replace") as fh:
            text = fh.read()
        text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
        text = re.sub(r"//[^\n]*", " ", text)
        if CANDIDATE_RE.search(text):
            out.append(e)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shadow", help="shadow include directory")
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--make-shadow", metavar="DIR", nargs="?", const="",
                    help="(re)build the shadow directory and exit")
    ap.add_argument("--all", action="store_true",
                    help="sweep every TU in the build that streams to QDebug")
    ap.add_argument("--selftest", action="store_true",
                    help="prove the emulation reproduces the CI error")
    ap.add_argument("--selftest-src", default="src/core/ConfigManager.cpp")
    ap.add_argument("files", nargs="*")
    args = ap.parse_args()

    if args.make_shadow is not None:
        make_shadow(args.make_shadow or args.shadow)
        return 0

    if not args.shadow:
        sys.exit("error: --shadow is required")
    if not os.path.isdir(os.path.join(args.shadow, "QtCore")):
        sys.exit("error: %s is not a shadow dir; run --make-shadow first" % args.shadow)

    entries = load_commands(args.build_dir)

    if args.selftest:
        return selftest(args, entries)

    if args.all:
        todo = [("--all", e) for e in all_streaming(entries, args.build_dir)]
    elif args.files:
        todo = pick(entries, args.files, args.build_dir)
        found = {w for w, _ in todo}
        for w in args.files:
            if w not in found:
                print("%-56s %s" % (w, "SKIP (not in compile_commands.json)"))
    else:
        sys.exit("error: name files, or pass --all / --selftest")

    fails = []
    others = []
    for _, e in todo:
        rel = os.path.relpath(e["file"], os.path.dirname(os.path.abspath(args.build_dir)))
        rc, msg = run_entry(e, args.shadow)
        if rc == 0:
            print("%-58s rc=0   SAFE under Qt5 emulation" % rel)
        elif "incomplete type" in msg and "QDebug" in msg:
            print("%-58s rc=%-3d FAILS under Qt5 emulation: %s" % (rel, rc, msg))
            fails.append(rel)
        else:
            # A build-tree artefact (a generated ui_*.h that no build has produced yet),
            # not a QDebug verdict.  Reported, never counted as a pass.
            print("%-58s rc=%-3d NOT A QDEBUG VERDICT (build-tree artefact or unrelated error): %s"
                  % (rel, rc, msg))
            others.append(rel)

    print()
    print("summary: %d TU(s) swept, %d fail under the Qt5 emulation, %d inconclusive"
          % (len(todo), len(fails), len(others)))
    for f in fails:
        print("  NEEDS <QDebug>: %s" % f)
    for f in others:
        print("  INCONCLUSIVE (not a QDebug verdict): %s" % f)
    return 1 if fails else 0


def selftest(args, entries):
    """Delete the #include <QDebug> line from a COPY of a known-good file and compile it.

    The copy lives in <build-dir>/qdebug-selftest/ and is compiled through the original
    translation unit's own command line (source token substituted), so no tracked
    source file is ever modified by this script.
    """
    src = os.path.join(os.path.dirname(os.path.abspath(args.build_dir)), args.selftest_src)
    if not os.path.isfile(src):
        sys.exit("error: selftest source not found: %s" % src)
    with open(src, errors="replace") as fh:
        text = fh.read()
    if "#include <QDebug>" not in text:
        sys.exit("error: %s carries no '#include <QDebug>' to delete" % src)
    scratch = os.path.join(os.path.abspath(args.build_dir), "qdebug-selftest")
    os.makedirs(scratch, exist_ok=True)
    variants = {
        "as-is": text,
        "no-qdebug": text.replace("#include <QDebug>\n", "", 1),
    }
    match = None
    for _, e in pick(entries, [args.selftest_src], args.build_dir):
        match = e
    if match is None:
        sys.exit("error: %s is not in %s" % (args.selftest_src, args.build_dir))
    rc_all = {}
    for name, body in variants.items():
        copy = os.path.join(scratch, "%s-%s" % (name, os.path.basename(src)))
        with open(copy, "w") as fh:
            fh.write(body)
        rc, msg = run_entry(match, args.shadow, src_override=copy)
        rc_all[name] = (rc, msg)
        print("SELFTEST %-10s rc=%-3d %s" % (name, rc, msg))
    ok = True
    if rc_all["as-is"][0] != 0:
        print("FAIL: the file does not compile even with <QDebug> present")
        ok = False
    if rc_all["no-qdebug"][0] == 0:
        print("FAIL: deleting the include still compiles -- the shadow is not modelling Qt5")
        ok = False
    elif "incomplete type" not in rc_all["no-qdebug"][1] or "QDebug" not in rc_all["no-qdebug"][1]:
        print("FAIL: wrong error text: %s" % rc_all["no-qdebug"][1])
        ok = False
    print()
    print("SELFTEST %s: the shadow reproduces the CI error (one include is the difference)"
          % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
