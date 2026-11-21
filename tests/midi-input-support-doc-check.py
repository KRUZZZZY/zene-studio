#!/usr/bin/env python3
"""midi-input-support-doc-check.py - docs/MIDI-INPUT-SUPPORT.md must state the UMP position.

WHY (Q050-356). That page is the public answer to "does Zene take MIDI 2.0 / UMP input?". The answer
is "not yet supported", the planned scope is undecided ('not defined'), the platform table names every
backend family, and every path:line citation must still point at a file that has that many lines.
The MIDI 1-only statement, the backend-to-guard mapping and the manual paragraph must also stay in
step with the source, so a regression in any of them fails here.

Checks:
  (a) the file exists;
  (b) the four H2 headings are present;
  (c) ## Status says 'not yet supported' and mentions UMP;
  (d) no line asserts shipped UMP input ("UMP input is/are supported|available");
  (e) ## Planned: UMP input says 'not defined';
  (f) ## Supported-platform boundary names ALSA, JACK, WinMM and macOS/Apple;
  (g) every citation (src|include|docs|CMakeLists)...:N resolves to a file with at least N lines;
  (h) MIDI 1 only: ## Status says 'input is MIDI 1 only'; ## Current input path (MIDI 1) names both
      paths and 'no UMP message types' and does not mention MIDI 2;
  (i) input-path membership: the Current-input section names and cites the raw feeders
      (MidiAlsaRaw, MidiOss, MidiSndio, MidiJack) and the decoded clients (MidiAlsaSeq, MidiWinMM,
      MidiApple); each raw feeder .cpp really calls parseData(;
  (j) backend/platform mapping: each backend row of ## Supported-platform boundary names its guard
      macro, the real src/core/midi/<Class>.cpp has '#ifdef <macro>', and the forced-off or platform
      words that the row must carry are present;
  (k) manual: data/manual/index.html has a <p> mentioning UMP that says 'not yet supported', 'MIDI 1'
      and 'docs/MIDI-INPUT-SUPPORT.md'.

Usage:
    python3 tests/midi-input-support-doc-check.py [doc] [manual]
        defaults: docs/MIDI-INPUT-SUPPORT.md and data/manual/index.html (both under the repo root)
    A relative [doc] or [manual] is taken from the current directory. Citations resolve from the repo root.
Exit: 0 every check passes; 1 at least one check failed (one line per failure); 2 usage.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DOC = os.path.join(ROOT, "docs", "MIDI-INPUT-SUPPORT.md")
DEFAULT_MANUAL = os.path.join(ROOT, "data", "manual", "index.html")

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

CURRENT_PATH = "## Current input path (MIDI 1)"
PATH_PHRASES = ["Byte-stream path", "Decoded-event path", "no UMP message types"]
RAW_FEEDERS = ["MidiAlsaRaw", "MidiOss", "MidiSndio", "MidiJack"]
DECODED_CLIENTS = ["MidiAlsaSeq", "MidiWinMM", "MidiApple"]

# (backend class, guard macro, source file must contain '#ifdef <macro>')
PLATFORM_MAP = [
    ("MidiAlsaSeq", "LMMS_HAVE_ALSA"),
    ("MidiAlsaRaw", "LMMS_HAVE_ALSA"),
    ("MidiJack", "LMMS_HAVE_JACK"),
    ("MidiOss", "LMMS_HAVE_OSS"),
    ("MidiSndio", "LMMS_HAVE_SNDIO"),
    ("MidiWinMM", "LMMS_HAVE_WINMM"),
    ("MidiApple", "LMMS_BUILD_APPLE"),
]
# words each backend row must carry (forced-off platforms, or the platform name)
ROW_TERMS = {
    "MidiAlsaSeq": ["Win32", "Apple"],
    "MidiAlsaRaw": ["Win32", "Apple"],
    "MidiWinMM": ["Win32"],
    "MidiApple": ["macOS", "CoreMIDI"],
}

MANUAL_PHRASES = ["not yet supported", "MIDI 1", "docs/MIDI-INPUT-SUPPORT.md"]
MANUAL_PARAGRAPH = re.compile(r"<p\b[^>]*>(.*?)</p>", re.S | re.I)


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


def read_text(path):
    with open(path, encoding="utf-8", errors="replace") as handle:
        return handle.read()


def check_headings(secs):
    return [f"(b) missing heading: {title}" for title in REQUIRED_TITLES if title not in secs]


def check_status(secs):
    status = secs.get("## Status", "")
    failures = []
    if not re.search(r"(?i)not yet supported", status):
        failures.append("(c) ## Status does not say 'not yet supported'")
    if "UMP" not in status:
        failures.append("(c) ## Status does not mention UMP")
    return failures


def check_no_shipped_claim(lines):
    return [f"(d) line {number} asserts shipped UMP input: {line.strip()}"
            for number, line in enumerate(lines, 1) if UNSHIPPED_CLAIM.search(line)]


def check_planned(secs):
    planned = secs.get("## Planned: UMP input", "")
    if not re.search(r"(?i)not defined", planned):
        return ["(e) ## Planned: UMP input does not say 'not defined'"]
    return []


def check_platforms(secs):
    platform = secs.get("## Supported-platform boundary", "")
    return [f"(f) ## Supported-platform boundary does not name {label}"
            for label, pattern in PLATFORM_NAMES if not re.search(pattern, platform)]


def citation_failure(number, rel, need):
    """One failure line for a path:line citation that does not resolve, or None."""
    path = os.path.join(ROOT, rel)
    if not os.path.isfile(path):
        return f"(g) line {number}: {rel}:{need} - file not found"
    have = count_lines(path)
    if have < need:
        return f"(g) line {number}: {rel}:{need} - file has only {have} lines"
    return None


def check_citations(lines):
    failures = []
    for number, line in enumerate(lines, 1):
        for match in CITATION.finditer(line):
            problem = citation_failure(number, match.group(1), int(match.group(2)))
            if problem:
                failures.append(problem)
    return failures


def check_midi1_status(secs):
    if not re.search(r"(?i)input is MIDI 1 only", secs.get("## Status", "")):
        return ["(h) ## Status does not say 'input is MIDI 1 only'"]
    return []


def check_midi1_path(secs):
    current = secs.get(CURRENT_PATH, "")
    failures = [f"(h) {CURRENT_PATH} does not contain '{phrase}'"
                for phrase in PATH_PHRASES if not re.search(re.escape(phrase), current, re.I)]
    if re.search(r"MIDI 2", current):
        failures.append(f"(h) {CURRENT_PATH} mentions MIDI 2 (it must stay MIDI 1 only)")
    return failures


def check_feeders_named(secs):
    current = secs.get(CURRENT_PATH, "")
    failures = []
    for name in RAW_FEEDERS + DECODED_CLIENTS:
        if name not in current:
            failures.append(f"(i) {CURRENT_PATH} does not name {name}")
        if f"src/core/midi/{name}.cpp" not in current:
            failures.append(f"(i) {CURRENT_PATH} does not cite src/core/midi/{name}.cpp")
    return failures


def check_feeder_sources():
    failures = []
    for name in RAW_FEEDERS:
        path = os.path.join(ROOT, "src", "core", "midi", name + ".cpp")
        if not os.path.isfile(path):
            failures.append(f"(i) raw feeder source not found: src/core/midi/{name}.cpp")
        elif "parseData(" not in read_text(path):
            failures.append(f"(i) src/core/midi/{name}.cpp does not call parseData(")
    return failures


def table_row(platform, cls):
    """The platform-table row whose first cell names `cls`, or None."""
    token = f"`{cls}`"
    for line in platform.splitlines():
        cells = line.split("|")
        if line.startswith("|") and len(cells) > 1 and token in cells[1]:
            return line
    return None


def guard_cell(row):
    cells = row.split("|")
    return cells[4] if len(cells) > 4 else ""


def check_row_mapping(platform, cls, macro):
    row = table_row(platform, cls)
    if row is None:
        return [f"(j) ## Supported-platform boundary has no row for {cls}"]
    failures = []
    if f"`{macro}`" not in guard_cell(row):
        failures.append(f"(j) {cls} row guard cell does not name {macro}")
    path = os.path.join(ROOT, "src", "core", "midi", cls + ".cpp")
    if not os.path.isfile(path):
        failures.append(f"(j) source not found: src/core/midi/{cls}.cpp")
    elif f"#ifdef {macro}" not in read_text(path):
        failures.append(f"(j) src/core/midi/{cls}.cpp has no '#ifdef {macro}'")
    return failures


def check_row_terms(platform, cls):
    row = table_row(platform, cls)
    if row is None:
        return []  # check_row_mapping already reports the missing row
    return [f"(j) {cls} row does not mention {term}"
            for term in ROW_TERMS.get(cls, []) if term not in row]


def check_platform_mapping(secs):
    platform = secs.get("## Supported-platform boundary", "")
    failures = []
    for cls, macro in PLATFORM_MAP:
        failures += check_row_mapping(platform, cls, macro)
        failures += check_row_terms(platform, cls)
    return failures


def check_manual(manual_text):
    paragraphs = [body for body in MANUAL_PARAGRAPH.findall(manual_text) if "UMP" in body]
    if not paragraphs:
        return ["(k) manual has no <p> paragraph that mentions UMP"]
    best = max(paragraphs, key=lambda body: sum(phrase in body for phrase in MANUAL_PHRASES))
    return [f"(k) manual UMP paragraph does not say '{phrase}'"
            for phrase in MANUAL_PHRASES if phrase not in best]


def run_checks(lines, manual_text):
    secs = sections(lines)
    return (check_headings(secs) + check_status(secs) + check_no_shipped_claim(lines)
            + check_planned(secs) + check_platforms(secs) + check_citations(lines)
            + check_midi1_status(secs) + check_midi1_path(secs)
            + check_feeders_named(secs) + check_feeder_sources()
            + check_platform_mapping(secs) + check_manual(manual_text))


def main(argv):
    if len(argv) > 3:
        print("usage: midi-input-support-doc-check.py [doc] [manual]", file=sys.stderr)
        return 2
    doc = os.path.abspath(argv[1]) if len(argv) >= 2 else DEFAULT_DOC
    manual = os.path.abspath(argv[2]) if len(argv) == 3 else DEFAULT_MANUAL
    if not os.path.isfile(doc):
        print(f"FAIL (a): doc not found: {doc}")
        return 1
    if not os.path.isfile(manual):
        print(f"FAIL (k): manual not found: {manual}")
        return 1

    lines = read_text(doc).splitlines()
    failures = run_checks(lines, read_text(manual))

    if failures:
        for failure in failures:
            print(f"FAIL {failure}")
        print(f"midi-input-support-doc-check: {len(failures)} failure(s) in {doc}")
        return 1
    print(f"midi-input-support-doc-check: OK ({doc}, {manual})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
