#!/usr/bin/env python3
"""Generate the MIDI-depth render fixtures from the untouched tutorial project.

Everything is derived from data/projects/tutorials/editing_note_volumes.mmp by a
literal, verifiable transformation, so the fixtures cannot drift from the
project they claim to be a copy of. baseline.mmp is a byte-for-byte copy; every
other fixture differs from it only in the ways named below.

    python3 tests/data/midi-depth/gen-fixtures.py <repo root>

The generator refuses to write a fixture in which a note element carries the
same attribute twice (an earlier revision of this script produced exactly that,
and QDom's choice between the duplicates would have made the fixture ambiguous).
"""

import hashlib
import pathlib
import re
import sys

REPO = pathlib.Path(sys.argv[1]).resolve()
SOURCE = REPO / "data/projects/tutorials/editing_note_volumes.mmp"
OUT = REPO / "tests/data/midi-depth"

baseline = SOURCE.read_text()

NOTE_RE = re.compile(r"<note ([^>/]*)/>")
HEAD_TAIL = 'mastervol="100" timesig_numerator="4"/>'


def with_seed(text, seed):
    assert text.count(HEAD_TAIL) == 1, "the head element moved"
    return text.replace(HEAD_TAIL, f'mastervol="100" timesig_numerator="4" midiseed="{seed}"/>')


def add_attr(text, name, value):
    return NOTE_RE.sub(lambda m: f'<note {m.group(1).strip()} {name}="{value}"/>', text)


def check_no_duplicate_attributes(text, name):
    duplicates = 0
    for index, match in enumerate(NOTE_RE.finditer(text)):
        names = re.findall(r'([A-Za-z_][A-Za-z0-9_]*)="', match.group(1))
        if len(names) != len(set(names)):
            print(f"ERROR: {name}: note {index + 1} has duplicate attributes: {names}")
            duplicates += 1
    assert duplicates == 0, f"{name} has {duplicates} note(s) with duplicate attributes"


def write(name, text):
    check_no_duplicate_attributes(text, name)
    path = OUT / name
    path.write_text(text)
    digest = hashlib.sha256(text.encode()).hexdigest()
    print(f"{name}\tsha256={digest}\tbytes={len(text)}")


assert baseline == SOURCE.read_text()
OUT.mkdir(parents=True, exist_ok=True)

# 1. the untouched project: a byte-for-byte copy (the behaviour-preservation
#    reference)
write("baseline.mmp", baseline)

# 2. note probability, seed A; 3. the same project with a different seed
seed_a = add_attr(with_seed(baseline, 1234), "prob", "0.5")
write("prob-seed1.mmp", seed_a)
write("prob-seed2.mmp", add_attr(with_seed(baseline, 5678), "prob", "0.5"))

# 4. sensitivity control: seed A with exactly one note's velocity moved by one
#    step, so the render comparator has to notice a one-value change
assert seed_a.count('vol="54"') == 1, "the note the control moves is not unique"
write("prob-seed1-vol.mmp", seed_a.replace('vol="54"', 'vol="55"'))

# 5. an explicit probability of 1 on every note: the documented default written
#    out. This has to render exactly like the untouched baseline.
write("prob-all-one.mmp", add_attr(with_seed(baseline, 1234), "prob", "1"))

# 6./7. velocity jitter, two seeds
write("veljit-seed1.mmp", add_attr(with_seed(baseline, 1234), "veljit", "0.5"))
write("veljit-seed2.mmp", add_attr(with_seed(baseline, 5678), "veljit", "0.5"))

print("baseline.mmp is an exact copy of", SOURCE.relative_to(REPO))
