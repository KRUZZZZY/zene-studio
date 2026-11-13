#!/usr/bin/env python3
"""Regression for cluster ci-017caf2edd434b66: the arranger's gesture code must compile on a Qt
built without gesture support.

The hosted mingw64 job builds against the tobydox PPA's Qt 5, whose QtCore/qtcore-config.h
carries `QT_NO_GESTURES` (the same file carries `QT_NO_ICONV` for a disabled feature). Qt's
`Qt::GestureState` - and QWidget::grabGesture, QGestureEvent, QPinchGesture - exist only when
gestures are enabled (`#ifndef QT_NO_GESTURES` in qnamespace.h and qwidget.h), so
`include/ArrangerZoom.h`'s unconditional `Qt::GestureState` parameter failed the build:

    include/ArrangerZoom.h:81:64: error: 'Qt::GestureState' has not been declared
      (src/gui/ControlCommandsArrangerView.cpp, job 132025174898)

The same missing guard was in src/gui/ArrangerZoom.cpp's definition, src/gui/ArrangerAids.cpp's
QPinchGesture filter, and tests/src/gui/ArrangerTimelineTest.cpp's canceled-pinch case.

This box's Qt has gestures, so the check does not rely on the host's features: it rebuilds each
affected translation unit from this build's own compile_commands.json against a SHADOW
`QtCore/qtcore-config.h` whose gestures feature is off - exactly the configuration Qt's own
configure writes for a gesture-less Qt, and one under which Qt's headers stay coherent (defining
QT_NO_GESTURES on a gestures-enabled Qt instead breaks qevent.h itself, which is not the failing
platform). Two probes keep the simulation honest: Qt::GestureStarted must be undeclared under the
shadow and declared without it. The API is exercised both ways: the bool form must compile under
the shadow (a Qt without gestures has no Qt::GestureState) and the Qt::GestureState form must
compile where gestures exist.

Usage: arranger-zoom-no-gestures-check.py --build-dir <build> [--moc <moc>]
"""

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

# Every translation unit that includes ArrangerZoom.h, plus the API's own definition. A guard
# that only fixes the header leaves these broken, and a guard that only fixes one of them is
# what a build job reports next.
AFFECTED = [
    "src/gui/ArrangerZoom.cpp",
    "src/gui/ArrangerAids.cpp",
    "src/gui/ControlCommandsArrangerView.cpp",
    "src/gui/ArrangerNavigator.cpp",
    "src/gui/editors/SongEditor.cpp",
    "tests/src/gui/ArrangerTimelineTest.cpp",
]

PROBE = """#include <QtCore/qnamespace.h>
int main() { return Qt::GestureStarted; }
"""

# The public API in both spellings: the bool form exists on every Qt, the Qt::GestureState
# overload only where the enum does.
API_FIXTURE = """#include "ArrangerZoom.h"
static void ended() { lmms::gui::arrangerzoom::finishPinch(1, 2, true); }
int main() { ended(); return 0; }
"""

TYPED_FIXTURE = """#include "ArrangerZoom.h"
int main() { lmms::gui::arrangerzoom::finishPinch(1, 2, Qt::GestureCanceled); return 0; }
"""

WINDOWS_COMMAND_RE = re.compile(r"(?:^|[\s=\"'])[A-Za-z]:[\\/]|\\[^\"']")


def split_command(command):
    """A recorded command line as an argument list (Windows paths keep their separators)."""
    if WINDOWS_COMMAND_RE.search(command):
        arguments = shlex.split(command, posix=False)
        return [a[1:-1] if len(a) > 1 and a[0] == a[-1] == '"' else a for a in arguments]
    return shlex.split(command)


def entry_arguments(entry):
    if entry.get("command"):
        return split_command(entry["command"])
    return list(entry.get("arguments") or [])


def compile_flags(args):
    """The command without its output argument and compile action."""
    out = []
    index = 0
    while index < len(args):
        arg = args[index]
        if arg in ("-o", "/Fo"):
            index += 2
            continue
        if arg.startswith("/Fo"):
            index += 1
            continue
        if arg in ("-c", "/c"):
            index += 1
            continue
        out.append(arg)
        index += 1
    return out


def include_dirs(args):
    out = []
    index = 0
    while index < len(args):
        arg = args[index]
        if arg in ("-I", "-isystem", "/I", "/external:I") and index + 1 < len(args):
            out.append(args[index + 1])
            index += 2
            continue
        if arg.startswith(("-I", "/I", "/external:I")):
            value = arg.split(":", 1)[1] if arg.lower().startswith(("/external:i", "-external:i")) else arg[2:]
            if value:
                out.append(value)
        index += 1
    return out


def run(cmd, cwd, timeout=300):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)


def source_of(entry):
    """The TU's absolute path (compile_commands.json may carry it relative to the directory)."""
    path = entry["file"]
    if not os.path.isabs(path):
        path = os.path.join(entry["directory"], path)
    return os.path.normpath(path)


class Toolchain:
    """The compile line of one recorded translation unit, reused for probes and fixtures."""

    def __init__(self, entry):
        self.directory = entry["directory"]
        self.source = source_of(entry)
        self.args = compile_flags(entry_arguments(entry))
        if not self.args:
            raise SystemExit("FAIL: %s has no compile command" % entry["file"])
        self.msvc = os.path.basename(self.args[0]).lower() in ("cl", "cl.exe")
        self.syntax_only = "/Zs" if self.msvc else "-fsyntax-only"
        self.include_flag = "/I" if self.msvc else "-I"
        self.includes = include_dirs(self.args)

    def command(self, extra_includes, source):
        # The recorded flags carry this TU's defines and include paths; only the leading
        # compiler, the include search order and the source argument change.
        flags = list(self.args[1:])
        while flags and os.path.normpath(flags[-1]) == self.source:
            flags.pop()
        prefix = [self.args[0], self.syntax_only]
        # The shadow QtCore comes first, so its qtcore-config.h is the one the compiler sees;
        # the recorded include path follows it.
        prefix += [self.include_flag + path for path in extra_includes]
        return prefix + flags + [source]

    def moc_include(self, source, tempdir, moc):
        """Run moc for a TU that includes its own .moc; (include dir or None, detail)."""
        match = re.search(r'#include\s+"([\w.+-]+\.moc)"', open(source).read())
        if match is None:
            return None, ""
        if not moc:
            return None, "needs %s but no moc was given" % match.group(1)
        moc_output = os.path.join(tempdir, match.group(1))
        moc_cmd = [moc, source, "-o", moc_output]
        for directory in self.includes:
            moc_cmd += ["-I", directory]
        produced = run(moc_cmd, self.directory)
        if produced.returncode != 0:
            lines = (produced.stderr or produced.stdout).strip().splitlines()
            return None, "moc failed: " + (lines[-1] if lines else "no output")
        return tempdir, ""

    def compile_source(self, source, extra_includes, tempdir, moc):
        """Compile @source in this TU's include environment; (ok, detail)."""
        includes = list(extra_includes)
        moc_dir, detail = self.moc_include(source, tempdir, moc)
        if moc_dir is None and detail:
            return False, detail
        if moc_dir:
            includes.append(moc_dir)
        result = run(self.command(includes, source), self.directory)
        if result.returncode == 0:
            return True, ""
        lines = [line for line in (result.stderr or result.stdout).splitlines() if "error" in line.lower()]
        return False, "; ".join(lines[:3]) or "compile failed (exit %d)" % result.returncode


def find_config(entries):
    """The real QtCore/qtcore-config.h, from the build's own recorded include paths."""
    for entry in entries:
        base = entry["directory"]
        for directory in include_dirs(entry_arguments(entry)):
            for candidate in (os.path.join(directory, "QtCore", "qtcore-config.h"),
                              os.path.join(directory, "qtcore-config.h")):
                if not os.path.isabs(candidate):
                    candidate = os.path.join(base, candidate)
                if os.path.isfile(candidate):
                    return os.path.normpath(candidate)
    return None


def write_shadow(config_path, shadow):
    """A QtCore/qtcore-config.h with gestures off, as Qt's configure writes one."""
    text = open(config_path).read()
    if re.search(r"^#\s*define\s+QT_FEATURE_gestures\b", text, re.M):
        text = re.sub(r"^#\s*define\s+QT_FEATURE_gestures\b.*$", "#define QT_FEATURE_gestures -1", text, flags=re.M)
    else:
        text += "\n#define QT_FEATURE_gestures -1\n"
    text += "\n#ifndef QT_NO_GESTURES\n#define QT_NO_GESTURES 1\n#endif\n"
    destination = os.path.join(shadow, "QtCore", "qtcore-config.h")
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    with open(destination, "w") as handle:
        handle.write(text)
    return destination


def write_fixtures(tempdir):
    """The API fixtures and the feature probe; {name: path}."""
    fixtures = {}
    for name, text in (("probe", PROBE), ("bool_api", API_FIXTURE), ("typed_api", TYPED_FIXTURE)):
        fixtures[name] = os.path.join(tempdir, name + ".cpp")
        with open(fixtures[name], "w") as handle:
            handle.write(text)
    return fixtures


def load_build(build_dir):
    """(control toolchain, entries, entries by source, tree root, Qt feature header) or an error."""
    build_dir = os.path.abspath(build_dir)
    commands = os.path.join(build_dir, "compile_commands.json")
    if not os.path.isfile(commands):
        return "no compile_commands.json in " + build_dir
    entries = json.load(open(commands))
    if not entries:
        return "compile_commands.json is empty"
    root = os.path.dirname(build_dir)
    by_source = {source_of(entry): entry for entry in entries}
    control = by_source.get(os.path.normpath(os.path.join(root, "src/gui/ArrangerZoom.cpp")))
    if control is None:
        return "src/gui/ArrangerZoom.cpp is not in compile_commands.json"
    config = find_config(entries)
    if config is None:
        return "no QtCore/qtcore-config.h on any recorded include path"
    return Toolchain(control), entries, by_source, root, config


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--moc", default="")
    options = parser.parse_args()

    setup = load_build(options.build_dir)
    if isinstance(setup, str):
        print("FAIL:", setup)
        return 1
    toolchain, entries, by_source, root, config = setup
    print("Qt feature header:", config)

    failures = []

    def report(ok, label, detail=""):
        print("%s %s%s" % ("PASS" if ok else "FAIL", label, "" if ok else ": " + detail))
        if not ok:
            failures.append(label)

    with tempfile.TemporaryDirectory(prefix="zene-arranger-zoom-") as tempdir:
        shadow = os.path.join(tempdir, "shadow")
        write_shadow(config, shadow)
        fixtures = write_fixtures(tempdir)

        # The simulation is only meaningful if the shadow really hides the enum, and the
        # normal build is only honest if the same probe compiles without it.
        ok, detail = toolchain.compile_source(fixtures["probe"], [shadow], tempdir, None)
        report(not ok, "the shadow really is gesture-less (Qt::GestureStarted must not exist)",
               "it compiled, so the probe does not detect the feature: " + detail if ok else detail)
        host_gestures, host_detail = toolchain.compile_source(fixtures["probe"], [], tempdir, None)

        for source in AFFECTED:
            entry = by_source.get(os.path.normpath(os.path.join(root, source)))
            if entry is None:
                report(False, source + " compiles on a Qt without gestures",
                       "not in compile_commands.json (was it renamed?)")
                continue
            ok, detail = Toolchain(entry).compile_source(source_of(entry), [shadow], tempdir, options.moc)
            report(ok, source + " compiles on a Qt without gestures", detail)

        ok, detail = toolchain.compile_source(fixtures["bool_api"], [shadow], tempdir, None)
        report(ok, "finishPinch(int, int, bool) is callable without gestures", detail)
        # The gesture-typed spelling only exists where Qt has gestures: on a gesture-less host
        # the six compiles above ARE the shipped configuration, so this one is skipped, not
        # failed - a supported platform must not fail its own regression.
        if host_gestures:
            ok, detail = toolchain.compile_source(fixtures["typed_api"], [], tempdir, None)
            report(ok, "finishPinch(int, int, Qt::GestureState) still exists with gestures", detail)
        else:
            print("SKIP this host's Qt has no gestures, so the typed overload does not exist here"
                  + (": " + host_detail if host_detail else ""))

    if failures:
        print("FAIL: %d arranger gesture case(s) do not build on a Qt without gestures: %s"
              % (len(failures), "; ".join(failures)))
        return 1
    print("PASS: the arranger gesture code builds on a Qt without gestures")
    return 0


if __name__ == "__main__":
    sys.exit(main())
