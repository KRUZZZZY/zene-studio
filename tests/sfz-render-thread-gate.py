#!/usr/bin/env python3
"""SfzRenderThreadGate (Q050-484): sfizz's render callback does not run the file pool's garbage collection.

`sfz::Synth::renderBlock` (plugins/SfzPlayer/sfizz/src/sfizz/Synth.cpp) calls FilePool::triggerGarbageCollection about every
five seconds - a spin mutex and a walk over the pool that moves sample data - and FilePool::waitForBackgroundLoading when
freewheeling. Both are removed from the render callback by building with ZENE_SFIZZ_NO_RENDER_THREAD_WAITS, which
plugins/SfzPlayer/CMakeLists.txt always defines. This gate reads the built static library's symbol table: Synth.cpp's object
must not reference triggerGarbageCollection at all (it is the only caller). With the define removed the reference is back and
the gate fails. Needs `nm`; reports skipped (exit 77) where there is none (an MSVC build).

Usage: python3 tests/sfz-render-thread-gate.py <libsfzplayer_sfizz.a>
"""
import shutil
import subprocess
import sys

SYMBOL = "FilePool::triggerGarbageCollection"
MEMBER = "Synth.cpp.o"


def references(archive):
    nm = shutil.which("nm")
    if nm is None:
        return None
    out = subprocess.run([nm, "-A", "-C", archive], capture_output=True, text=True, check=True).stdout
    # nm -A prefixes each line "archive:member.o: address type name"
    return [line for line in out.splitlines() if (":" + MEMBER + ":") in line and SYMBOL in line and " U " in line]


def main():
    archive = sys.argv[1]
    found = references(archive)
    if found is None:
        print("SKIP: no nm on this system")
        return 77
    for line in found:
        print("FAIL: %s" % line)
    print("RESULT: %s" % ("FAIL - the render callback still collects garbage" if found else "PASS - Synth.cpp's render callback does not reference " + SYMBOL))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
