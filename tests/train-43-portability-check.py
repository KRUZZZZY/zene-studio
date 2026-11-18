#!/usr/bin/env python3
"""Focused compiler contract regressions for train 43 portability fixes."""

import argparse
import importlib.util
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile


def fail(message):
    raise RuntimeError(message)


def require_file(path):
    if not path.is_file():
        fail(f"required file is missing: {path}")


def load_train41(root):
    path = root / "tests/train-41-portability-check.py"
    require_file(path)
    spec = importlib.util.spec_from_file_location("train41_portability", path)
    if spec is None or spec.loader is None:
        fail(f"cannot load helpers from {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(command, cwd=None):
    printable = shlex.join([str(part) for part in command])
    print(f"COMMAND: {printable}", flush=True)
    result = subprocess.run(command, cwd=cwd, check=False)
    print(f"RESULT: exit {result.returncode}", flush=True)
    if result.returncode:
        fail(f"command failed with exit code {result.returncode}: {printable}")


def compiler():
    tool = shutil.which("c++")
    if tool is None:
        fail("required tool is missing: c++")
    return tool


def probe_compile_db_tu(root, build, helpers, relative, label, moc, extra=()):
    entries = helpers.compile_database(build)
    entry, command = helpers.entry_for(entries, root, relative)
    source = root / relative
    source_text = source.read_text(encoding="utf-8")
    moc_include = re.search(r'^\s*#\s*include\s+"([^"]+\.moc)"', source_text, re.M)
    with tempfile.TemporaryDirectory(prefix=f"train43-{label}-") as tmp:
        shadow = Path(tmp)
        if moc_include:
            generated = shadow / moc_include.group(1)
            run([moc, str(source), "-o", str(generated)], cwd=root)
            extra = ("-I" + str(shadow), *extra)
        if label == "barrier":
            (shadow / "barrier").write_text(
                "#error <barrier> is forbidden in RemotePluginShmFifoTest\n", encoding="utf-8")
            extra = ("-I" + str(shadow), *extra)
        if label == "warp":
            # Shadow only QThread's declaration, retaining every other Qt API.
            headers = [Path(arg) / "qthread.h" for arg in command]
            header = next((path for path in headers if path.is_file()), None)
            if header is None:
                fail("configured QtCore qthread.h is missing from the compiler include paths")
            (shadow / "QtCore").mkdir()
            text = re.sub(r"\bcreate\b", "train43_unavailable_create", header.read_text())
            (shadow / "QtCore/qthread.h").write_text(text)
            for path in (shadow / "QThread", shadow / "QtCore/QThread"):
                path.write_text("#include <QtCore/qthread.h>\n")
            extra = ("-I" + str(shadow), *extra)
        args = helpers.syntax_command(entry, command, extra)
        run(args, cwd=entry["directory"])


def probe_warp(root, build, helpers, moc):
    probe_compile_db_tu(root, build, helpers,
                        "tests/src/gui/Warp/SampleClipWarpEditTest.cpp", "warp", moc)


def probe_import_assignments(root):
    source = root / "tests/src/core/ImportTrackIoTest.cpp"
    require_file(source)
    text = source.read_text(encoding="utf-8")
    start = text.index("void anUnparsableDocumentDoesNotShiftTheOthers()")
    end = text.index("plan.need", start)
    statements = re.findall(r"\bplan\.documents[^;]*;", text[start:end])
    if not statements:
        fail(f"could not extract the actual document construction from {source}")
    tool = shutil.which("pkg-config")
    if tool is None:
        fail("required tool is missing: pkg-config")
    pkg_command = [tool, "--cflags", "Qt5Core"]
    print(f"COMMAND: {shlex.join(pkg_command)}", flush=True)
    flags = subprocess.run(pkg_command, text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    print(f"RESULT: exit {flags.returncode}", flush=True)
    if flags.returncode:
        fail(f"pkg-config Qt5Core failed: {flags.stderr.strip()}")
    with tempfile.TemporaryDirectory(prefix="train43-import-") as tmp:
        tmpdir = Path(tmp)
        probe = tmpdir / "import_assignments.cpp"
        # Preserve the source expressions while mapping the test's plan to a QStringList fixture.
        body = ["#include <QStringList>", "struct PlanProxy { QStringList& documents; };",
                "int main() {", "QStringList plan_documents;", "PlanProxy plan{plan_documents};",
                'const QString UnparsableTrack = QStringLiteral("<track/>");',
                'auto trackDocument = [](const QString&) { return QStringLiteral("<track/>"); };',
                'const QString Lead = QStringLiteral("Lead");']
        body.extend(statements)
        body.extend(["return plan.documents.isEmpty();", "}"])
        probe.write_text("\n".join(body) + "\n", encoding="utf-8")
        run([compiler(), "-std=c++17", "-fPIC", *shlex.split(flags.stdout), "-fsyntax-only", str(probe)], cwd=root)


def probe_clock(root):
    header = root / "plugins/NeuralAmp/tests/NamHarnessCpuClock.h"
    require_file(header)
    with tempfile.TemporaryDirectory(prefix="train43-clock-") as tmp:
        tmpdir = Path(tmp)
        fake = tmpdir / "fake"
        fake.mkdir()
        (fake / "windows.h").write_text(
            """#pragma once
#include <cstdint>
using DWORD = std::uint32_t;
using HANDLE = void*;
struct FILETIME { DWORD dwLowDateTime; DWORD dwHighDateTime; };
inline FILETIME fakeKernel{5, 1}, fakeUser{7, 2};
inline bool fakeSuccess = true;
inline HANDLE GetCurrentThread() { return nullptr; }
inline int GetThreadTimes(HANDLE, FILETIME* created, FILETIME* exited, FILETIME* kernel, FILETIME* user) {
    *created = {}; *exited = {}; *kernel = fakeKernel; *user = fakeUser; return fakeSuccess;
}
""", encoding="utf-8")
        win = tmpdir / "windows_clock.cpp"
        win.write_text(
            f'''#include "{header}"
#include <windows.h>
#include <cassert>
int main() {{
    const double expected = (4294967301.0 + 8589934599.0) / 10.0;
    assert(namThreadCpuMicroseconds() == expected);
    fakeSuccess = false;
    try {{ (void)namThreadCpuMicroseconds(); }} catch (const std::runtime_error&) {{ return 0; }}
    return 1;
}}
''', encoding="utf-8")
        exe = tmpdir / "windows_clock"
        run([compiler(), "-std=c++17", "-D_WIN32", "-I" + str(fake), str(win), str(header.parent / "HarnessClockWindows.cpp"), "-o", str(exe)], cwd=root)
        run([str(exe)], cwd=root)

        native = tmpdir / "native_clock.cpp"
        native.write_text(
            f'''#include "{header}"
#include <cassert>
int main() {{
    const double first = namThreadCpuMicroseconds();
    const double second = namThreadCpuMicroseconds();
    assert(first >= 0.0 && second >= first);
}}
''', encoding="utf-8")
        native_exe = tmpdir / "native_clock"
        run([compiler(), "-std=c++17", str(native), str(header.parent / "HarnessClockPosix.cpp"), "-o", str(native_exe)], cwd=root)
        run([str(native_exe)], cwd=root)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--moc", required=True)
    args = parser.parse_args()
    root, build = args.source_root.resolve(), args.build_dir.resolve()
    helpers = load_train41(root)
    print("[remote-shm-fifo-no-barrier]", flush=True)
    probe_compile_db_tu(root, build, helpers,
                        "tests/src/core/RemotePluginShmFifoTest.cpp", "barrier", args.moc)
    print("[warp-callable-thread-without-qthread-create]", flush=True)
    probe_warp(root, build, helpers, args.moc)
    print("[import-track-qt5-qstringlist-statements]", flush=True)
    probe_import_assignments(root)
    print("[neuralamp-cpu-clock]", flush=True)
    probe_clock(root)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"train-43 portability check: {error}", file=sys.stderr)
        sys.exit(1)
