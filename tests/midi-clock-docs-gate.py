#!/usr/bin/env python3
"""midi-clock-docs-gate.py - the docs describe BOTH MIDI Clock routes, and no current claim calls them socket-only.

WHY. R-workflow-117 ("MIDI clock input selection is socket-only") was true of the 0.3.0 engine-only release
and stopped being true twice: Edit > MIDI Clock > Clock Source picks the follower's input
(`src/gui/MidiClockMenu.cpp`, Q050-026), and Project tools > Sync > Follow MIDI Clock... opens the
`clock.slave_set` form whose fields include `source_port` and the integer `drift_bound_ms`
(`src/gui/LibraryCommandMenus.cpp`). `docs/KNOWN-LIMITATIONS.md` kept the old sentence ("Choosing the port
and the drift bound is still socket-only") and an older paragraph titled "MIDI clock is the engine and the
socket, and there is no interface for it"; `docs/FEATURE-LIST-0.3.0.md` and the 0.3.0 release notes carried
the same framing. A reader could not tell that both values are settable from the menus. Q050-308 is the
documentation-truth correction; this gate keeps it from drifting back.

WHAT IT CHECKS (file reads only; no network, no instance, no build):
  1. The named GUI locations are REAL. `src/gui/LibraryCommandMenus.cpp` adds the Sync entries
     "Send MIDI Clock..." / "Follow MIDI Clock..." against `clock.master_set` / `clock.slave_set` with
     `ask=true` (a form), and `src/gui/MidiClockMenu.cpp` builds the "Clock Source" submenu over
     `clock.slave_set`.
  2. The drift bound is the bound the docs state. `include/MidiClock.h` sets `MaxDriftBoundMs = 100.0`,
     `src/core/ControlCommandsClock.cpp` types `drift_bound_ms` as an integer from 0 to that maximum, and
     the committed command snapshot carries the same 0..100 integer for `clock.slave_set`.
  3. `docs/KNOWN-LIMITATIONS.md`'s MIDI-clock region describes BOTH routes as separate: the Edit menu's
     Clock Source picker, and the Project tools > Sync > Follow MIDI Clock... form carrying `source_port`
     and `drift_bound_ms` (0-100 ms), read back through `clock.get_state`, one recorded undo step - and it
     no longer says "Choosing the port and the drift bound is still socket-only" or "nothing in src/gui/
     offers a clock port selector".
  4. `data/manual/index.html` names both routes and the drift bound.
  5. A documentation sweep over every tracked .md/.html (dated logs and reports excluded) finds no
     UNMARKED statement that MIDI Clock source-port or drift-bound selection is socket-only: a hit is
     allowed only inside a record that says it is superseded/historical and carries a date.

`--self-test` is the gate's own red/green control: it copies every input into a throwaway root and proves
the gate fails (exit 1, naming the lost claim) when the stale sentence returns, when the Sync form route
is dropped from KNOWN-LIMITATIONS.md, when the documented drift bound no longer matches the schema, and
when the manual paragraph goes.

Usage:  python3 tests/midi-clock-docs-gate.py [--root DIR] [--self-test]
Exit:   0 every check holds; 1 a mismatch or a stale claim; 2 usage/setup error.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

KL = "docs/KNOWN-LIMITATIONS.md"
MANUAL = "data/manual/index.html"
MENUS = "src/gui/LibraryCommandMenus.cpp"
CLOCK_MENU = "src/gui/MidiClockMenu.cpp"
COMMANDS = "src/core/ControlCommandsClock.cpp"
HEADER = "include/MidiClock.h"
SNAPSHOT = "tools/mcp-zene-control/zene_control/commands_snapshot.json"

# The two Sync entries, as the source writes them: a form (ask=true) over the registered command.
SYNC_ENTRIES = (
    ('entry(sync, "Send MIDI Clock...", "clock.master_set", {}, true)', "Send MIDI Clock"),
    ('entry(sync, "Follow MIDI Clock...", "clock.slave_set", {}, true)', "Follow MIDI Clock"),
)

# The MIDI-clock region of KNOWN-LIMITATIONS.md: from the updated Edit-menu bullet to the next capture
# bullet. The claims below must hold inside it, not merely somewhere in the document.
KL_START = "- *(Updated 2026-09-30; corrected 2026-10-20, Q050-308.)*"
KL_END = "- *(Corrected 2026-09-30.)* **Edit \u25b8 Arm MIDI Capture"

# Statements that assert MIDI Clock port or drift-bound selection has no interface. Each is a failure
# unless the unit that carries it (its bullet or its table row) is marked as a dated record.
STALE = (
    r"socket-only",
    r"socket only",
    r"only on the socket",
    r"no interface for it",
    r"not from the interface",
    r"no port selector",
)
CLOCK = re.compile(r"\b(midi clock|clock port|clock source|drift[_ ]bound|source_port|clock\.slave_set)\b", re.I)
NEGATED = re.compile(r"\b(not|no longer|isn't|is not|never)\b[^.]{0,40}\bsocket-only\b"
                     r"|\bsocket-only\b[^.]{0,20}\bno longer\b", re.I)
RECORD = re.compile(r"\b(superseded|pre-menu record|historical|record of|the record)\b[^.]{0,160}\b20\d\d-\d\d-\d\d\b"
                    r"|\b20\d\d-\d\d-\d\d\b[^.]{0,160}\b(superseded|pre-menu record|historical|record of)\b",
                    re.I)

# Trees that hold dated run logs, reviews and reports rather than documentation claims.
EXCLUDED_DIRS = ("docs/bunny-fixes-2026-10-05/", "docs/qwen-fixes-2026-10/")
EXCLUDED_SUFFIX = ("-logs/", "logs/")


def read(root, rel):
    with open(os.path.join(root, rel), encoding="utf-8") as fh:
        return fh.read()


def flat(text):
    return re.sub(r"\s+", " ", text)


def region(text, start, end):
    i = text.find(start)
    if i < 0:
        return ""
    j = text.find(end, i + len(start))
    return text[i:j] if j >= 0 else text[i:]


def units(text):
    """The smallest records a claim lives in: a bullet, a table row, or a paragraph.

    A marker written on the bullet (the KNOWN-LIMITATIONS convention: "The bullet below is the
    pre-menu record") or in the table row therefore covers exactly the claim it annotates, not an
    unrelated date elsewhere in a long table."""
    out, cur = [], []

    def flush():
        if cur:
            out.append("\n".join(cur))
            cur.clear()

    for line in text.splitlines():
        opens = (line.lstrip().startswith("#") or re.match(r"^\s*[-*]\s", line) is not None
                 or (line.lstrip().startswith("|") and not (cur and cur[0].lstrip().startswith("|"))))
        if opens:
            flush()
            cur.append(line)
        elif not line.strip():
            flush()
        else:
            cur.append(line)
    flush()
    return out


# "still ... socket-only" asserts the present, so no historical marker can excuse it.
CURRENCY = re.compile(r"\bstill\b[^.]{0,60}\bsocket-only\b", re.I)


def excused(units_seq, index):
    """Whether the claim in unit @a index is annotated as a dated record.

    The KNOWN-LIMITATIONS convention puts the marker on the bullet itself, or says "the bullet below is
    the pre-menu record" - which annotates the NEXT unit, not the one carrying the sentence. A "still
    socket-only" claim is current by its own words and is never excused."""
    unit = flat(units_seq[index])
    if CURRENCY.search(unit):
        return False
    if RECORD.search(unit):
        return True
    if index > 0:
        previous = flat(units_seq[index - 1])
        if re.search(r"bullet below is the[^.]{0,40}record", previous, re.I) and RECORD.search(previous):
            return True
    return False


def stale_hits(text):
    """(unit, sentence, excused) for every socket-only-style claim about MIDI Clock."""
    found = []
    seq = units(text)
    for index, unit in enumerate(seq):
        body = flat(unit)
        for sentence in re.split(r"(?<=[.;])\s+|\s+\u2014\s+", body):
            if not CLOCK.search(sentence):
                continue
            if not any(re.search(phrase, sentence, re.I) for phrase in STALE):
                continue
            if NEGATED.search(sentence):
                continue
            found.append((body, sentence.strip(), excused(seq, index)))
    return found


def documented_files(root):
    """Every tracked .md/.html the sweep reads, minus dated run logs and review reports."""
    listed = subprocess.run(["git", "-C", root, "ls-files", "*.md", "*.html"],
                            capture_output=True, text=True).stdout.splitlines()
    docs = ["README.md", MANUAL]
    for rel in listed:
        if rel.startswith("docs/") or rel == MANUAL:
            if rel.startswith(EXCLUDED_DIRS) or any(part in rel for part in EXCLUDED_SUFFIX):
                continue
            docs.append(rel)
    return sorted(set(docs))


def check_code(root, ok):
    """1-2: the GUI location and the schema bound the docs name are real."""
    menus = read(root, MENUS)
    for call, label in SYNC_ENTRIES:
        ok(call in menus, "%s: the Sync menu opens the %s form (ask=true)" % (MENUS, label))
        ok('"%s"' % call.split('"')[3] in menus, "%s: %s drives a registered command" % (MENUS, label))

    clock_menu = read(root, CLOCK_MENU)
    ok('QMenu::tr("Clock Source")' in clock_menu,
       "%s: Edit > MIDI Clock > Clock Source exists" % CLOCK_MENU)
    ok('clock.slave_set' in clock_menu,
       "%s: the Clock Source picker drives clock.slave_set" % CLOCK_MENU)

    header = read(root, HEADER)
    ok(re.search(r"MaxDriftBoundMs\s*=\s*100\.0\s*;", header),
       "%s: MaxDriftBoundMs is 100.0" % HEADER)
    ok("integerProperty(0, static_cast<int>(MidiClockTracker::MaxDriftBoundMs))" in read(root, COMMANDS),
       "%s: drift_bound_ms is an integer from 0 to MaxDriftBoundMs" % COMMANDS)


def check_surface(root, ok):
    """2: the committed agent surface carries clock.slave_set with the same typed fields."""
    snapshot = json.loads(read(root, SNAPSHOT))
    slave = next((c for c in snapshot["commands"] if c["id"] == "clock.slave_set"), None)
    ok(slave is not None, "%s: clock.slave_set is on the agent surface" % SNAPSHOT)
    if slave is None:
        return
    props = slave.get("args_schema", {}).get("properties", {})
    bound = props.get("drift_bound_ms", {})
    ok(bound.get("type") == "integer" and bound.get("minimum") == 0 and bound.get("maximum") == 100,
       "%s: drift_bound_ms is an integer 0..100 (measured %r)" % (SNAPSHOT, bound))
    ok(props.get("source_port", {}).get("type") == "string",
       "%s: source_port is a string" % SNAPSHOT)


def check_known_limitations(root, ok):
    """3: the MIDI-clock region describes both routes and no longer calls either socket-only."""
    kl_clock = flat(region(read(root, KL), KL_START, KL_END))
    ok(kl_clock != "", "%s: has the MIDI-clock region this gate reads" % KL)
    for phrase in ("Edit \u25b8 MIDI Clock \u25b8 Clock Source",
                   "Project tools \u25b8 Sync \u25b8 Follow MIDI Clock...",
                   "source_port", "drift_bound_ms", "0", "100", "MaxDriftBoundMs",
                   "clock.get_state", "undo"):
        ok(phrase in kl_clock, "%s MIDI-clock region: describes %r" % (KL, phrase))
    for stale in ("Choosing the port and the drift bound is still socket-only",
                  "nothing in `src/gui/` offers a clock port selector",
                  "MIDI clock is the engine and the socket, and there is no interface for it"):
        ok(stale not in kl_clock, "%s MIDI-clock region: no stale claim %r" % (KL, stale[:60]))


def check_manual(root, ok):
    """4: the in-app manual names both routes and the drift bound."""
    manual = read(root, MANUAL)
    for phrase in ("Edit &gt; MIDI Clock", "Clock Source", "Project tools &gt; Sync &gt; Follow MIDI Clock",
                   "drift bound", "0-100 ms"):
        ok(phrase in manual, "%s: names %r" % (MANUAL, phrase))


def check_sweep(root, ok):
    """5: no UNMARKED socket-only claim about the MIDI clock survives anywhere in the documentation."""
    checked = 0
    for rel in documented_files(root):
        checked += 1
        for _body, sentence, marked in stale_hits(read(root, rel)):
            ok(marked, "%s: unmarked socket-only claim about MIDI Clock (%r)" % (rel, sentence[:110]))
    ok(checked > 0, "the documentation sweep read at least one document")


def run_checks(root, check):
    failures = []

    def ok(cond, msg):
        check(cond, msg)
        if not cond:
            failures.append(msg)

    for part in (check_code, check_surface, check_known_limitations, check_manual, check_sweep):
        part(root, ok)
    return failures


def mirror(root, dest):
    """Copy every input into a throwaway tree: no symlinks, so the self-test is privilege-free."""
    rels = [KL, MANUAL, MENUS, CLOCK_MENU, COMMANDS, HEADER, SNAPSHOT]
    for rel in rels + documented_files(root):
        src = os.path.join(root, rel)
        dst = os.path.join(dest, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if os.path.lexists(dst):
            os.remove(dst)
        shutil.copy2(src, dst)


def rewrite(dest, rel, text):
    with open(os.path.join(dest, rel), "w", encoding="utf-8") as fh:
        fh.write(text)


def selftest_case(root, rel, mutate, expect):
    """Mutate one input, run the gate in a mirrored root, require exit 1 naming @a expect."""
    tmp = tempfile.mkdtemp(prefix="midi-clock-docs-selftest-")
    try:
        mirror(root, tmp)
        original = read(tmp, rel)
        changed = mutate(original)
        if changed == original:
            return False, "the mutation did not change %s" % rel
        rewrite(tmp, rel, changed)
        proc = subprocess.run([sys.executable, os.path.abspath(__file__), "--root", tmp],
                              capture_output=True, text=True)
        out = proc.stdout + proc.stderr
        named = any(line.startswith("FAIL") and expect in line for line in out.splitlines())
        return proc.returncode == 1 and named, "gate exit %d, names %r=%s" % (proc.returncode, expect, named)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def _widen_drift_bound(text):
    """Move only clock.slave_set's drift-bound maximum, leaving every other command intact."""
    import json as _json
    doc = _json.loads(text)
    for command in doc.get("commands", []):
        if command.get("id") == "clock.slave_set":
            command.setdefault("args_schema", {}).setdefault("properties", {})["drift_bound_ms"]["maximum"] = 1000
    return _json.dumps(doc, indent=1) + "\n"


def _drop_sync_route(text):
    """Remove every naming of the Project tools > Sync route, across soft line wraps."""
    return re.sub(r"Project tools \u25b8 Sync(?:\s*\u25b8\s*Follow\s+MIDI Clock\.\.\.)?", "Sync", text)


def self_test(root):
    print("SELF-TEST: each corrected claim, put back wrong, must fail the gate")
    kl = read(root, KL)
    manual = read(root, MANUAL)
    snapshot_text = read(root, SNAPSHOT)
    cases = (
        ("the stale socket-only sentence returns", KL, lambda t: t.replace(
            "Neither the port nor the bound is socket-only.",
            "Choosing the port and the drift bound is still socket-only.", 1),
         "socket-only"),
        ("the Sync form route is dropped", KL, _drop_sync_route, "Project tools"),
        ("the documented drift bound no longer matches the schema", SNAPSHOT,
         _widen_drift_bound, "drift_bound_ms is an integer 0..100"),
        ("the manual stops naming the Sync form", MANUAL, lambda t: t.replace(
            "Project tools &gt; Sync &gt; Follow MIDI Clock...", "Sync", 1),
         "Project tools &gt; Sync &gt; Follow MIDI Clock"),
    )
    failures = 0
    for name, rel, mutate, expect in cases:
        good, detail = selftest_case(root, rel, mutate, expect)
        print("  %s  %s - %s" % ("PASS" if good else "FAIL", name, detail))
        failures += 0 if good else 1
    print("SELF-TEST %s" % ("PASS" if not failures else "FAIL"))
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=REPO)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print("FAIL no such root: %s" % root)
        return 2
    if args.self_test:
        return self_test(root)

    def report(cond, msg):
        print("%s %s" % ("ok  " if cond else "FAIL", msg))

    failures = run_checks(root, report)
    print("RESULT: %s" % ("PASS" if not failures else "FAIL (%d)" % len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
