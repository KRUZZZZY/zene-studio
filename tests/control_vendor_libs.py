#!/usr/bin/env python3
"""The vendored wasmtime C API's lib directory, for a spawned instance's LD_LIBRARY_PATH.

WHY THIS MODULE EXISTS (measured 2026-09-16, the 0.3.0 fix-up pass). A build made with
the wasmtime C API on the find path (`scripts/fetch-wasmtime.sh` installs it under
`<tree>/third_party/wasmtime`) links `libwasmtime.so` - `readelf -d build/zene` lists it
as NEEDED - and the BUILD TREE carries no rpath for it: `readelf -d` prints no
RPATH/RUNPATH at all, so a binary-driven test cannot start unless the runner exports
`LD_LIBRARY_PATH=<tree>/third_party/wasmtime/lib`. Measured, not assumed:

    $ env -u LD_LIBRARY_PATH build/zene --version
    build/zene: error while loading shared libraries: libwasmtime.so: cannot open
    shared object file: No such file or directory                       (exit 127)

The wave-9 trains measured the consequence on their proof tables: 11/13 green with the
export, 7/13 without it.

WHY THE FIX IS NOT AN RPATH, which is where it was looked for first. `src/CMakeLists.txt`
sets `CMAKE_BUILD_WITH_INSTALL_RPATH TRUE` for Linux (upstream, so the build tree uses
the install rpath for a non-standard Qt prefix), and CMake IGNORES `BUILD_RPATH` under
it - verified, not assumed: a six-line CMake project (a main.c linking a .so by absolute
path, that variable TRUE, BUILD_RPATH set) emitted no RUNPATH at all. `CMAKE_INSTALL_RPATH_USE_LINK_PATH`
does not cover it either, because it appends link directories OUTSIDE the project and
`third_party/` is inside it. The one rpath route left would bake this machine's source
path into the INSTALLED binary's rpath, which is refused. Fixing it would mean editing
the upstream line or the install rpath; both are refused for a fix-up, so the runtime
search path is supplied where the test starts the instance instead.

WHY IT IS NOT IN THE HARNESS. `tests/control_socket_harness.py` is a fork file AT its
Gate 7 cap (511 lines, `tests/file-length-baseline.tsv`) with zero headroom, and its own
header already records this pattern (the per-case payloads live in
`control_socket_flows.py`, the frozen-instance diagnosis in
`control_instance_diagnosis.py`, "for the same Gate 7 reason"). The harness gains one
import and one wrapped call; the code and the reasoning live here.

CI IS UNAFFECTED, and that is the point of deriving the directory instead of hardcoding
it: no workflow fetches wasmtime, so `FindWasmtime` degrades `WANT_WASM` to OFF on the
runners and no CI binary carries that NEEDED entry. `with_vendor_library_path()` returns
the environment unchanged when no vendored tree is found.
"""

import os


def vendor_library_path(binary):
    """The vendored C API's lib directory, or None when this tree has none.

    Candidates, in order: `$WASMTIME_ROOT/lib` (the same override `FindWasmtime.cmake`
    honours), then `<dir>/third_party/wasmtime/lib` for the binary's own directory and
    each of its two parents - which covers `<tree>/build/zene`, a test binary in
    `<tree>/build/tests/` and a binary built beside the sources.
    """
    candidates = []
    if os.environ.get("WASMTIME_ROOT"):
        candidates.append(os.path.join(os.environ["WASMTIME_ROOT"], "lib"))
    directory = os.path.dirname(os.path.abspath(binary))
    for _ in range(3):
        candidates.append(os.path.join(directory, "third_party", "wasmtime", "lib"))
        directory = os.path.dirname(directory)
    for candidate in candidates:
        if os.path.isdir(candidate) and any(name.startswith("libwasmtime")
                                            for name in os.listdir(candidate)):
            return candidate
    return None


def with_vendor_library_path(binary, env):
    """Prepend the vendored C API's lib directory to `env`'s LD_LIBRARY_PATH, in place.

    Returns `env`, so a caller can write `env=with_vendor_library_path(binary, ...)`.
    An existing LD_LIBRARY_PATH is kept, with the vendor directory in front of it.
    """
    directory = vendor_library_path(binary)
    if not directory:
        return env
    existing = env.get("LD_LIBRARY_PATH")
    env["LD_LIBRARY_PATH"] = "%s:%s" % (directory, existing) if existing else directory
    return env
