#!/usr/bin/env python3
"""Compile the hosted Windows contract of SfzDistoBuffersTest's aligned operators.

Cluster ci-00e6f05f41fa4f1a: the Windows-arm64 runner compiles with clang (mingw
runtime), which defines `_WIN32` but not `_MSC_VER`.  The test's aligned
operator new/delete branched on `_MSC_VER`, so that build fell through to
`posix_memalign`, a function the Windows runtime does not have:

    tests/src/plugins/SfzDistoBuffersTest.cpp:60:6: error: use of undeclared
    identifier 'posix_memalign'

This check extracts the three platform-dependent blocks from the real test
source (unique begin/end markers) and compiles them twice:

  * native: the POSIX branch must allocate and free normally;
  * simulated Windows (`-D_WIN32`): `malloc.h` comes from a shim declaring the
    Windows CRT's `_aligned_malloc`/`_aligned_free`, exactly as the real
    windows-arm64 build gets them.  `posix_memalign` is poisoned with ENOMEM,
    so a test that still selects it throws std::bad_alloc and the probe fails.

The marking of the guard as `_WIN32` is therefore load-bearing in both the
include block (the declaration of `_aligned_malloc`) and the operators.
"""

# Copyright (c) 2026 Zene Studio contributors
#
# This file is part of LMMS - https://lmms.io
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SOURCE = "tests/src/plugins/SfzDistoBuffersTest.cpp"
BLOCKS = {
    "include": "// zene-portable-aligned-include",
    "new": "// zene-portable-aligned-new",
    "delete": "// zene-portable-aligned-delete",
}


def fail(message):
    raise RuntimeError(message)


def extract_block(text, name):
    begin = f"{BLOCKS[name]}: begin"
    end = f"{BLOCKS[name]}: end"
    start = text.find(begin)
    if start < 0:
        fail(f"missing marker {begin!r} in {SOURCE}")
    start = text.find("\n", start)
    stop = text.find(end, start + 1)
    if stop < 0:
        fail(f"missing marker {end!r} in {SOURCE}")
    block = text[start + 1:stop]
    if not block.strip():
        fail(f"empty block between {begin!r} and {end!r} in {SOURCE}")
    return block


def probe_source(blocks):
    return (
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "#include <cstdlib>\n"
        "#include <new>\n"
        "\n"
        "static void noteA() {}\n"
        "static void noteF(void*) {}\n"
        "\n"
        f"{blocks['include']}\n"
        f"{blocks['new']}\n"
        f"{blocks['delete']}\n"
        "\n"
        "#if defined(_WIN32)\n"
        "static char zeneAlignedStorage[64];\n"
        "// Definitions come after the extracted operators on purpose: the only\n"
        "// declaration of _aligned_malloc is the one the extracted include block\n"
        "// gets from the (shim) Windows malloc.h.\n"
        "extern \"C\" void* _aligned_malloc(std::size_t, std::size_t)\n"
        "{ return zeneAlignedStorage; }\n"
        "extern \"C\" void _aligned_free(void* p)\n"
        "{ if (p != zeneAlignedStorage) { std::abort(); } }\n"
        "extern \"C\" int posix_memalign(void**, std::size_t, std::size_t)\n"
        "{ return 12; } // ENOMEM: the Windows runtime has no posix_memalign.\n"
        "#endif\n"
        "\n"
        "int main()\n"
        "{\n"
        "    try {\n"
        "        void* p = ::operator new(std::size_t{16}, std::align_val_t{32});\n"
        "#if defined(_WIN32)\n"
        "        if (p != zeneAlignedStorage) { return 2; }\n"
        "#else\n"
        "        if (reinterpret_cast<std::uintptr_t>(p) % 32 != 0) { return 2; }\n"
        "#endif\n"
        "        ::operator delete(p, std::align_val_t{32});\n"
        "    } catch (std::bad_alloc const&) {\n"
        "        return 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n"
    )


def run_probe(compiler, probe, shim, temporary, windows, label):
    binary = Path(temporary) / f"aligned-{label}"
    command = [compiler, "-std=c++20"]
    if windows:
        command.append("-D_WIN32")
        command.extend(["-I", str(shim)])
    command.extend([str(probe), "-o", str(binary)])
    built = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, check=False)
    if built.returncode:
        fail(f"{label} probe did not compile ({built.returncode}):\n{built.stderr}")
    ran = subprocess.run([str(binary)], text=True, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, check=False)
    if ran.returncode == 0:
        return
    meanings = {
        1: "the POSIX posix_memalign branch was selected (stub returned ENOMEM); "
           "the aligned operators must use the Windows CRT under _WIN32",
        2: "the probe returned an unusable pointer",
        3: "the aligned delete did not route through the Windows CRT",
    }
    detail = meanings.get(ran.returncode, f"probe exited {ran.returncode}")
    fail(f"{label}: {detail}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    args = parser.parse_args()
    root = args.source_root.resolve()

    source = root / SOURCE
    if not source.is_file():
        fail(f"required source is missing: {source}")
    text = source.read_text(encoding="utf-8")
    blocks = {name: extract_block(text, name) for name in BLOCKS}

    compiler = shutil.which("c++")
    if compiler is None:
        fail("required tool is missing: c++")

    with tempfile.TemporaryDirectory(prefix="zene-windows-aligned-") as temporary:
        tmp = Path(temporary)
        shim = tmp / "win-shim"
        shim.mkdir()
        (shim / "malloc.h").write_text(
            "#ifndef ZENE_SIMULATED_WINDOWS_MALLOC_H\n"
            "#define ZENE_SIMULATED_WINDOWS_MALLOC_H\n"
            "#include <cstddef>\n"
            "extern \"C\" {\n"
            "void* _aligned_malloc(std::size_t size, std::size_t alignment);\n"
            "void _aligned_free(void* ptr);\n"
            "}\n"
            "#endif\n", encoding="utf-8")
        probe = tmp / "aligned-operators.cpp"
        probe.write_text(probe_source(blocks), encoding="utf-8")
        run_probe(compiler, probe, shim, tmp, windows=False, label="native")
        run_probe(compiler, probe, shim, tmp, windows=True, label="windows")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"windows-arm64 aligned-new check: {error}", file=sys.stderr)
        sys.exit(1)
