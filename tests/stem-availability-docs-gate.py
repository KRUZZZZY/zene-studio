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
     is **local and offline**, describes the 44.1 kHz **output** with other input rates resampled (no
     stale "44100 Hz only" / "there is no resampler"), names `stem.tracks_create` as the four-track verb
     whose tracks are ONE undo step, and does NOT carry the known stale strings (`WANT_STEM_SPLIT='OFF'`,
     "Two features are compiled out of these builds").
  5. `docs/RELEASE-NOTES-v0.3.0-alpha.md` no longer says the feature is OFF in the default release
     configuration.
  6. `tests/CMakeLists.txt`'s registration comment no longer says the feature is OFF by default.
  7. `docs/FEATURE-LIST-0.3.0.md` row 26 is rewritten, not left with the stale audit status ("no
     `stem.*` group", `WANT_STEM_SPLIT OFF`) plus a contradicting correction: it is **in the tree**,
     names the eight `stem.*` ids and calls the processing **local and offline**.
  8. The active inline comments in `include/ControlRegistryGroups.h`,
     `src/core/ControlRegistryRegistrations.cpp`, `src/core/CMakeLists.txt`,
     `src/gui/CMakeLists.txt`, `tests/CMakeLists.txt`, `src/core/ControlCommandsStems.cpp`,
     `include/ControlStemSupport.h` and `include/StemSeparation/StemTypes.h` no longer call the feature
     opt-in/OFF, and the two headers no longer claim resampling is unimplemented.
  9. `docs/FEATURE-LIST-0.3.0.md` Table B row 1 ("In the tree but not drivable") no longer says stem
     separation has no `stem.*` group; the audit-era absence is labelled **superseded** and the `stem.*`
     ids that made it drivable are named (so row 26 and Table B agree).
 10. `docs/FEATURE-LIST-0.3.0.md` Reconciliation 5 no longer claims "the remaining six prefixes still
     return nothing": it accounts for the `stem.*` and `mastering.*` groups and names only `lufs.`
     and `scan.` as the prefixes still absent.
 11. The GUI's split action (src/gui/StemSplitController.cpp) drives the SAME registered commands the
     agent surface uses (`stem.job_start`, `stem.job_status`, `stem.job_cancel`, `stem.tracks_create`),
     and `stem.tracks_create` is a registered mutating command.

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


def check_configure_default(read, path, check):
    """(1) The configure-time default. Returns the option value, or None on a setup error."""
    cmake = read(path("CMakeLists.txt"))
    m = re.search(r"^OPTION\(WANT_STEM_SPLIT\b[^\n]*?\s+(ON|OFF)\)\s*$", cmake, re.M)
    if not m:
        print("FAIL could not find OPTION(WANT_STEM_SPLIT ... ON|OFF) in CMakeLists.txt")
        return None
    default = m.group(1)
    check(default == "ON",
          "CMakeLists.txt: WANT_STEM_SPLIT defaults to %s (expected ON)" % default)
    return default


def check_manifest(read, path, check, default):
    """(2) The release-honesty manifest agrees with the option, and claims presence."""
    manifest = read(path("tests/advertised-features.tsv"))
    row = None
    for line in manifest.splitlines():
        if line.startswith("stem-separation\t"):
            row = line.split("\t")
            break
    if row is None or len(row) < 5:
        check(False, "tests/advertised-features.tsv: no usable stem-separation row")
        return
    printed, required, claim = row[1], row[2], row[4]
    check(required == default,
          "advertised-features.tsv: stem-separation requires %s, option default is %s"
          % (required, default))
    check("is in the build" in claim,
          "advertised-features.tsv: stem-separation claim states the feature is in the build")


def check_readme(read, path, check):
    """(3) README says built in by default."""
    readme = read(path("README.md"))
    check("in the build by default" in readme,
          "README.md: stem separation is described as in the build by default")


def check_known_limitations(read, path, check):
    """(4) KNOWN-LIMITATIONS.md: correct, local/offline, resampling accurate, no stale strings."""
    kl = read(path("docs/KNOWN-LIMITATIONS.md"))
    check("built in and on by default" in kl,
          "docs/KNOWN-LIMITATIONS.md: states stem separation is built in and on by default")
    check("local and offline" in kl,
          "docs/KNOWN-LIMITATIONS.md: describes the processing as local and offline")
    check("WANT_STEM_SPLIT='OFF'" not in kl,
          "docs/KNOWN-LIMITATIONS.md: no stale WANT_STEM_SPLIT='OFF' claim")
    check("Two features are compiled out of these builds" not in kl,
          "docs/KNOWN-LIMITATIONS.md: no stale 'compiled out' claim for stem separation")
    # The rate limit is an OUTPUT rate, not an input one: other input rates are resampled.
    check("44100 Hz only" not in kl,
          "docs/KNOWN-LIMITATIONS.md: no stale '44100 Hz only' input claim")
    check("there is no resampler" not in kl,
          "docs/KNOWN-LIMITATIONS.md: no stale 'there is no resampler' claim")
    check("resampled to it offline" in kl,
          "docs/KNOWN-LIMITATIONS.md: states other input rates are resampled offline")
    check("stem.tracks_create" in kl,
          "docs/KNOWN-LIMITATIONS.md: names stem.tracks_create as the four-track verb")
    check("ONE undo step" in kl or "one undo step" in kl,
          "docs/KNOWN-LIMITATIONS.md: states the four tracks are one undo step")


def check_release_notes(read, path, check):
    """(5) The release notes no longer say it is OFF in the default configuration."""
    notes = read(path("docs/RELEASE-NOTES-v0.3.0-alpha.md"))
    check("The feature is OFF in the default release configuration" not in notes,
          "docs/RELEASE-NOTES-v0.3.0-alpha.md: no stale 'OFF in the default release configuration'")


def check_tests_comment(read, path, check):
    """(6) The test registration comment in tests/CMakeLists.txt."""
    tests_cmake = read(path("tests/CMakeLists.txt"))
    check("OFF by default: in a default build the ids do not exist" not in tests_cmake,
          "tests/CMakeLists.txt: the stem.* registration comment no longer says OFF by default")


def check_feature_list(read, path, check):
    """(7) docs/FEATURE-LIST-0.3.0.md row 26 states the current status, not the stale audit finding
    with a contradicting parenthetical correction."""
    featlist = read(path("docs/FEATURE-LIST-0.3.0.md"))
    frows = [ln for ln in featlist.splitlines() if ln.startswith("| 26 |")]
    check(len(frows) == 1,
          "docs/FEATURE-LIST-0.3.0.md: exactly one row 26 (found %d)" % len(frows))
    if len(frows) != 1:
        return
    frow = frows[0]
    check("WANT_STEM_SPLIT OFF" not in frow and "WANT_STEM_SPLIT='OFF'" not in frow,
          "docs/FEATURE-LIST-0.3.0.md row 26: no stale WANT_STEM_SPLIT OFF claim")
    check("no `stem.*` group" not in frow,
          "docs/FEATURE-LIST-0.3.0.md row 26: no stale 'no stem.* group' claim")
    check("stem.get_state" in frow,
          "docs/FEATURE-LIST-0.3.0.md row 26: names the eight stem.* ids")
    check("stem.tracks_create" in frow,
          "docs/FEATURE-LIST-0.3.0.md row 26: names stem.tracks_create")
    check("**in the tree**" in frow,
          "docs/FEATURE-LIST-0.3.0.md row 26: status is in the tree")
    check("local and offline" in frow,
          "docs/FEATURE-LIST-0.3.0.md row 26: describes the processing as local and offline")


def check_inline_comments(read, path, check):
    """(8) Every active inline comment that used to call the feature opt-in/OFF is corrected."""
    check("The feature is opt-in at configure time" not in
          read(path("include/ControlRegistryGroups.h")),
          "include/ControlRegistryGroups.h: stem.* comment no longer calls the feature opt-in")
    check("WANT_STEM_SPLIT=OFF" not in
          read(path("src/core/ControlRegistryRegistrations.cpp")),
          "src/core/ControlRegistryRegistrations.cpp: no stale WANT_STEM_SPLIT=OFF comment")
    check("Offline stem separation (opt-in" not in read(path("src/core/CMakeLists.txt")),
          "src/core/CMakeLists.txt: stem source comment no longer says opt-in")
    check("Offline stem separation GUI (opt-in" not in read(path("src/gui/CMakeLists.txt")),
          "src/gui/CMakeLists.txt: stem GUI comment no longer says opt-in")
    check("Offline stem separation tests (opt-in" not in read(path("tests/CMakeLists.txt")),
          "tests/CMakeLists.txt: stem tests comment no longer says opt-in")
    # The `stem.*` group's own source comments (round-4 review): the block comment in
    # ControlCommandsStems.cpp still claimed the feature was OFF by default, and
    # ControlStemSupport.h's header did too.
    commands = read(path("src/core/ControlCommandsStems.cpp"))
    check("THE FEATURE IS OFF IN THE DEFAULT RELEASE CONFIGURATION" not in commands,
          "src/core/ControlCommandsStems.cpp: no stale 'feature is OFF in the default' comment")
    check("defaults to OFF" not in commands,
          "src/core/ControlCommandsStems.cpp: no stale 'defaults to OFF' comment")
    check("THE FEATURE IS OFF IN THE RELEASE CONFIGURATION" not in
          read(path("include/ControlStemSupport.h")),
          "include/ControlStemSupport.h: no stale 'feature is OFF in the release' comment")
    check("resampling is not implemented" not in read(path("include/ControlStemSupport.h")),
          "include/ControlStemSupport.h: no stale 'resampling is not implemented' claim")
    check("Inputs at other rates are rejected" not in
          read(path("include/StemSeparation/StemTypes.h")),
          "include/StemSeparation/StemTypes.h: no stale 'inputs at other rates are rejected' claim")


def check_shared_command(read, path, check):
    """(11) The GUI's split action goes through the SAME registered commands the agent surface uses."""
    gui = read(path("src/gui/StemSplitController.cpp"))
    check("ControlRegistry::instance()" in gui,
          "src/gui/StemSplitController.cpp: the GUI drives commands through ControlRegistry")
    for command in ("stem.job_start", "stem.job_status", "stem.job_cancel", "stem.tracks_create"):
        check(command in gui,
              "src/gui/StemSplitController.cpp: the GUI invokes %s" % command)
    commands = read(path("src/core/ControlCommandsStems.cpp"))
    check("stem.tracks_create" in commands,
          "src/core/ControlCommandsStems.cpp: stem.tracks_create is registered")
    check("cmd.mutating = true" in commands,
          "src/core/ControlCommandsStems.cpp: stem.tracks_create declares itself mutating")


def check_table_b_row(read, path, check):
    """(9) Table B row 1 no longer claims stem separation is not drivable."""
    featlist = read(path("docs/FEATURE-LIST-0.3.0.md"))
    rows = [ln for ln in featlist.splitlines() if ln.startswith("| 1 | Stem separation ")]
    check(len(rows) == 1,
          "docs/FEATURE-LIST-0.3.0.md: exactly one Table B row 1 for stem separation (found %d)"
          % len(rows))
    if len(rows) != 1:
        return
    row = rows[0]
    check("no `stem.*` group" not in row,
          "docs/FEATURE-LIST-0.3.0.md Table B row 1: no stale 'no stem.* group' claim")
    check("superseded" in row,
          "docs/FEATURE-LIST-0.3.0.md Table B row 1: labels the audit-era absence as superseded")
    check("stem.job_start" in row,
          "docs/FEATURE-LIST-0.3.0.md Table B row 1: names the stem.* ids that made it drivable")
    check("stem.tracks_create" in row,
          "docs/FEATURE-LIST-0.3.0.md Table B row 1: names stem.tracks_create")


def check_reconciliation(read, path, check):
    """(10) Reconciliation 5 accounts for stem.* and mastering.*, not the stale six."""
    featlist = read(path("docs/FEATURE-LIST-0.3.0.md"))
    lines = featlist.splitlines()
    start = None
    for i, ln in enumerate(lines):
        if ln.startswith("5. **`stem.`"):
            start = i
            break
    check(start is not None,
          "docs/FEATURE-LIST-0.3.0.md: reconciliation 5 on the stem./mastering. class claim exists")
    if start is None:
        return
    para = [lines[start]]
    j = start + 1
    while j < len(lines) and lines[j].strip() and not re.match(r"^\d+\. ", lines[j]):
        para.append(lines[j])
        j += 1
    text = "\n".join(para)
    check("the remaining six prefixes still return nothing" not in text,
          "docs/FEATURE-LIST-0.3.0.md reconciliation 5: no stale 'remaining six prefixes' claim")
    check("`lufs`" in text and "`scan`" in text,
          "docs/FEATURE-LIST-0.3.0.md reconciliation 5: names lufs. and scan. as the still-absent prefixes")
    check("`stem`" in text and "`mastering`" in text,
          "docs/FEATURE-LIST-0.3.0.md reconciliation 5: accounts for the stem.* and mastering.* groups")


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

    default = check_configure_default(read, path, check)
    if default is None:
        return 2
    check_manifest(read, path, check, default)
    check_readme(read, path, check)
    check_known_limitations(read, path, check)
    check_release_notes(read, path, check)
    check_tests_comment(read, path, check)
    check_feature_list(read, path, check)
    check_inline_comments(read, path, check)
    check_shared_command(read, path, check)
    check_table_b_row(read, path, check)
    check_reconciliation(read, path, check)

    print()
    if failures:
        print("RESULT: FAIL - %d stem-availability doc claim(s) are stale" % len(failures))
        return 1
    print("RESULT: PASS - stem separation is documented as built in, local and ON by default")
    return 0


if __name__ == "__main__":
    sys.exit(main())
