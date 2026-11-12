#!/usr/bin/env python3
"""Regression for cluster ci-12f4249981b085d6: the VST2 test instrument must
keep Darwin's C99 <stdio.h> declarations visible.

The macos-arm64 build stopped at

    tests/data/vst2-test-instrument/vst2-test-instrument.c:173: error:
    call to undeclared library function 'snprintf'

even though the file includes <stdio.h>.  Darwin's headers gate C99
declarations on the feature level: the fixture defines _POSIX_C_SOURCE 199309L
(for nanosleep) but not _DARWIN_C_SOURCE, so Darwin's stdio.h hides snprintf.
Glibc exposed it anyway - __USE_ISOC99 follows __STDC_VERSION__ - which is why
only the macOS job saw the error.

This box is not Darwin, so the check compiles the real fixture twice with the
host C compiler:

  1. native: unchanged, with the system headers; must build.
  2. simulated Darwin: __APPLE__/__MACH__ plus a fake <stdio.h> ahead of the
     system headers that models Darwin's rule - _POSIX_C_SOURCE without
     _DARWIN_C_SOURCE hides snprintf.  The fixture must define
     _DARWIN_C_SOURCE (or not restrict the level at all) to build.

Before the fix case 2 fails with "implicit declaration of function
'snprintf'", the hosted error; after it both cases pass.  Deterministic: two
compiler runs over fixed inputs, no timing.

Usage: vst2-fixture-darwin-headers-check.py <c compiler> <fixture source>
"""

import os
import subprocess
import sys
import tempfile

# A model of Darwin's <stdio.h> rule: only the C89 declarations are
# unconditional; specifying a POSIX level without _DARWIN_C_SOURCE pins the
# visibility below C99, so snprintf is not declared.  _DARWIN_C_SOURCE (or no
# POSIX level at all) restores the full header, as it does on the hosted macOS
# job (src/core/CrashReporter.cpp uses the same guard for O_NOFOLLOW).
FAKE_DARWIN_STDIO = """\
#ifndef ZENE_TEST_FAKE_DARWIN_STDIO_H
#define ZENE_TEST_FAKE_DARWIN_STDIO_H

#include <stddef.h>

int sscanf(const char*, const char*, ...);

#if defined(_POSIX_C_SOURCE) && !defined(_DARWIN_C_SOURCE)
# define ZENE_TEST_DARWIN_C_LEVEL _POSIX_C_SOURCE
#else
# define ZENE_TEST_DARWIN_C_LEVEL 200809L
#endif

#if ZENE_TEST_DARWIN_C_LEVEL >= 200112L
int snprintf(char*, size_t, const char*, ...);
#endif

#endif
"""


def compile_case(compiler, source, include_dir, out, defines, extra_includes):
    cmd = [compiler, "-std=c99", "-Wall", "-Werror",
           "-Werror=implicit-function-declaration", "-fPIC", "-c", source,
           "-I", include_dir]
    for i in extra_includes:
        cmd += ["-I", i]
    cmd += ["-D" + d for d in defines]
    cmd += ["-o", out]
    result = subprocess.run(cmd, capture_output=True, text=True)
    return cmd, result


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    compiler, source = sys.argv[1], os.path.abspath(sys.argv[2])
    if not os.path.isfile(source):
        print("FAIL: no fixture source at", source)
        return 1
    include_dir = os.path.dirname(source)

    failures = 0
    with tempfile.TemporaryDirectory(prefix="zene-vst2-darwin-") as tmp:
        fake_dir = os.path.join(tmp, "fake-darwin")
        os.makedirs(fake_dir)
        with open(os.path.join(fake_dir, "stdio.h"), "w") as f:
            f.write(FAKE_DARWIN_STDIO)

        cases = [
            ("native headers", [], []),
            ("simulated Darwin headers", ["__APPLE__", "__MACH__"], [fake_dir]),
        ]
        for index, (label, defines, extra_includes) in enumerate(cases):
            cmd, result = compile_case(compiler, source, include_dir,
                                       os.path.join(tmp, "case%d.o" % index),
                                       defines, extra_includes)
            if result.returncode != 0:
                print("FAIL [{}]: fixture did not compile (exit {})".format(
                    label, result.returncode))
                print("  command:", " ".join(cmd))
                print(result.stderr.strip()[:2000])
                failures += 1
            else:
                print("PASS [{}]".format(label))

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
