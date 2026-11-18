#!/usr/bin/env python3
"""warp-docs-agree.py - the warp docs must state the three warp.stretch modes, not the old blanket claim.

WHY (Q050-148 rework). The warp engine renders a clip's rate change in THREE modes
(`include/WarpMarkers.h`: `resample`, the pitch-moving default; `preserve_pitch`, the WSOLA
stretcher; `rubberband`, the optional Rubber Band R3 stretcher), but the warp docs once said
"warping changes pitch" and "Pitch-preserving stretch is not built" in four places, and
described `warp.stretch` as a two-mode command in three more. The engine tests (render
comparisons, the socket transcript) cannot see a DOCUMENT reverting, so this test is the
docs' own regression check. It is change-sensitive by construction: every required string
below is a sentence the docs-correction commit added, so restoring the docs to their
pre-correction revision fails it, and the forbidden-phrase half rejects the old blanket
claims outright.

What it checks, per document:

  1. the corrected three-mode statements are still present (exact phrases from the
     correction, so reverting the docs hunks fails);
  2. the per-algorithm caveats are still present (WSOLA: formants, transient smear, the
     ~345 Hz alignment floor, linear-interpolation SRC, one rate per audio period;
     Rubber Band: the optional dependency, the no-library refusal and the WSOLA fallback);
  3. the runnable reviewer example is still runnable: the docs name
     `tests/control-pitch-stretch-transcript.py`, that script exists, and it still drives
     `warp.stretch` with `preserve_pitch` and reads the mode back with `warp.list`;
  4. the old blanket claims do not appear as CURRENT claims anywhere in the checked set:
     "Pitch-preserving stretch is not built", "no pitch-preserving stretch", "there is no
     pitch-preserving time-stretch", "True pitch-preserving stretch is a DSP project",
     feature row 30's old "**to build**" state, the old "or **`preserve_pitch`**"
     two-mode `warp.stretch` descriptions, and the old "Resampling stretch, so it changes
     pitch" one-liner. An occurrence is allowed only where the file itself marks it
     historical - a dated `Corrected` / `no longer` / `was:` / `SUPERSEDED` note on the
     same line or within the following six lines - which is this repo's correction
     convention (see the `Corrected 2026-09-19 (task 691)` notes).

`docs/RELEASE-NOTES-v0.2.1-alpha.md` is deliberately NOT in the checked set: it is the
historical record of the 0.2.1 release, its resampling sentence was true of that release,
and the 0.3.0 release notes supersede it with the dated correction.

Usage:
    python3 tests/warp-docs-agree.py                # this repo's own docs
    python3 tests/warp-docs-agree.py --root <dir>   # another checkout of the same tree
Exit: 0 every check held; 1 any failure; 2 usage.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

# The correction's own sentences, per document, with the line breaks exactly as written.
# Reverting the docs hunks removes them, so the test fails after a reversal.
REQUIRED = {
    "docs/WARP.md": [
        "How a warp renders its rate is a per-clip choice, and the default changes pitch",
        "Two optional modes render the same\nrate with the pitch kept",
        "The default stretch is resampling. It changes pitch.",
        "Row 30 later built exactly that module",
        'mode="preserve_pitch"',
        'mode="rubberband"',
        "the optional Rubber Band R3\nstretcher",
    ],
    "docs/KNOWN-LIMITATIONS.md": [
        "The default warp render changes pitch, and two optional modes keep it",
        "the pitch moves with the\n    rate, so a 2× warp is an octave up",
        "routes the same rate through `AudioStretcher` (WSOLA): the clip lasts as long as the",
        "routes the rate through Rubber Band 3's R3 engine",
        "`WANT_RUBBERBAND`, default ON",
        "falls back to WSOLA for that pass",
        "no formant preservation",
        "no transient detection",
        "~345 Hz",
        "linear-interpolation SRC",
        "are refused for a\n  clip that renders linearly",
        "tests/control-pitch-stretch-transcript.py",
    ],
    "docs/PITCH-STRETCH.md": [
        "{ Resample = 0, PreservePitch = 1, RubberBand = 2 }",
        '`"resample"` \\| `"preserve_pitch"` \\| `"rubberband"`',
        "naming `preserve_pitch` as the\n  stretch to use instead",
        "Run it yourself (Q050-148)",
        "python3 tests/control-pitch-stretch-transcript.py build/zene",
        "No formant handling.",
        "below ~345 Hz",
        "Transients are not detected",
        "One rate per audio period",
        "refuses the mode (`refused`, not a silent WSOLA)",
    ],
    "docs/CAPABILITIES-0.3.0.md": [
        "or **`rubberband`**",
        "SampleClipRubberBandTest",
        "Appendix A below is the\ncatalogue of the capture its provenance block names and predates the third mode",
    ],
    "docs/FEATURE-LIST-0.3.0.md": [
        "corrected 2026-10-19, Q050-148",
        "unless the clip selects `preserve_pitch` (WSOLA) or `rubberband` (optional Rubber Band R3)",
        "| 30 | Pitch-preserving time-stretch | `warp.stretch` (modes `preserve_pitch`, `rubberband`) | **in the tree**",
        "SampleClipRubberBandTest",
    ],
    "docs/STATUS.md": [
        "The default stretch is resampling, so a warp changes pitch",
        "`preserve_pitch` (WSOLA) or `rubberband` (optional Rubber Band R3) to keep it",
    ],
    "docs/RELEASE-NOTES-v0.3.0-alpha.md": [
        "Corrected 2026-10-19 (Q050-148)",
        "`preserve_pitch` (WSOLA) or `rubberband` (optional Rubber Band R3)",
    ],
}

# The old blanket claims. An occurrence is a failure unless the file marks that line
# historical itself: a correction marker on the same line or within the following
# ALLOWANCE_LINES lines (the repo's *Corrected ...* / *(was: ...)* convention).
FORBIDDEN = [
    "Pitch-preserving stretch is not built",
    "no pitch-preserving stretch",
    "there is no pitch-preserving time-stretch",
    "True pitch-preserving stretch is a DSP project",
    "| 30 | Pitch-preserving time-stretch | none yet | **to build**",
    "or **`preserve_pitch`**",
    "Resampling stretch, so it changes pitch",
]
ALLOWANCE_LINES = 6
CORRECTION = re.compile(r"Corrected|no longer|was:|SUPERSEDED|landed since", re.I)
TRANSCRIPT = "tests/control-pitch-stretch-transcript.py"


def read_lines(root, relpath):
    with open(os.path.join(root, relpath), encoding="utf-8") as fh:
        return fh.read().splitlines()


def check_required(root, failures):
    for relpath, phrases in sorted(REQUIRED.items()):
        try:
            text = "\n".join(read_lines(root, relpath))
        except OSError as exc:
            print("FAIL %s: unreadable (%s)" % (relpath, exc))
            failures += 1
            continue
        for phrase in phrases:
            if phrase not in text:
                print("FAIL %s: the corrected statement is gone: %r" % (relpath, phrase))
                failures += 1
    return failures


def check_example(root, failures):
    # The example a reviewer runs must still exist and still drive warp.stretch: a
    # docs-only edit cannot silently orphan the docs' runnable command.
    script = os.path.join(root, TRANSCRIPT)
    if not os.path.isfile(script):
        print("FAIL %s: the docs' runnable example names a script that does not exist" % TRANSCRIPT)
        return failures + 1
    with open(script, encoding="utf-8") as fh:
        script_text = fh.read()
    for token in ("warp.stretch", "preserve_pitch", "warp.list"):
        if token not in script_text:
            print("FAIL %s: no longer drives %r, so the docs' example is stale" % (TRANSCRIPT, token))
            failures += 1
    return failures


def check_forbidden(root, failures):
    for relpath in sorted(REQUIRED):
        try:
            lines = read_lines(root, relpath)
        except OSError as exc:
            print("FAIL %s: unreadable (%s)" % (relpath, exc))
            failures += 1
            continue
        for i, line in enumerate(lines):
            for phrase in FORBIDDEN:
                if phrase.lower() not in line.lower():
                    continue
                window = "\n".join(lines[i:i + ALLOWANCE_LINES + 1])
                if CORRECTION.search(window):
                    print("ok   %s:%d: historical claim, marked corrected in place: %r"
                          % (relpath, i + 1, phrase))
                else:
                    print("FAIL %s:%d: the old blanket claim is back as a current statement: %r"
                          % (relpath, i + 1, phrase))
                    failures += 1
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=REPO, help="checkout root (default: this repo)")
    args = parser.parse_args()
    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print("warp-docs-agree: no such root: %s" % root)
        return 2
    failures = 0
    failures = check_required(root, failures)
    failures = check_example(root, failures)
    failures = check_forbidden(root, failures)
    checked = sum(len(v) for v in REQUIRED.values())
    if failures:
        print("warp-docs-agree: %d check(s) failed over %d docs / %d required strings"
              % (failures, len(REQUIRED), checked))
        return 1
    print("warp-docs-agree: %d required statements, %d blanket-claim sweeps and the runnable "
          "example all held over %d docs" % (checked, len(FORBIDDEN), len(REQUIRED)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
