#!/usr/bin/env python3
"""rev_report.py - name WHICH SIDE of a three-way merge changed each object,
read from the model's own `rev`/`writer` attributes (SPEC-ARCH-4 1.5 R4,
ARCH-4 S3): the stated properties a merge works from "instead of rebuilding the
answer from XML diffs".

usage:
  rev_report.py --base B --ours O --theirs T
                [--ours-label ours] [--theirs-label theirs]

For every object the three files disagree about, one line:

  track id=2            changed by ours (writer=7f3a91c20b41, rev 0 -> 1)
  track id=5            changed on BOTH sides: ours (writer=..., rev 0 -> 1) /
                        theirs (writer=..., rev 0 -> 1)   <- stated, not guessed

Identity is (tag, stable id) when the object has an id (SPEC-stable-ids), NOT
the mutable name: a rename is a CHANGE to one object, not a delete plus an
add. Elements without an id fall back to mmpz-git's own sibling identity.

exit codes:
  0  every differing object attributed (zero differences also prints, exits 0)
  1  at least one object changed on both sides - the conflict is stated
  2  usage or parse failure
  3  no object in any of the three files carries a revision - the stated
     properties do not exist in these inputs (they predate rev/writer) and
     this tool refuses to dress an XML diff up as attribution
"""

import argparse
import os
import sys
from xml.dom.minidom import parseString

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mmpz_git as mg  # noqa: E402  (the parser both tools share)


def identity(elem):
    """Sibling-unique identity for the merge view: (tag, stable id) when the
    object has one, mmpz-git's content identity otherwise."""
    if elem.hasAttribute("id") and elem.getAttribute("id") != "":
        return (elem.tagName, elem.getAttribute("id"))
    return mg._identity(elem)


def collect(path):
    """path-tuple -> element for every object in one file."""
    doc = parseString(mg.load_any(path))
    out = {}

    def walk(elem, prefix):
        counts = {}
        for child in mg._children(elem):
            base = identity(child)
            n = counts.get(base, 0)
            counts[base] = n + 1
            key = prefix + ((base, n),)
            out[key] = child
            walk(child, key)

    walk(doc.documentElement, ())
    return out


def rev_of(elem):
    """(rev, writer) as the model states them; absent rev reads as 0."""
    if elem is None:
        return 0, ""
    raw = elem.getAttribute("rev") if elem.hasAttribute("rev") else ""
    try:
        rev = int(raw)
    except ValueError:
        rev = 0
    writer = elem.getAttribute("writer") if elem.hasAttribute("writer") else ""
    return (rev if rev > 0 else 0), writer


def label_of(elem):
    """Human name for the report: `track id=2`, or mmpz's identity fallback."""
    if elem.hasAttribute("id") and elem.getAttribute("id") != "":
        return "%s id=%s" % (elem.tagName, elem.getAttribute("id"))
    base = mg._identity(elem)
    return "%s %s" % (base[0], "/".join(str(x) for x in base[1:]))


def added(name, o, t, our_label, their_label):
    """An object base does not carry: present on one side or both."""
    present = [(lb, e) for lb, e in ((our_label, o), (their_label, t))
               if e is not None]
    if not present:
        return None, False
    if len(present) == 2:
        return "%-24s added on BOTH sides: %s (rev %d) / %s (rev %d)" % (
            name, our_label, rev_of(o)[0], their_label, rev_of(t)[0]), True
    side, elem = present[0]
    return "%-24s added by %s (writer=%s, rev %d)" % (
        name, side, rev_of(elem)[1] or "<unstamped>", rev_of(elem)[0]), False


def removed(name, b, o, t, our_label, their_label):
    """An object one side dropped (or both)."""
    missing = [lb for lb, e in ((our_label, o), (their_label, t))
               if e is None]
    if len(missing) == 2:
        return "%-24s present only in base (removed on both sides)" % name, True
    return "%-24s removed by %s (base rev %d)" % (
        name, missing[0], rev_of(b)[0]), False


def added_or_removed(name, b, o, t, our_label, their_label):
    """Presence-only difference; None when the object is absent from the
    report, a line plus a conflict flag otherwise."""
    if b is None:
        return added(name, o, t, our_label, their_label)
    if o is None or t is None:
        return removed(name, b, o, t, our_label, their_label)
    return None, False


def changed(base_rev, ours_rev, theirs_rev, name, our_label, our_writer,
            their_label, their_writer):
    """Rev-based difference: who states a higher revision than base does."""
    ours_changed = ours_rev > base_rev
    theirs_changed = theirs_rev > base_rev
    if not ours_changed and not theirs_changed:
        return None, False
    if ours_changed and theirs_changed:
        return "%-24s changed on BOTH sides: %s (writer=%s, rev %d -> %d) / " \
               "%s (writer=%s, rev %d -> %d)" % (
                   name, our_label, our_writer or "<unstamped>", base_rev,
                   ours_rev, their_label, their_writer or "<unstamped>",
                   base_rev, theirs_rev), True
    side, writer, to = (our_label, our_writer, ours_rev) if ours_changed \
        else (their_label, their_writer, theirs_rev)
    return "%-24s changed by %s (writer=%s, rev %d -> %d)" % (
        name, side, writer or "<unstamped>", base_rev, to), False


def one(path, base, ours, theirs, our_label, their_label):
    """(report line or None, is_conflict) for a single object path."""
    b, o, t = base.get(path), ours.get(path), theirs.get(path)
    presence = added_or_removed(label_of(b or o or t), b, o, t,
                                our_label, their_label)
    if presence[0] is not None or (b is None) or (o is None) or (t is None):
        return presence
    br, bw = rev_of(b)
    orow, ow = rev_of(o)
    tr, tw = rev_of(t)
    return changed(br, orow, tr, label_of(b), our_label, ow,
                   their_label, tw)


def any_revision(*maps):
    return any(rev_of(elem)[0] > 0 for m in maps for elem in m.values())


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="rev_report.py",
        description="name which side changed each object, from rev/writer")
    parser.add_argument("--base", required=True)
    parser.add_argument("--ours", required=True)
    parser.add_argument("--theirs", required=True)
    parser.add_argument("--ours-label", default="ours")
    parser.add_argument("--theirs-label", default="theirs")
    args = parser.parse_args(argv)

    try:
        base = collect(args.base)
        ours = collect(args.ours)
        theirs = collect(args.theirs)
    except Exception as exc:  # parse/read failure is a usage-class exit
        print("rev_report: %s" % exc, file=sys.stderr)
        return 2

    if not any_revision(base, ours, theirs):
        print("rev_report: no object in these files carries a rev - they "
              "predate the revision pair; refusing to fake attribution "
              "from an XML diff.")
        return 3

    conflicts = 0
    changed_count = 0
    for path in sorted(set(base) | set(ours) | set(theirs), key=str):
        line, conflict = one(path, base, ours, theirs,
                             args.ours_label, args.theirs_label)
        if line is None:
            continue
        changed_count += 1
        conflicts += 1 if conflict else 0
        print(line)
    print("rev_report: %d object(s) differ, %d conflict(s)"
          % (changed_count, conflicts))
    return 1 if conflicts else 0


if __name__ == "__main__":
    sys.exit(main())
