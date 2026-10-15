#!/usr/bin/env python3
"""stem-availability-docs-gate.py - the docs say stem separation is built in, local, and ON by default.

WHY. R-platform-26 ("local stem processing can preserve privacy and work offline") is a praise item
whose documentation was wrong in the release-truth direction: `docs/KNOWN-LIMITATIONS.md` said
offline HTDemucs stem separation was "compiled out of these builds" and reported
`WANT_STEM_SPLIT='OFF'`, while owner decision 14 had already made `WANT_STEM_SPLIT` default ON and
the release-honesty manifest row require ON. A release that advertises a feature it ships as absent
is the v0.1.0-alpha defect this project's release-honesty gate exists to prevent; this gate keeps the
DOCUMENT side of the same fact from drifting again.

WHAT IT CHECKS (all file reads, no network, no instance):
  1. `CMakeLists.txt` declares `OPTION(WANT_STEM_SPLIT ... ON)` - the supported default is ON.
  2. `tests/advertised-features.tsv`'s `stem-separation` row requires the SAME value the option
     defaults to, and its claim says the feature is IN the build (not absent). The manifest is the
     single list `tests/release-honesty-gate.sh` reads, so this ties the doc claim to the build claim.
  3. `README.md` describes the feature as "in the build by default", not as absent.
  4. `docs/KNOWN-LIMITATIONS.md` states the feature is built in and on by default, says the processing
     is **local and offline**, and does NOT carry the known stale strings (`WANT_STEM_SPLIT='OFF'`,
     "Two features are compiled out of these builds").
  5. `docs/RELEASE-NOTES-v0.3.0-alpha.md` no longer says the feature is OFF in the default release
     configuration.
  6. `tests/CMakeLists.txt`'s registration comment no longer says the feature is OFF by default.

Usage:  python3 tests/stem-availability-docs-gate.py [--root DIR]
Exit:   0 every check holds; 1 a mismatch; 2 usage/setup error.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=os.path.dirname(HERE))
    args = parser.parse_args()
    root = os.path.abspath(args.root)

    failures = []

    def check(ok, msg):
        print(("PASS " if ok else "FAIL ") + msg)
        if not ok:
            failures.append(msg)

    def path(rel):
        return os.path.join(root, rel)

    # 1. The configure-time default.
    cmake = read(path("CMakeLists.txt"))
    m = re.search(r"^OPTION\(WANT_STEM_SPLIT\b[^\n]*?\s+(ON|OFF)\)\s*$",
                  cmake, re.M)
    if not m:
        print("FAIL could not find OPTION(WANT_STEM_SPLIT ... ON|OFF) in CMakeLists.txt")
        return 2
    default = m.group(1)
    check(default == "ON",
          "CMakeLists.txt: WANT_STEM_SPLIT defaults to %s (expected ON)" % default)

    # 2. The release-honesty manifest agrees with the option, and claims presence.
    manifest = read(path("tests/advertised-features.tsv"))
    row = None
    for line in manifest.splitlines():
        if line.startswith("stem-separation\t"):
            row = line.split("\t")
            break
    if row is None or len(row) < 5:
        check(False, "tests/advertised-features.tsv: no usable stem-separation row")
    else:
        printed, required, claim = row[1], row[2], row[4]
        check(required == default,
              "advertised-features.tsv: stem-separation requires %s, option default is %s"
              % (required, default))
        check("is in the build" in claim,
              "advertised-features.tsv: stem-separation claim states the feature is in the build")

    # 3. README says built in by default.
    readme = read(path("README.md"))
    check("in the build by default" in readme,
          "README.md: stem separation is described as in the build by default")

    # 4. KNOWN-LIMITATIONS.md: correct, local/offline, no stale strings.
    kl = read(path("docs/KNOWN-LIMITATIONS.md"))
    check("built in and on by default" in kl,
          "docs/KNOWN-LIMITATIONS.md: states stem separation is built in and on by default")
    check("local and offline" in kl,
          "docs/KNOWN-LIMITATIONS.md: describes the processing as local and offline")
    check("WANT_STEM_SPLIT='OFF'" not in kl,
          "docs/KNOWN-LIMITATIONS.md: no stale WANT_STEM_SPLIT='OFF' claim")
    check("Two features are compiled out of these builds" not in kl,
          "docs/KNOWN-LIMITATIONS.md: no stale 'compiled out' claim for stem separation")

    # 5. The release notes no longer say it is OFF in the default configuration.
    notes = read(path("docs/RELEASE-NOTES-v0.3.0-alpha.md"))
    check("The feature is OFF in the default release configuration" not in notes,
          "docs/RELEASE-NOTES-v0.3.0-alpha.md: no stale 'OFF in the default release configuration'")

    # 6. The test registration comment in tests/CMakeLists.txt.
    tests_cmake = read(path("tests/CMakeLists.txt"))
    check("OFF by default: in a default build the ids do not exist" not in tests_cmake,
          "tests/CMakeLists.txt: the stem.* registration comment no longer says OFF by default")

    print()
    if failures:
        print("RESULT: FAIL - %d stem-availability doc claim(s) are stale" % len(failures))
        return 1
    print("RESULT: PASS - stem separation is documented as built in, local and ON by default")
    return 0


if __name__ == "__main__":
    sys.exit(main())
