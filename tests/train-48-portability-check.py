#!/usr/bin/env python3
"""Compile source-derived regressions for four platform portability fixes."""

# Copyright (c) 2026 Zene Studio contributors
#
# This file is part of LMMS - https://lmms.io
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program. If not, see <https://www.gnu.org/licenses/>.

import argparse
import importlib.util
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


def fail(message):
    raise RuntimeError(message)


def require_file(path):
    if not path.is_file():
        fail(f"required source or build file is missing: {path}")


def train41_helpers(root):
    path = root / "tests/train-41-portability-check.py"
    require_file(path)
    spec = importlib.util.spec_from_file_location("train41_portability", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(command, cwd=None):
    result = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, check=False)
    if result.returncode:
        fail(f"command failed ({result.returncode}): {shlex.join(command)}\n{result.stderr}")


def object_command(helpers, entry, command, temporary, label, extra=()):
    args = helpers.syntax_command(entry, command, extra)
    args.remove("-fsyntax-only")
    args.extend(["-c", "-o", str(Path(temporary) / (label + ".o"))])
    return args


def expect_failure(command, cwd, diagnostic):
    result = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, check=False)
    if result.returncode == 0 or diagnostic not in result.stderr:
        fail(f"negative control did not reproduce {diagnostic}: {result.stderr}")


def mode_sfz(root, build, helpers):
    entries = helpers.compile_database(build)
    entry, command = helpers.entry_for(entries, root, "tests/src/plugins/SfzPlayerTest.cpp")
    dependency = (root / "plugins/SfzPlayer/sfizz/external/abseil-cpp").resolve()
    includes = [Path(token[2:]).resolve() for token in command if token.startswith("-I")]
    includes.extend(Path(command[index + 1]).resolve() for index, token in enumerate(command[:-1])
                    if token == "-I")
    if dependency not in includes:
        fail(f"actual SfzPlayerTest compile command lacks private Abseil include root: {dependency}")
    with tempfile.TemporaryDirectory(prefix="train48-sfz-") as temporary:
        # Block system Abseil as a fallback: the target must resolve sfizz's copy.
        blocked = Path(temporary) / "fallback"
        (blocked / "absl/meta").mkdir(parents=True)
        (blocked / "absl/meta/type_traits.h").write_text(
            '#error train48 missing Abseil root\n')
        extra = ["-I" + str(blocked)]
        run(object_command(helpers, entry, command, temporary, "sfz", extra),
            cwd=entry["directory"])
        original = [token for token in command if token != "-I" + str(dependency)]
        expect_failure(object_command(helpers, entry, original, temporary, "sfz-original", extra),
                       entry["directory"], "train48 missing Abseil root")


def mode_video(root, build, qt_major):
    header = root / "tests/src/core/VideoTestClip.h"
    config = build / "lmmsconfig.h"
    require_file(header)
    require_file(config)
    compiler = shutil.which("c++")
    pkg = shutil.which("pkg-config")
    if compiler is None or pkg is None:
        fail("required tool is missing: c++ or pkg-config")
    flags_result = subprocess.run([pkg, "--cflags", "--libs", f"Qt{qt_major}Core"], text=True,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if flags_result.returncode:
        fail(f"pkg-config Qt{qt_major}Core failed: {flags_result.stderr.strip()}")
    flags = shlex.split(flags_result.stdout)
    with tempfile.TemporaryDirectory(prefix="train48-video-") as temporary:
        (Path(temporary) / "lmmsconfig.h").write_text(
            f'#include "{config}"\n#undef ZENE_HAVE_FFMPEG\n')
        probe = Path(temporary) / "video_no_ffmpeg.cpp"
        probe.write_text(
            '#include "lmmsconfig.h"\n#undef ZENE_HAVE_FFMPEG\n'
            '#include "VideoTestClip.h"\n'
            'int main() {\n'
            ' const QString path;\n'
            ' return videotest::makeClip(path) ||\n'
            '        videotest::makeClip(path, videotest::Palette::RedBlue) ||\n'
            '        videotest::makeClip(path, videotest::Palette::GreenYellow);\n'
            '}\n', encoding="utf-8")
        binary = Path(temporary) / "video_no_ffmpeg"
        run([compiler, "-std=c++20", "-fPIC", "-I" + temporary, "-I" + str(build),
             "-I" + str(root / "tests/src/core"), str(probe), "-o", str(binary), *flags])
        run([str(binary)])


def windows_preload(helpers, source, destination, definitions):
    lines = []
    for header in helpers.source_headers(source):
        lines.append(f'#include "{header}"' if isinstance(header, Path) else f"#include {header}")
    lines.extend(definitions)
    destination.write_text("\n".join(lines) + "\n", encoding="utf-8")


def moc(moc_path, source, output, cwd, defines):
    require_file(Path(moc_path))
    run([str(moc_path), *defines, str(source), "-o", str(output)], cwd=cwd)


def mode_render(root, build, moc_path, helpers):
    entries = helpers.compile_database(build)
    relative = "tests/src/core/RenderSourceGuardTest.cpp"
    entry, command = helpers.entry_for(entries, root, relative)
    source = root / relative
    with tempfile.TemporaryDirectory(prefix="train48-render-") as temporary:
        tmp = Path(temporary)
        shadow = tmp / "RenderSourceGuardTest.moc"
        preload = tmp / "windows_macros.h"
        windows_preload(helpers, source, preload, ["#define _WIN32 1", "#define Q_OS_WIN 1"])
        moc(moc_path, source, shadow, root, ["-DQ_OS_WIN=1"])
        args = object_command(helpers, entry, command, temporary, "render",
                              ["-include", str(preload), "-Werror=unused-function"])
        args.insert(1, "-I" + temporary)
        run(args, cwd=entry["directory"])


def mode_export(root, build, moc_path, helpers):
    entries = helpers.compile_database(build)
    relative = "tests/src/core/ExportWavWriteErrorTest.cpp"
    entry, command = helpers.entry_for(entries, root, relative)
    source = root / relative
    with tempfile.TemporaryDirectory(prefix="train48-export-") as temporary:
        tmp = Path(temporary)
        shadow = tmp / "ExportWavWriteErrorTest.moc"
        preload = tmp / "windows_macros.h"
        windows_preload(helpers, source, preload,
                        ["#undef Q_OS_UNIX", "#define Q_OS_WIN 1"])
        moc(moc_path, source, shadow, root, ["-DQ_OS_WIN=1", "-UQ_OS_UNIX"])
        args = object_command(helpers, entry, command, temporary, "export",
                              ["-include", str(preload), "-Werror=unused-function"])
        args.insert(1, "-I" + temporary)
        run(args, cwd=entry["directory"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--moc", default="/usr/lib/qt6/moc")
    parser.add_argument("--qt-major", required=True, choices=("5", "6"))
    args = parser.parse_args()
    root = args.source_root.resolve()
    build = args.build_dir.resolve()
    helpers = train41_helpers(root)
    mode_sfz(root, build, helpers)
    mode_video(root, build, args.qt_major)
    mode_render(root, build, args.moc, helpers)
    mode_export(root, build, args.moc, helpers)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"portability check: {error}", file=sys.stderr)
        sys.exit(1)
