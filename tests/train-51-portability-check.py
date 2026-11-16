#!/usr/bin/env python3
"""Red/green compiler regressions for hosted test portability failures."""

# Copyright (c) 2026 Zene Studio contributors
#
# This file is part of LMMS - https://lmms.io
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.

import argparse
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


def fail(message):
    raise RuntimeError(message)


def invoke(command, cwd=None):
    return subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=False)


def compile_source(compiler, source, directory, label, *, success, flags=(),
                  expected_diagnostic=()):
    path = directory / f"{label}.cpp"
    path.write_text(source, encoding="utf-8")
    result = invoke([compiler, "-std=c++20", "-Werror=unused-function", *flags,
                     "-c", str(path), "-o", str(directory / f"{label}.o")])
    if (result.returncode == 0) != success:
        fail(f"{label}: expected {'compile success' if success else 'compile failure'}; "
             f"exit {result.returncode}: {result.stderr.strip()}")
    missing = [diagnostic for diagnostic in expected_diagnostic
               if diagnostic not in result.stderr]
    if missing:
        fail(f"{label}: expected diagnostic(s) {', '.join(missing)}; "
             f"stderr: {result.stderr.strip()}")
    print(f"PASS {label}: {'compiled' if success else 'failed as expected'}")


def pkg_flags(pkg, modules):
    tool = shutil.which("pkg-config")
    if tool is None:
        fail("required tool is missing: pkg-config")
    result = invoke([tool, "--cflags", *modules])
    if result.returncode:
        fail(f"pkg-config {' '.join(modules)} failed: {result.stderr.strip()}")
    return shlex.split(result.stdout)


def pkg_exists(pkg, modules):
    tool = shutil.which(pkg)
    return tool is not None and invoke([tool, "--exists", *modules]).returncode == 0


def row_probe(root, compiler, temporary):
    source = (root / "tests/src/gui/RowTableViewTest.cpp").read_text(encoding="utf-8")
    begin = source.index("\t\tconst QAbstractItemDelegate* existingRowDelegate =")
    end = source.index("\n\t}", begin)
    assertions = source[begin:end]
    if assertions.count("itemDelegate(") != 2:
        fail("RowTableViewTest no longer has both effective index-delegate assertions")
    fixture = """#include <QtTest>
#include <QStandardItemModel>
#include <QTableView>
void probe() {
    QStandardItemModel model(1, 1);
    QTableView view;
    view.setModel(&model);
%s
}
""" % assertions
    qt5 = pkg_flags("pkg-config", ["Qt5Test", "Qt5Widgets"])
    compile_source(compiler, fixture, temporary, "row-qt5-fixed", success=True,
                   flags=[*qt5, "-fPIC"])
    if pkg_exists("pkg-config", ["Qt6Test", "Qt6Widgets"]):
        qt6 = pkg_flags("pkg-config", ["Qt6Test", "Qt6Widgets"])
        compile_source(compiler, fixture, temporary, "row-qt6-fixed", success=True,
                       flags=[*qt6, "-fPIC"])
    else:
        print("OMITTED row-qt6-fixed: Qt6Test/Qt6Widgets unavailable")
    original = fixture.replace("itemDelegate(", "itemDelegateForIndex(")
    compile_source(compiler, original, temporary, "row-qt5-original", success=False,
                   flags=[*qt5, "-fPIC"])


def midi_probe(root, compiler, temporary):
    source = (root / "tests/src/core/MidiOutTimingTest.cpp").read_text(encoding="utf-8")
    start = source.index("#ifdef LMMS_HAVE_ALSA\nconstexpr int Notes")
    finish = source.index("struct Receiver", start)
    helpers = source[start:finish]
    guarded = "namespace {\n" + helpers + "#endif\n}\n"
    if "measureAgainstRequested" not in guarded or "#ifdef LMMS_HAVE_ALSA" not in guarded:
        fail("MidiOutTimingTest helper guard or helper definition is missing")
    prelude = """#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
using f_cnt_t = unsigned long;
"""
    compile_source(compiler, prelude + guarded, temporary, "midi-windows-fixed", success=True)
    unguarded = helpers.replace("#ifdef LMMS_HAVE_ALSA\n", "", 1)
    unguarded = unguarded.rstrip() + "\n"
    original = "namespace {\n" + unguarded + "}\n"
    compile_source(compiler, prelude + original, temporary, "midi-windows-original",
                   success=False,
                   expected_diagnostic=("measure(", "measureAgainstRequested(",
                                        "unused-function"))


def sfz_probe(root, compiler, temporary):
    source = (root / "tests/src/plugins/SfzDistoBuffersTest.cpp").read_text(encoding="utf-8")
    start = source.index("void *operator new(std::size_t s, std::align_val_t a)")
    finish = source.index("void operator delete(void *p) noexcept", start)
    allocation = source[start:finish]
    delete_start = source.index("void operator delete(void *p, std::align_val_t)", finish)
    delete_finish = source.index("\n}", delete_start) + 2
    deallocation = source[delete_start:delete_finish]
    if "_aligned_malloc" not in allocation or "_aligned_free" not in deallocation:
        fail("SfzDistoBuffersTest Windows aligned allocation/free pair is missing")
    prelude = """#include <cstddef>
#include <new>
namespace std { void free(void*); }
#define _WIN32 1
inline void noteA() noexcept {}
inline void noteF(void const*) noexcept {}
inline void* _aligned_malloc(std::size_t, std::size_t) { return nullptr; }
inline void _aligned_free(void*) {}
"""
    compile_source(compiler, prelude + allocation + deallocation, temporary,
                   "sfz-windows-fixed", success=True)
    original = allocation.replace("#if defined(_WIN32)", "#if defined(_MSC_VER)")
    compile_source(compiler, prelude + original + deallocation, temporary,
                   "sfz-windows-original", success=False,
                   expected_diagnostic=("posix_memalign",))


def touch_probe(root, compiler, temporary):
    source = (root / "tests/src/gui/TouchLayoutTest.cpp").read_text(encoding="utf-8")
    start = source.index("lmms::gui::ClipView* viewOf(const Clip* clip)")
    finish = source.index("\n}\n", start) + 2
    helper = source[start:finish]
    declarations = []
    for marker in ("view = viewOf(clip);", "lowerView = viewOf(lower);"):
        matches = [line for line in source.splitlines() if marker in line]
        if len(matches) != 1 or "lmms::gui::ClipView*" not in matches[0]:
            fail("TouchLayoutTest qualified ClipView declaration is missing")
        declarations.append(matches[0])
    fixture = """#include <vector>
namespace lmms {
struct Clip {};
struct ClipView {};
namespace gui {
struct ClipView { const lmms::Clip* getClip() const { return nullptr; } };
}
}
using namespace lmms;
using namespace lmms::gui;
struct Editor {
    template<class T> std::vector<T> findChildren() { return {}; }
};
struct SongEditor { Editor* m_editor; };
struct GUI { SongEditor* songEditor(); };
GUI* getGUI();
%s
void probe(Clip* clip) {
    %s
    Clip* lower = clip;
    %s
    (void)view;
    (void)lowerView;
}
""" % (helper, declarations[0].strip(), declarations[1].strip())
    compile_source(compiler, fixture, temporary, "touch-helper-and-declarations-fixed",
                   success=True)
    unqualified_helper = helper.replace("lmms::gui::ClipView", "ClipView")
    compile_source(compiler, fixture.replace(helper, unqualified_helper), temporary,
                   "touch-helper-unqualified-original", success=False)
    unqualified_lower = fixture.replace(declarations[1].strip(),
                                        "ClipView* lowerView = viewOf(lower);")
    compile_source(compiler, unqualified_lower, temporary,
                   "touch-lower-view-unqualified-original", success=False)


def mixer_sends_probe(root, compiler, temporary):
    source = (root / "tests/src/gui/MixerSendsOnFadersTest.cpp").read_text(encoding="utf-8")
    declaration = next((line for line in source.splitlines()
                        if "QStringList expectedFaders;" in line), "")
    fill = next((line for line in source.splitlines()
                 if "expectedFaders << QStringLiteral(\"volume\")" in line), "")
    if not declaration or not fill:
        fail("MixerSendsOnFadersTest repeated-value expression is missing")
    qt5 = pkg_flags("pkg-config", ["Qt5Core"])
    fixed = """#include <QStringList>
QStringList probe(int channels) {
%s
%s
return expectedFaders;
}
""" % (declaration.strip(), fill.strip())
    compile_source(compiler, fixed, temporary, "mixer-sends-qt5-fixed", success=True,
                   flags=[*qt5, "-fPIC"])
    old_expression = """QStringList(channels, QStringLiteral("volume"))"""
    qt5_original = """#include <QStringList>
QStringList probe(int channels) {
    return %s;
}
""" % old_expression
    compile_source(compiler, qt5_original, temporary, "mixer-sends-qt5-original",
                   success=False, flags=[*qt5, "-fPIC"])
    if pkg_exists("pkg-config", ["Qt6Core"]):
        qt6 = pkg_flags("pkg-config", ["Qt6Core"])
        compile_source(compiler, fixed, temporary, "mixer-sends-qt6-fixed",
                       success=True, flags=[*qt6, "-fPIC"])
    else:
        print("OMITTED mixer-sends-qt6-red-green: Qt6Core unavailable")


def remote_critical_probe(root, compiler, temporary):
    source = (root / "tests/src/core/RemotePluginAudioPortsTest.cpp").read_text(
        encoding="utf-8")
    start = source.index("#ifndef SYNC_WITH_SHM_FIFO\nint g_criticalsSeen")
    finish = source.index("#endif", start) + len("#endif")
    guarded = source[start:finish]
    if "countCriticals" not in guarded or "QtCriticalMsg" not in guarded:
        fail("RemotePluginAudioPortsTest critical counter guard is missing")
    prelude = """enum QtMsgType { QtCriticalMsg };
struct QMessageLogContext {};
struct QString {};
"""
    fixed = prelude + "namespace {\n#define SYNC_WITH_SHM_FIFO 1\n" + guarded + "\n}\n"
    compile_source(compiler, fixed, temporary, "remote-critical-guard-fixed", success=True)
    original = guarded.replace("#ifndef SYNC_WITH_SHM_FIFO\n", "", 1)
    original = original.rsplit("#endif", 1)[0]
    compile_source(compiler, prelude + "namespace {\n" + original + "\n}\n",
                   temporary, "remote-critical-guard-original", success=False,
                   expected_diagnostic=("countCriticals", "unused-function"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    args = parser.parse_args()
    root = args.source_root.resolve()
    compiler = shutil.which("c++")
    if compiler is None:
        fail("required tool is missing: c++")
    with tempfile.TemporaryDirectory(prefix="train51-portability-") as directory:
        temporary = Path(directory)
        row_probe(root, compiler, temporary)
        midi_probe(root, compiler, temporary)
        sfz_probe(root, compiler, temporary)
        touch_probe(root, compiler, temporary)
        mixer_sends_probe(root, compiler, temporary)
        remote_critical_probe(root, compiler, temporary)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"train51 portability check: {error}", file=sys.stderr)
        sys.exit(1)
