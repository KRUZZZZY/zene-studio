#!/usr/bin/env python3
"""The zene::api boundary's proof (ARCH-2, board card #672).

THE CLAIM UNDER TEST, in one sentence: every translation unit of the control
registry's boundary target (`zene_api`) compiles with NO Qt widget include
reachable, and no object of that target references a Qt widget symbol either.

WHY IT IS A REGISTERED CTEST. The boundary target itself makes a widget include
in the registry a COMPILE ERROR (its include path has no QtWidgets directory and
it does not link Qt6::Widgets), so a regression fails the build step of every CI
job. What the build cannot see is a change that *restores* widget reachability
without adding an include - e.g. linking Qt6::Widgets to the boundary target
again, or writing the module-qualified form (`<QtWidgets/qwidget.h>`) that
resolves through the Qt parent directory Qt's own headers require. This test
re-measures both, on the artifacts the build just produced, and carries a
LIVENESS CONTROL for every check: the same measurement is run against a GUI
translation unit of the object library (`src/gui/MainWindow.cpp`), which MUST
show widget reachability. A check that cannot see widgets in the control is not
a check, and this test fails rather than passing vacuously.

Checks (each prints its own measurement):

  1. target membership - every source in the boundary list is compiled by the
     `zene_api` target exactly once (its recorded object path is under the
     target's object directory, the object exists, and no other source's object
     is in there).
  2. no widget include path - none of the boundary compile commands carries a
     QtWidgets include argument; the control's command must carry one.
  3. headless compile probe - the exact recorded flags of a boundary TU compile
     a probe that includes the boundary's public headers (`zene/api/ControlApi.h`
     and the registry headers); it must SUCCEED.
  4. widget-include negative controls - the same flags must FAIL to compile
     `#include <QWidget>` and `#include <QApplication>`. If either compiles, the
     widget include directory is reachable and the boundary is broken.
  5. include closure - preprocessing every boundary TU with its recorded flags
     reaches zero QtWidgets headers; the control reaches at least one.
  6. object symbols - no boundary object has an undefined Qt widget symbol; the
     control object has many.

PORTABILITY. Checks 1-4 run on any toolchain (syntax-only is `-fsyntax-only` for
GCC/Clang, `/Zs` for MSVC). Checks 5 and 6 need a GCC-compatible preprocessor
(`-M`) and `nm`; on MSVC they are skipped with the reason printed, because the
build-level gate (the target's include path) applies there regardless.

Usage:
    python3 zene-api-boundary.py --build-dir <build> --object-dir <build>/src/CMakeFiles/zene_api.dir \
        --source-list <build>/zene-api-sources.txt \
        --control-source <repo>/src/gui/MainWindow.cpp \
        --control-object <build>/src/CMakeFiles/lmmsobjs.dir/gui/MainWindow.cpp.o

Exit codes: 0 every check passed; 1 a check failed; 2 setup error (a required
input is missing - a test that cannot look must never pass).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

#: Symbols that only a Qt widget translation unit can reference. Deliberately
#: broad - a false positive on a boundary object is itself worth stopping for.
WIDGET_SYMBOL_RE = re.compile(
    r"QWidget|QApplication|QMenu|QToolBar|QToolButton|QMenuBar|QDialog|QMainWindow"
    r"|QFrame|QRubberBand|QAbstractButton|QAbstractScrollArea|QTabWidget|QMdiArea"
    r"|QScrollArea|QSizePolicy|QLayout")
#: The include-path text every QtWidgets header lives under.
WIDGET_INCLUDE_RE = re.compile(r"QtWidgets")

PROBE_HEADERS = r'''
#include "zene/api/ControlApi.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlUndoCoalescing.h"
#include "ControlVocabulary.h"
int main() { return 0; }
'''

PROBE_WIDGET = r'''
#include <QWidget>
int main() { return 0; }
'''

PROBE_APPLICATION = r'''
#include <QApplication>
int main() { return 0; }
'''

INCLUDE_FLAGS = ("-I", "-isystem", "-iquote", "/I", "-external:I", "/external:I")


class Failure(Exception):
    pass


def run(args, cwd, timeout=300):
    return subprocess.run(args, cwd=cwd, capture_output=True, text=True, timeout=timeout)


def entry_arguments(entry):
    """The recorded command as an argument list (compiler at index 0)."""
    command = entry.get("command")
    if command:
        return shlex.split(command)
    return list(entry.get("arguments") or [])


def is_msvc(argv0):
    return os.path.basename(argv0).lower().startswith("cl")


def split_output(args):
    """(arguments without the -o/-Fo output argument, the output path or None)."""
    out = []
    output = None
    index = 0
    while index < len(args):
        arg = args[index]
        if arg == "-o":
            output = args[index + 1] if index + 1 < len(args) else None
            index += 2
            continue
        if arg.startswith("/Fo"):
            output = arg[3:] or (args[index + 1] if index + 1 < len(args) else None)
            index += 1 if arg[3:] else 2
            continue
        out.append(arg)
        index += 1
    return out, output


def compile_flags(args):
    """A compile line's flags: no output argument, no compile action."""
    kept, _ = split_output(args)
    return [a for a in kept if a not in ("-c", "/c")]


def include_arguments(args):
    """The include-search arguments of a compile line, both toolchain forms."""
    out = []
    index = 0
    while index < len(args):
        arg = args[index]
        if arg in INCLUDE_FLAGS and index + 1 < len(args):
            out.append(args[index + 1])
            index += 2
            continue
        if arg.startswith(("/I", "-I", "/external:I", "-external:I")):
            value = arg.split(":", 1)[1] if arg.lower().startswith(("/external:i", "-external:i")) else arg[2:]
            if value and value[:1] not in ("-", "/"):
                out.append(value)
            index += 1
            continue
        index += 1
    return out


def resolved_object_path(entry, output):
    if not output:
        return None
    if not os.path.isabs(output):
        output = os.path.join(entry["directory"], output)
    return os.path.normpath(output)


def under(directory, path):
    """True when path is inside directory (case-insensitive on Windows)."""
    directory = os.path.normcase(os.path.normpath(directory))
    path = os.path.normcase(os.path.normpath(path))
    try:
        return os.path.commonpath([directory, path]) == directory
    except ValueError:            # different drives / mixed absolute-relative
        return False


def load_entries(build_dir):
    path = Path(build_dir) / "compile_commands.json"
    if not path.is_file():
        raise Failure("no compile_commands.json in %s (CMAKE_EXPORT_COMPILE_COMMANDS must be ON)" % build_dir)
    with open(path) as handle:
        entries = json.load(handle)
    if not entries:
        raise Failure("compile_commands.json is empty")
    return entries


def read_source_list(path):
    file = Path(path)
    if not file.is_file():
        raise Failure("the boundary source list %s does not exist" % path)
    wanted = [line.strip() for line in file.read_text().splitlines()
              if line.strip() and not line.strip().startswith("#")]
    if not wanted:
        raise Failure("the boundary source list is empty")
    return wanted


# --- check 1: target membership -------------------------------------------


def boundary_member(source, by_file, object_dir):
    """(entry, object_path, problem) for one boundary source."""
    matches = by_file.get(os.path.normpath(source)) or []
    if len(matches) != 1:
        return None, None, "%s is compiled by %d targets (expected exactly the zene_api target)" \
                           % (source, len(matches))
    entry = matches[0]
    _, output = split_output(entry_arguments(entry))
    object_path = resolved_object_path(entry, output)
    if object_path is None:
        return None, None, "%s: no object path in its compile command" % source
    if not under(object_dir, object_path):
        return None, None, "%s is compiled OUTSIDE the boundary target (object %s)" % (source, object_path)
    if not os.path.isfile(object_path):
        return None, None, "%s: the object %s does not exist" % (source, object_path)
    return entry, object_path, None


def unexpected_boundary_sources(entries, object_dir, boundary):
    problems = []
    for entry in entries:
        _, output = split_output(entry_arguments(entry))
        object_path = resolved_object_path(entry, output)
        if object_path and under(object_dir, object_path) and os.path.normpath(entry["file"]) not in boundary:
            problems.append("the boundary target compiles an unexpected source: %s" % entry["file"])
    return problems


def check_target_membership(wanted, entries, object_dir):
    by_file = {}
    for entry in entries:
        by_file.setdefault(os.path.normpath(entry["file"]), []).append(entry)
    boundary = {}
    problems = []
    for source in wanted:
        entry, object_path, problem = boundary_member(source, by_file, object_dir)
        if problem:
            problems.append(problem)
        else:
            boundary[os.path.normpath(source)] = (entry, object_path)
    problems += unexpected_boundary_sources(entries, object_dir, boundary)
    summary = "target membership: %d/%d boundary sources compile in %s" % (len(boundary), len(wanted), object_dir)
    return boundary, problems, summary


# --- check 2: the include path --------------------------------------------


def widget_include_dirs(entry):
    return [d for d in include_arguments(entry_arguments(entry)) if WIDGET_INCLUDE_RE.search(d)]


def check_include_paths(boundary, control_entries, control_source):
    problems = []
    control_dirs = widget_include_dirs(control_entries[0]) if control_entries else []
    if not control_dirs:
        problems.append("the control (%s) shows no QtWidgets include path - check 2 would pass vacuously; "
                        "is it still a GUI translation unit of the object library?" % control_source)
    on_boundary = []
    for source, (entry, _) in sorted(boundary.items()):
        for directory in widget_include_dirs(entry):
            on_boundary.append((os.path.relpath(source), directory))
    for source, directory in on_boundary:
        problems.append("%s compiles with a QtWidgets include path: %s" % (source, directory))
    summary = ("no widget include path: %d boundary compile lines carry a QtWidgets include "
               "(liveness: the control carries %d)" % (len(on_boundary), len(control_dirs)))
    return problems, summary


# --- checks 3 and 4: the compile probes -----------------------------------


def compile_probe(text, flags, cwd, name):
    handle = tempfile.NamedTemporaryFile("w", suffix=".cpp", prefix=name + "-", delete=False)
    handle.write(text)
    handle.close()
    try:
        return run(flags + [handle.name], cwd=cwd)
    finally:
        os.unlink(handle.name)


def probe_flags(boundary):
    """The flags half of the alphabetically first boundary TU, syntax-only added."""
    probe_source = sorted(boundary)[0]
    entry, _ = boundary[probe_source]
    args = entry_arguments(entry)
    syntax_only = "/Zs" if is_msvc(args[0]) else "-fsyntax-only"
    flags = [a for a in compile_flags(args) if os.path.normpath(a) != os.path.normpath(probe_source)]
    return probe_source, entry["directory"], flags + [syntax_only], syntax_only


def widget_probe_problems(flags, workdir):
    problems = []
    for name, text in (("<QWidget>", PROBE_WIDGET), ("<QApplication>", PROBE_APPLICATION)):
        if compile_probe(text, flags, workdir, "widget").returncode == 0:
            problems.append("`#include %s` COMPILES with the boundary's flags: the widget include "
                            "directory is reachable and the boundary is broken" % name)
    return problems


def check_compile_probes(boundary):
    problems = []
    probe_source, workdir, flags, syntax_only = probe_flags(boundary)
    good = compile_probe(PROBE_HEADERS, flags, workdir, "boundary")
    if good.returncode != 0:
        problems.append("the boundary's public headers do not compile with the boundary's own flags:\n%s"
                        % (good.stderr or good.stdout)[-1500:])
    results = ["headless compile probe: %s (flags of %s, %s)"
               % ("OK" if good.returncode == 0 else "FAILED", os.path.relpath(probe_source), syntax_only)]
    problems += widget_probe_problems(flags, workdir)
    results.append("widget-include negative controls: `#include <QWidget>` and `#include <QApplication>` "
                   "fail to compile as required" if not problems[-2:]
                   else "widget-include negative control FAILED (see above)")
    return problems, results


# --- check 5: the include closure -----------------------------------------


def closure_of(job):
    """(source, reached_line, error) for one boundary TU's preprocessing."""
    boundary, source = job
    entry, _ = boundary[source]
    result = run(compile_flags(entry_arguments(entry)) + ["-M"], cwd=entry["directory"])
    if result.returncode != 0:
        return source, None, "preprocessing %s failed:\n%s" % (os.path.relpath(source),
                                                               (result.stderr or "")[-600:])
    for line in result.stdout.splitlines():
        if WIDGET_INCLUDE_RE.search(line):
            return source, line.strip(), None
    return source, None, None


def check_include_closure(boundary, control_entries):
    problems = []
    reached = []
    with ThreadPoolExecutor(max_workers=min(4, os.cpu_count() or 2)) as pool:
        jobs = [(boundary, source) for source in sorted(boundary)]
        for source, line, error in pool.map(closure_of, jobs):
            if error:
                problems.append(error)
            elif line:
                reached.append(os.path.relpath(source))
    for source in reached:
        problems.append("%s reaches a QtWidgets header in its include closure" % source)
    control = run(compile_flags(entry_arguments(control_entries[0])) + ["-M"],
                  cwd=control_entries[0]["directory"])
    control_reached = sum(1 for line in control.stdout.splitlines() if WIDGET_INCLUDE_RE.search(line))
    if control_reached == 0:
        problems.append("the control's include closure reaches no QtWidgets header - check 5 would pass "
                        "vacuously")
    summary = ("include closure: %d/%d boundary TUs reach a QtWidgets header (liveness: the control "
               "reaches %d)" % (len(reached), len(boundary), control_reached))
    return problems, summary


# --- check 6: the object symbols ------------------------------------------


def object_widget_symbol(path):
    """(path, symbol or None, error) for one object file."""
    result = run(["nm", "--undefined-only", "--format=posix", path], cwd=os.path.dirname(path))
    if result.returncode != 0:
        return path, None, "nm failed on %s: %s" % (path, (result.stderr or "")[-300:])
    for line in result.stdout.splitlines():
        symbol = line.split(" ")[0]
        if WIDGET_SYMBOL_RE.search(symbol):
            return path, symbol, None
    return path, None, None


def widget_symbol_count(path):
    result = run(["nm", "--undefined-only", "--format=posix", path], cwd=os.path.dirname(path))
    return sum(1 for line in result.stdout.splitlines() if WIDGET_SYMBOL_RE.search(line.split(" ")[0]))


def check_object_symbols(boundary, control_object):
    problems = []
    hits = []
    with ThreadPoolExecutor(max_workers=min(4, os.cpu_count() or 2)) as pool:
        for path, symbol, error in pool.map(object_widget_symbol, sorted(p for _, p in boundary.values())):
            if error:
                problems.append(error)
            elif symbol:
                hits.append((os.path.relpath(path), symbol))
    for path, symbol in hits:
        problems.append("%s references a Qt widget symbol: %s" % (path, symbol))
    control_hits = widget_symbol_count(control_object) if os.path.isfile(control_object) else 0
    if control_hits == 0:
        problems.append("the control object %s shows no widget symbol - check 6 would pass vacuously"
                        % control_object)
    summary = ("object symbols: %d/%d boundary objects reference a Qt widget symbol (liveness: the "
               "control references %d)" % (len(hits), len(boundary), control_hits))
    return problems, summary


# --- orchestration ---------------------------------------------------------


def boundary_msvc(boundary):
    return is_msvc(entry_arguments(next(iter(boundary.values()))[0])[0]) if boundary else False


def control_object_path(options):
    path = options.control_object
    if not os.path.isfile(path):
        path = os.path.join(options.build_dir, os.path.relpath(path, options.build_dir))
    return path


def run_all(options):
    """(problems, output lines, notes, the boundary's size)."""
    wanted = read_source_list(options.source_list)
    entries = load_entries(options.build_dir)
    object_dir = os.path.normpath(options.object_dir)
    control_entries = [e for e in entries
                       if os.path.normpath(e["file"]) == os.path.normpath(options.control_source)]
    if not control_entries:
        raise Failure("the control source %s is not in compile_commands.json" % options.control_source)

    boundary, problems, summary = check_target_membership(wanted, entries, object_dir)
    lines = [summary]
    extra, summary = check_include_paths(boundary, control_entries, options.control_source)
    problems += extra
    lines.append(summary)
    extra, results = check_compile_probes(boundary)
    problems += extra
    lines += results
    notes = []
    if boundary_msvc(boundary):
        notes.append("MSVC toolchain: the include-closure measurement (-M) and the symbol scan (nm) are "
                     "not implemented here; the boundary target's include path is the gate on this job")
        lines += ["include closure: not run (MSVC)", "object symbols: not run (MSVC)"]
    else:
        extra, summary = check_include_closure(boundary, control_entries)
        problems += extra
        lines.append(summary)
        if shutil.which("nm"):
            extra, summary = check_object_symbols(boundary, control_object_path(options))
            problems += extra
            lines.append(summary)
        else:
            notes.append("nm not available: the object-symbol scan is not run on this job "
                         "(the include checks above are the enforcement)")
            lines.append("object symbols: not run (no nm)")
    return problems, lines, notes, len(boundary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--object-dir", required=True)
    parser.add_argument("--source-list", required=True)
    parser.add_argument("--control-source", required=True)
    parser.add_argument("--control-object", required=True)
    options = parser.parse_args()
    problems, lines, notes, count = run_all(options)
    for line in lines:
        print(line)
    for note in notes:
        print("note: %s" % note)
    print("")
    if problems:
        print("=== FAIL (zene::api boundary) ===")
        for problem in problems:
            print("  - %s" % problem)
        return 1
    print("=== PASS (zene::api boundary: %d TUs, headless) ===" % count)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Failure as error:
        print("zene-api-boundary: %s" % error, file=sys.stderr)
        sys.exit(2)
