#!/usr/bin/env python3
"""midi-input-support-doc-check.py - docs/MIDI-INPUT-SUPPORT.md must state the UMP position.

WHY (Q050-356). That page is the public answer to "does Zene take MIDI 2.0 / UMP input?". The answer
is "not yet supported", the planned scope is undecided ('not defined'), the platform table names every
backend family, and every path:line citation must still point at a file that has that many lines.

Checks:
  (a) the file exists;
  (b) the four H2 headings are present;
  (c) ## Status says 'not yet supported' and mentions UMP;
  (d) no line asserts shipped UMP input ("UMP input is/are supported|available");
  (e) ## Planned: UMP input says 'not defined';
  (f) ## Supported-platform boundary names ALSA, JACK, WinMM and macOS/Apple;
  (g) every citation (src|include|docs|CMakeLists)...:N resolves to a file with at least N lines.

Usage:
    python3 tests/midi-input-support-doc-check.py [doc]   # default docs/MIDI-INPUT-SUPPORT.md
    A relative [doc] is taken from the current directory. Citations resolve from the repo root.
Exit: 0 every check passes; 1 at least one check failed (one line per failure); 2 usage.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DOC = os.path.join(ROOT, "docs", "MIDI-INPUT-SUPPORT.md")

REQUIRED_TITLES = [
    "## Status",
    "## Current input path (MIDI 1)",
    "## Planned: UMP input",
    "## Supported-platform boundary",
]
PLATFORM_NAMES = [
    ("ALSA", r"ALSA"),
    ("JACK", r"JACK"),
    ("WinMM", r"WinMM"),
    ("macOS/Apple", r"(?i)macOS|Apple"),
]
UNSHIPPED_CLAIM = re.compile(r"(?i)\bUMP input (is|are) (supported|available)\b")
CITATION = re.compile(r"((?:src|include|docs|CMakeLists)[\w/.\-]*):(\d+)")


def sections(lines):
    """Map each H2 heading line to the text under it, up to the next H2."""
    out = {}
    current = None
    for line in lines:
        if line.startswith("## "):
            current = line.strip()
            out[current] = []
        elif current is not None:
            out[current].append(line)
    return {title: "\n".join(body) for title, body in out.items()}


def count_lines(path):
    with open(path, encoding="utf-8", errors="replace") as handle:
        return sum(1 for _ in handle)


def main(argv):
    if len(argv) > 2:
        print("usage: midi-input-support-doc-check.py [doc]", file=sys.stderr)
        return 2
    doc = os.path.abspath(argv[1]) if len(argv) == 2 else DEFAULT_DOC
    if not os.path.isfile(doc):
        print(f"FAIL (a): doc not found: {doc}")
        return 1

    with open(doc, encoding="utf-8", errors="replace") as handle:
        lines = handle.read().splitlines()
    secs = sections(lines)
    failures = []

    for title in REQUIRED_TITLES:
        if title not in secs:
            failures.append(f"(b) missing heading: {title}")

    status = secs.get("## Status", "")
    if not re.search(r"(?i)not yet supported", status):
        failures.append("(c) ## Status does not say 'not yet supported'")
    if "UMP" not in status:
        failures.append("(c) ## Status does not mention UMP")

    for number, line in enumerate(lines, 1):
        if UNSHIPPED_CLAIM.search(line):
            failures.append(f"(d) line {number} asserts shipped UMP input: {line.strip()}")

    planned = secs.get("## Planned: UMP input", "")
    if not re.search(r"(?i)not defined", planned):
        failures.append("(e) ## Planned: UMP input does not say 'not defined'")

    platform = secs.get("## Supported-platform boundary", "")
    for label, pattern in PLATFORM_NAMES:
        if not re.search(pattern, platform):
            failures.append(f"(f) ## Supported-platform boundary does not name {label}")

    for number, line in enumerate(lines, 1):
        for match in CITATION.finditer(line):
            rel, need = match.group(1), int(match.group(2))
            path = os.path.join(ROOT, rel)
            if not os.path.isfile(path):
                failures.append(f"(g) line {number}: {rel}:{need} - file not found")
                continue
            have = count_lines(path)
            if have < need:
                failures.append(f"(g) line {number}: {rel}:{need} - file has only {have} lines")

    if failures:
        for failure in failures:
            print(f"FAIL {failure}")
        print(f"midi-input-support-doc-check: {len(failures)} failure(s) in {doc}")
        return 1
    print(f"midi-input-support-doc-check: OK ({doc})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
