#!/usr/bin/env python3
"""doc-pricing-model-gate.py - the README's no-tier / no-activation wording stays verified.

WHY (Q050-145). README.md documents the current pricing/activation state of the product:
no paid tier, no registration, no activation, and where a future policy change would be
announced. Two claims drifted once (round 1 of this queue unit):

  * the GPL's permission to charge ("gratis or for a fee") was presented as VERIFICATION
    that no paid tier exists - but LICENSE.txt itself says free software is about freedom,
    not price, so the license settles nothing about tiers; the no-tier statement must be
    grounded in the repository's own facts (the source search and the distribution facts),
  * the section promised that `--version` would report a future policy change, while
    main.cpp's printVersion() prints build metadata and a GPL notice only.

This gate repeats the verification the section claims, so the wording cannot drift from
what it cites:

  1. README.md: the "Free and activation model (current build)" section states the current
     no-tier / no-registration / no-activation state, is NOT grounded in the GPL's
     charging clause, carries no `--version` policy promise, notifies that the GPL
     permits charging, and names the announcement channel (the GitHub Releases page,
     per docs/RELEASING.md).
  2. LICENSE.txt: it really contains the "freedom, not price" clause and the "gratis or
     for a fee" redistribution clause, and it holds no product pricing / activation policy.
  3. The product source tree (the same first-party C/C++ scope tests/all-sources.txt
     derives, with the same vendored exclusions) contains no match for product-licensing
     patterns (licence key, activation code, serial number, purchase, paid tier, free
     trial, subscription plan ...), and the broader competitor words "tier"/"premium"
     appear only in the two documented competitor-comparison comments.

Usage:
    python3 tests/doc-pricing-model-gate.py            # this repo (run from anywhere)
Exit: 0 every claim verifies; 1 anything is off; 2 usage.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

README_PATH = os.path.join(REPO, "README.md")
LICENSE_PATH = os.path.join(REPO, "LICENSE.txt")

# The section of README.md this gate polices, and the licence section beside it.
SECTION_HEAD = "## Free and activation model (current build)"
LICENSE_HEAD = "## License and attribution"

failures = 0


def fail(what, detail):
    global failures
    failures += 1
    print("FAIL %s: %s" % (what, detail))


def section(text, heading):
    """The body of a markdown H2 section, from after its heading to the next H2."""
    at = text.find(heading)
    if at < 0:
        return None
    start = at + len(heading)
    nxt = text.find("\n## ", start)
    return text[start:nxt if nxt >= 0 else len(text)]


# Product-licensing patterns that must match NOTHING in the first-party C/C++ tree
# or in LICENSE.txt. Words kept out of this list on purpose (they have unrelated
# technical uses that round 1 measured): bare "activation" (Qt signal / plugin
# activation), bare "subscription" (MIDI port subscription), bare "tier" and
# "premium" (competitor comparisons, checked as an allowlisted set below).
NARROW = [
    r"licen[cs]e[-_ ]?key",
    r"activation[-_ ]?(code|key|fee)",
    r"enter[-_ ](?:your[-_ ])?(?:serial|licen[cs]e|activation)",
    r"serial[-_ ]number",
    r"product[-_ ]key",
    r"register[-_ ](?:this|your|the)[- _](?:product|copy|version|software)",
    r"purchase",
    r"paid[-_ ]?(tier|plan|version|edition)",
    r"free[-_ ]trial",
    r"upgrade[-_ ]to[-_ ](?:pro|premium)",
    r"subscription[-_ ](?:plan|fee|tier|managed|pricing)\b",
    r"\btrial[-_ ](period|version|mode)\b",
]
NARROW_RE = re.compile("|".join(NARROW), re.I)

# Broader competitor words; every match must be in this allowlist of documented
# competitor-comparison comments (file level, so line drift cannot break the gate).
TIER_ALLOWLIST = {
    "include/AudioInputPath.h",
    "include/MultiTrackRecorder.h",
}
TIER_RE = re.compile(r"\btier\b|\bpremium\b", re.I)

# The vendored exclusions, the same set tests/all-sources.txt declares on purpose.
EXCLUDE_RE = re.compile(
    r"^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/"
    r"|plugins/RnnoiseDenoiser/rnnoise/|plugins/LadspaEffect/(calf|cmt|swh|tap)/"
    r"|plugins/CarlaBase/carla/|plugins/FreeBoy/game-music-emu/"
    r"|plugins/MidiImport/portsmf/|plugins/OpulenZ/adplug/|plugins/Sid/resid/"
    r"|plugins/Xpressive/exprtk/|plugins/ZynAddSubFx/zynaddsubfx/)"
)


def product_sources():
    out = subprocess.run(
        ["git", "ls-files", "*.c", "*.cc", "*.cpp", "*.cxx", "*.h", "*.hpp"],
        cwd=REPO, capture_output=True, text=True, check=True,
    ).stdout.splitlines()
    return [p for p in out if not EXCLUDE_RE.match(p)]


def main():
    if not os.path.isfile(README_PATH) or not os.path.isfile(LICENSE_PATH):
        print("doc-pricing-model-gate: README.md / LICENSE.txt not found under %s" % REPO)
        return 2

    readme = open(README_PATH, encoding="utf-8").read()
    free = section(readme, SECTION_HEAD)
    lic = section(readme, LICENSE_HEAD)
    if free is None:
        fail("README.md", "no %r section" % SECTION_HEAD)
        free = ""

    # 1a. The current-state statement, grounded in the repository, not the license.
    for phrase in ["no paid tier", "no registration", "no activation",
                   "the repository itself, not in the license",
                   "a search of the product-owned"]:
        if phrase not in free:
            fail("README.md", "free-and-activation section lost %r" % phrase)
    if "gratis or for a fee" not in free or "settles nothing" not in free:
        fail("README.md", "free-and-activation section no longer tells the reader that "
                          "the GPL permits charging (\"gratis or for a fee\") and that "
                          "the license settles nothing about tiers")

    # 1b. No --version policy promise; no promise of a future policy.
    for bad in ["--version", "will report", "will be free", "will remain free",
                "promised by this document that"]:
        if bad.lower() in free.lower():
            fail("README.md", "free-and-activation section holds the unsupported claim %r"
                              % bad)

    # 1c. The announcement channel is named, from docs/RELEASING.md, as release notes
    #     published on the GitHub Releases page.
    for phrase in ["docs/RELEASING.md", "GitHub Releases page", "announced"]:
        if phrase not in free:
            fail("README.md", "free-and-activation section lost the announcement "
                              "channel %r" % phrase)

    # 1d. The plain license statement beside it still says GPL-2.0-or-later.
    if lic is None or "GPL-2.0-or-later" not in lic:
        fail("README.md", "license section lost the GPL-2.0-or-later statement")

    # 2. LICENSE.txt: the GPL clauses the README may factually cite, and no product
    #    pricing / activation policy inside it.
    gpl = open(LICENSE_PATH, encoding="utf-8").read()
    for clause in ["speak of free software",
                   "freedom, not",
                   "gratis or for a fee"]:
        if clause not in gpl:
            fail("LICENSE.txt", "the GPL clause %r is gone from LICENSE.txt" % clause)
    at = NARROW_RE.search(gpl)
    if at:
        fail("LICENSE.txt", 'product-licensing wording %r in the license text itself'
                            % at.group(0))

    # 3. The source search, the same scope the README section describes.
    paths = product_sources()
    print("doc-pricing-model-gate: searching %d product-owned C/C++ sources" % len(paths))
    licensing = []
    tiered = []
    for path in paths:
        with open(os.path.join(REPO, path), encoding="utf-8", errors="replace") as fh:
            for line in fh:
                if NARROW_RE.search(line):
                    licensing.append("%s: %s" % (path, line.rstrip()))
                elif TIER_RE.search(line) and path not in TIER_ALLOWLIST:
                    tiered.append("%s: %s" % (path, line.rstrip()))
    for hit in licensing:
        fail("sources", "product-licensing match: %s" % hit)
    for hit in tiered:
        fail("sources", "competitor word outside the allowlist %s: %s"
                        % (sorted(TIER_ALLOWLIST), hit))

    print("doc-pricing-model-gate: RESULT: %s" % ("PASS" if not failures else "FAIL (%d)"
                                                  % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
