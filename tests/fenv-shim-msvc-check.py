#!/usr/bin/env python3
"""Regression for cluster ci-v041-6: include/fenv.h must not take #include_next on MSVC.

include/fenv.h shadows the C library's <fenv.h> (it polyfills feenableexcept on
macOS) and reached the real header with an unconditional `#include_next`.  MSVC
(cl.exe) has no such directive - `fatal error C1021: invalid preprocessor
command 'include_next'` on the hosted msvc-x64 job - and MSVC's own <cfenv>
includes <fenv.h>, so any MSVC translation unit using <cfenv> hit it.

This box has no MSVC, so the check runs the HOST compiler's preprocessor over
the shim with cl.exe's identity predefined (_MSC_VER, no __clang__) and a fake
Windows SDK layout (Include/<ver>/{um,ucrt}): on that configuration the shim
must forward to the UCRT's fenv.h (a sentinel header here), i.e. it must not
take the #include_next branch, which would land on the host's own fenv.h.  The
other configurations must keep #include_next (the host's real header): plain
GCC/Clang and clang-cl (_MSC_VER together with __clang__), which supports it.

Usage: fenv-shim-msvc-check.py <c++ compiler> <source include dir>
"""

import os
import subprocess
import sys
import tempfile

SENTINEL = "ZENE_TEST_FAKE_UCRT_FENV"

PROBE = """#include <fenv.h>
#ifdef {s}
zene_probe_reached_fake_ucrt
#else
zene_probe_reached_next_fenv
#endif
""".format(s=SENTINEL)


def preprocess(compiler, include_dir, sdk_um, probe, defines):
    cmd = [compiler, "-E", "-P", "-w", "-x", "c++"]
    cmd += ["-D" + d for d in defines]
    cmd += ["-I", include_dir, "-I", sdk_um, probe]
    result = subprocess.run(cmd, capture_output=True, text=True)
    return cmd, result


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    compiler, include_dir = sys.argv[1], os.path.abspath(sys.argv[2])
    if not os.path.isfile(os.path.join(include_dir, "fenv.h")):
        print("FAIL: no fenv.h shim in", include_dir)
        return 1

    cases = [
        # (label, defines, expected marker)
        ("cl.exe (_MSC_VER, no __clang__)", ["_MSC_VER=1929"], "zene_probe_reached_fake_ucrt"),
        ("GCC/Clang (no _MSC_VER)", [], "zene_probe_reached_next_fenv"),
        ("clang-cl (_MSC_VER and __clang__)", ["_MSC_VER=1929", "__clang__=1"],
         "zene_probe_reached_next_fenv"),
    ]

    failures = 0
    with tempfile.TemporaryDirectory(prefix="zene-fenv-shim-") as tmp:
        # Windows SDK layout: Include/<ver>/um is on the search path (as it is for
        # cl.exe), the UCRT headers sit in its sibling Include/<ver>/ucrt.
        sdk = os.path.join(tmp, "Include", "10.0.19041.0")
        sdk_um = os.path.join(sdk, "um")
        os.makedirs(sdk_um)
        os.makedirs(os.path.join(sdk, "ucrt"))
        with open(os.path.join(sdk, "ucrt", "fenv.h"), "w") as f:
            f.write("#pragma once\n#define {} 1\n".format(SENTINEL))
        probe = os.path.join(tmp, "probe.cpp")
        with open(probe, "w") as f:
            f.write(PROBE)

        for label, defines, expected in cases:
            cmd, result = preprocess(compiler, include_dir, sdk_um, probe, defines)
            if result.returncode != 0:
                print("FAIL [{}]: preprocessing failed (exit {})".format(label, result.returncode))
                print("  command:", " ".join(cmd))
                print(result.stderr.strip()[:2000])
                failures += 1
                continue
            reached = [m for m in ("zene_probe_reached_fake_ucrt", "zene_probe_reached_next_fenv")
                       if m in result.stdout]
            if reached != [expected]:
                print("FAIL [{}]: expected {}, the probe saw {}".format(label, expected, reached))
                if expected.endswith("fake_ucrt"):
                    print("  include/fenv.h took #include_next under cl.exe's predefines; "
                          "MSVC rejects that directive (C1021)")
                failures += 1
            else:
                print("PASS [{}]: {}".format(label, expected))

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
