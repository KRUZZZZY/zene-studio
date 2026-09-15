#!/usr/bin/env python3
"""Resolve one merge's NON-code conflicts by a recorded UNION rule, reading the
three index stages (never marker geometry) and writing the file in place.

Every rule is a UNION OF ENTRIES (or a union of appended blocks), never a
side-pick, except two declared classes:
  * a MEASUREMENT file -> keep-ours, and the merged tip re-measures it;
  * a genuine code conflict -> NOT handled here (reported, resolved by hand).

Proofs printed per file: entry counts before (base/ours/theirs), union count,
duplicates, conflict markers, trailing newline.

usage: union-resolve.py <label>            # rules from RULES[<label>] by path
       union-resolve.py --dry <label>
"""
import difflib
import subprocess
import sys

# ---------------------------------------------------------------- rule tables
# path -> rule.  Rules:
#   block-union    append-only register (doc block / CMake block / declaration
#                  block at the tail): ours verbatim, then theirs' added lines
#   section-union  heading-delimited doc register: union of the SECTION set
#   entry-union    non-comment entry list (a manifest): union of the entry SET,
#                  re-emitted in LC_ALL=C byte order into the existing slots;
#                  theirs-only comment lines appended after ours'
#   ledger-union   TAB-delimited `path<TAB>reason` ledger: union keyed by path
#   keep-ours      a MEASUREMENT (never a union): ours kept, re-measured at tip
RULES = {
    # ---- lane 1: lua-daw-binding
    "lua-daw-binding": {
        "docs/KNOWN-LIMITATIONS.md": "block-union",
        "docs/RELEASE-NOTES-v0.3.0-alpha.md": "section-union",
        "tests/CMakeLists.txt": "block-union",
        "tests/fork-sources.txt": "entry-union",
        "tests/upstream-modifications.txt": "ledger-union",
    },
    # ---- lane 2: mcp-coverage
    "mcp-coverage": {
        "docs/RELEASE-NOTES-v0.3.0-alpha.md": "section-union",
        "tests/CMakeLists.txt": "block-union",
        "tests/fork-sources.txt": "entry-union",
    },
    # ---- lane 3: patcher-graph
    "patcher-graph": {
        "docs/RELEASE-NOTES-v0.3.0-alpha.md": "section-union",
        "tests/fork-sources.txt": "entry-union",
        "tests/src/core/ReversibilityContractTest.cpp": "keep-ours",
        "tests/upstream-modifications.txt": "ledger-union",
    },
    # ---- lane 4: revision-timeline
    "revision-timeline": {
        "docs/KNOWN-LIMITATIONS.md": "block-union",
        "docs/RELEASE-NOTES-v0.3.0-alpha.md": "section-union",
        "include/ControlRegistryGroups.h": "scope-union",
        "include/ControlReversibility.h": "scope-union",
        "src/core/CMakeLists.txt": "scope-union",
        "src/core/ControlRegistryRegistrations.cpp": "scope-union",
        "src/core/ControlReversibilityTable.cpp": "join-union",
        "tests/src/core/ReversibilityContractTest.cpp": "keep-ours",
        "tests/upstream-modifications.txt": "ledger-union",
    },
    # ---- lane 5: import-detection
    "import-detection": {
        "LANE-STATE.md": "block-union",
        "docs/KNOWN-LIMITATIONS.md": "block-union",
        "docs/RELEASE-NOTES-v0.3.0-alpha.md": "section-union",
        "include/ControlRegistryGroups.h": "scope-union",
        "include/ControlReversibility.h": "scope-union",
        "include/Song.h": "code",              # by hand
        "src/core/CMakeLists.txt": "scope-union",
        "src/core/ControlRegistryRegistrations.cpp": "scope-union",
        "src/core/ControlReversibilityTable.cpp": "join-union",
        "src/core/Song.cpp": "code",           # by hand
        "tests/CMakeLists.txt": "block-union",
        "tests/all-sources.txt": "entry-union",
        "tests/fork-sources.txt": "entry-union",
        "tests/src/core/ReversibilityContractTest.cpp": "keep-ours",
        "tests/tools-sources.txt": "entry-union",
        "tests/upstream-modifications.txt": "ledger-union",
    },
    # ---- lane 6: safe-start
    "safe-start": {
        "docs/RELEASE-NOTES-v0.3.0-alpha.md": "section-union",
        "include/ControlRegistryGroups.h": "scope-union",
        "src/core/CMakeLists.txt": "scope-union",
        "src/core/ControlRegistryRegistrations.cpp": "scope-union",
        "src/core/ControlReversibilityTable.cpp": "join-union",
        "tests/src/core/ReversibilityContractTest.cpp": "keep-ours",
        "tests/upstream-modifications.txt": "ledger-union",
    },
    # ---- lane 7: sample-accurate-automation (merged with the 5a fix)
    "sample-accurate-automation": {
        "docs/KNOWN-LIMITATIONS.md": "block-union",
        "docs/RELEASE-NOTES-v0.3.0-alpha.md": "section-union",
        "include/ControlRegistryGroups.h": "scope-union",
        "include/ControlReversibility.h": "scope-union",
        "src/core/ControlReversibilityTable.cpp": "join-union",
        "tests/all-sources.txt": "entry-union",
        "tests/fork-sources.txt": "entry-union",
        "tests/src/core/ReversibilityContractTest.cpp": "keep-ours",
        "tests/upstream-modifications.txt": "ledger-union",
    },
    # ---- lane 8: audit (docs-only)
    "audit": {
        # LANE-STATE.md is a LANE-OWNED scratch record (each lane overwrites it
        # with its own state), not a shared registry: union has no meaning there,
        # so the incoming lane's copy wins and the decision is recorded.
        "LANE-STATE.md": "last-writer-theirs",
        # The coverage matrix was rewritten on BOTH sides. Ours is the NEWER
        # measurement (re-measured at 0ee78abed by 030/test-gaps: 30 groups /
        # 154 ids); theirs is the audit's older tip-pinned snapshot plus its own
        # SUPERSEDED banner and its new final section. Keep ours' body, carry
        # their banner and their extra section - content is not a measurement.
        "docs/COVERAGE-MATRIX-2026-09-13.md": "preamble-union",
    },
}

REMEASURE = []
MANUAL = []


def stage(n, path):
    out = subprocess.run(["git", "show", ":%d:%s" % (n, path)],
                         capture_output=True, text=True)
    if out.returncode != 0:
        return None
    return out.stdout.split("\n")


def delta(base, theirs):
    sm = difflib.SequenceMatcher(None, base, theirs, autojunk=False)
    d = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag in ("insert", "replace"):
            d.extend(theirs[j1:j2])
    return d


def is_entry(l):
    return l.strip() != "" and not l.lstrip().startswith("#")


def is_comment(l):
    return l.lstrip().startswith("#")


def emit(lines):
    text = "\n".join(lines).rstrip("\n") + "\n"
    bad = [n for n, l in enumerate(text.split("\n"), 1)
           if l.startswith("<<<<<<<") or l.startswith(">>>>>>>") or l.rstrip() == "======="]
    return text, bad


def sec_split(lines):
    secs, order, cur, buf = {}, [], "(preamble)", []
    for l in lines:
        if l.startswith("# ") or l.startswith("## ") or l.startswith("### "):
            secs[cur] = buf
            order.append(cur)
            cur, buf = l, []
        else:
            buf.append(l)
    secs[cur] = buf
    order.append(cur)
    return order, secs


def head_sections(lines):
    """Map heading -> (start, end) index range, in order."""
    hs = [(i, l) for i, l in enumerate(lines)
          if l.startswith("# ") or l.startswith("## ") or l.startswith("### ")]
    out = {}
    for k, (i, l) in enumerate(hs):
        end = hs[k + 1][0] if k + 1 < len(hs) else len(lines)
        out[l] = (i, end)
    return out


def hunk_report(path, text, base, ours, theirs):
    """Per-file proof that nothing either side ADDED is missing."""
    def added(side):
        return [l for l in delta(base, side) if l.strip()]
    ml = set(l.rstrip() for l in text.split("\n"))
    miss_o = [l for l in added(ours) if l.rstrip() not in ml]
    miss_t = [l for l in added(theirs) if l.rstrip() not in ml]
    return miss_o, miss_t


# ---------------------------------------------------------------- the rules
def resolve(path, rule, dry):
    base = stage(1, path)
    ours = stage(2, path)
    theirs = stage(3, path)
    print("== %s [%s]" % (path, rule))
    if ours is None:
        ours = []
    if theirs is None:
        theirs = []
    if base is None:
        base = []
    print("   lines base=%d ours=%d theirs=%d" % (len(base), len(ours), len(theirs)))

    if rule == "preamble-union":
        # A doc both sides rewrote: keep ours' BODY (the newer measurement) but
        # carry theirs' added PREAMBLE lines (a supersession banner is content,
        # not a measurement) and theirs-only SECTIONS. Reported line by line.
        o_h = head_sections(ours)
        t_h = head_sections(theirs)
        o_first = min([s for s, _ in o_h.values()], default=len(ours))
        t_first = min([s for s, _ in t_h.values()], default=len(theirs))
        sm = difflib.SequenceMatcher(None, base, theirs, autojunk=False)
        pre = []
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag not in ("insert", "replace"):
                continue
            for l in theirs[j1:j2]:
                if not l.strip():
                    continue
                # theirs' added lines in the preamble region, plus any BANNER
                # (a blockquote callout) it added below the title: a banner is
                # a declaration about the document, i.e. content.
                if j1 < t_first or l.lstrip().startswith(">"):
                    pre.append(l)
        o_lines = set(l.rstrip() for l in ours)
        extra_pre = [l for l in pre if l.rstrip() not in o_lines]
        out = list(ours)
        if extra_pre:
            # after the title line if ours has one, else at the very top
            o_title = [s for s, _ in o_h.values()] or []
            at = 1 if out and out[0].startswith("# ") else 0
            out = out[:at] + extra_pre + [""] + out[at:]
        o_sec = set(o_h)
        added_sections = []
        for h, (s, e) in t_h.items():
            if h in o_sec:
                continue
            added_sections.append(h)
            while out and out[-1].strip() == "":
                out.pop()
            out += [""] + theirs[s:e]
        note = ("preamble+section union: %d theirs-added preamble line(s) carried into ours; "
                "%d theirs-only section(s) appended; body kept from ours"
                % (len(extra_pre), len(added_sections)))
        for l in extra_pre:
            print("     preamble: %s" % l[:110])
        for h in added_sections:
            print("     section : %s" % h[:110])
    elif rule == "last-writer-theirs":
        out = list(theirs)
        note = ("LAST-WRITER: this file is a LANE-OWNED record, not a shared registry - "
                "each lane overwrites it with its own state, so the incoming lane's copy "
                "is the file. Ours' copy stays reachable in git history (declared, not "
                "silently dropped)")
        MANUAL.append("%s :: lane-owned scratch file taken from theirs (declared)" % path)
    elif rule == "code":
        print("   !! CODE CONFLICT - resolved by hand, not by this tool")
        MANUAL.append(path)
        return
    elif rule == "addadd-theirs":
        out = list(theirs)
        note = "add/add: whole file taken from THEIRS (a docs addition, no ours-side lineage)"
    elif rule == "join-union":
        # The A16 JOIN list in ControlReversibilityTable.cpp: the row-table ids
        # live on the continuation lines of one statement, `... : {a, b, c})`.
        # Union the id SET of that statement, insert theirs-only ids before the
        # closing `})` of ours' statement, and take nothing from theirs' layout
        # (theirs' reflow produces a stray tail fragment under block-union).
        def stmt_range(lines):
            i0 = next((i for i, l in enumerate(lines) if ": {reversibility" in l), None)
            if i0 is None:
                sys.exit("FATAL: no join statement in %s" % path)
            i1 = next((i for i in range(i0, len(lines)) if lines[i].rstrip().endswith("})")), None)
            if i1 is None:
                sys.exit("FATAL: join statement never closes in %s" % path)
            return i0, i1

        def ids_in(lines, i0, i1):
            body = "\n".join(lines[i0:i1 + 1])
            body = body[body.index("{") + 1:body.rindex("})")]
            return [x.strip() for x in body.replace("\n", " ").split(",") if x.strip()]

        o0, o1 = stmt_range(ours)
        o_ids = ids_in(ours, o0, o1)
        try:
            t0, t1 = stmt_range(theirs)
            t_ids = ids_in(theirs, t0, t1)
        except SystemExit:
            t_ids = [x.strip() for x in
                     "".join(delta(base, theirs)).replace("\n", " ").split("{")[-1]
                     .split("})")[0].split(",") if x.strip()]
        extra = [x for x in t_ids if x not in o_ids]
        lost = [x for x in o_ids if x not in t_ids]
        out = list(ours)
        if extra:
            last = out[o1]
            assert last.rstrip().endswith("})"), last[-20:]
            out[o1] = last.rstrip()[:-2].rstrip().rstrip(",") + ", " + ", ".join(extra) + "})"
        note = ("join union: ours %d ids + theirs %d -> %d (added %s; ours-only %s)"
                % (len(o_ids), len(t_ids), len(o_ids) + len(extra),
                   ",".join(extra) or "none", ",".join(lost) or "none"))
        print("     join line now ends: ...%s" % out[o1][-90:])
    elif rule == "keep-ours":
        out = list(ours)
        note = "kept OURS verbatim: a MEASUREMENT, not an entry set"
        REMEASURE.append(path)
    elif rule == "entry-union":
        # The file's own verify command sorts BOTH sides
        #   diff <(grep -vE '^[[:space:]]*(#|$)' <manifest> | LC_ALL=C sort) <(recipe | LC_ALL=C sort)
        # so the ENTRY SET is the contract and line order merely groups them. Union =
        # set union, placed byte-sorted into ours' existing entry slots; a surplus
        # entry is inserted at its byte-sorted position among the entry slots.
        o_e = [l for l in ours if is_entry(l)]
        t_e = [l for l in theirs if is_entry(l)]
        o_set, t_set = set(o_e), set(t_e)
        union = sorted(o_set | t_set, key=lambda x: x.encode())
        dupes_in_side = (len(o_e) - len(o_set)) + (len(t_e) - len(t_set))
        # slots: positions in ours that hold an entry
        slots = [i for i, l in enumerate(ours) if is_entry(l)]
        out = list(ours)
        for k, i in enumerate(slots):
            out[i] = union[k]
        surplus = union[len(slots):]
        # insert each surplus line immediately BEFORE the entry slot whose value is
        # the next greater entry (byte order), else after the last slot
        for e in surplus:
            pos = len(out)
            for i in range(len(out)):
                if is_entry(out[i]) and out[i].encode() > e.encode():
                    pos = i
                    break
            out.insert(pos, e)
        o_c = set(l for l in ours if is_comment(l))
        newc = [l for l in theirs if is_comment(l) and l not in o_c]
        if newc:
            while out and out[-1].strip() == "":
                out.pop()
            out += [""] + newc
        placed = [l for l in out if is_entry(l)]
        assert sorted(set(placed), key=lambda x: x.encode()) == union, \
            "FATAL: placed entry set != union"
        assert len(placed) == len(union), "FATAL: placed %d entries, union %d" % (len(placed), len(union))
        note = ("entry union: ours %d + theirs %d -> %d unique entries "
                "(%d surplus inserted at sorted slot positions); dups inside a side %d; "
                "%d theirs-only comment line(s) appended"
                % (len(o_set), len(t_set), len(union), len(surplus), dupes_in_side, len(newc)))
    elif rule == "ledger-union":
        def clauses(why):
            """A ledger reason is a '; '-separated clause list (each merge appends
            its own clause). Union of the CLAUSE SET is the union of the entry."""
            parts = [c.strip() for c in why.split("; ") if c.strip()]
            return parts

        def norm(c):
            return c.strip().rstrip(".").strip().lower()

        o_ent, t_ent = {}, {}
        for l in ours:
            if is_entry(l) and "\t" in l:
                p, w = l.split("\t", 1)
                o_ent[p] = w
        for l in theirs:
            if is_entry(l) and "\t" in l:
                p, w = l.split("\t", 1)
                t_ent[p] = w
        merged, extended, joined, bare = {}, [], [], []
        for p in o_ent:
            if p not in t_ent:
                merged[p] = o_ent[p]
                continue
            oc, tc = clauses(o_ent[p]), clauses(t_ent[p])
            o_n = set(norm(c) for c in oc)
            t_n = set(norm(c) for c in tc)
            extra = [c for c in tc if norm(c) not in o_n]
            lost_c = [c for c in oc if norm(c) not in t_n]
            merged[p] = "; ".join(oc + extra) if extra else o_ent[p]
            if extra:
                extended.append((p, len(extra)))
            if lost_c:
                joined.append((p, len(lost_c)))
        only_t = [p for p in t_ent if p not in o_ent]
        for p in only_t:
            merged[p] = t_ent[p]
        for line in ours:
            if is_entry(line) and "\t" not in line:
                bare.append(line)
        for line in theirs:
            if is_entry(line) and "\t" not in line and line not in bare:
                bare.append(line)
        # A lane cut from an older tip may still carry a human NOTE that lost its
        # leading '#': the gate reads it as an entry with an empty reason and
        # refuses the whole file. Ours already carries that note as a comment
        # (restored by an earlier fix-up) - so the union keeps the comment and
        # drops the un-commented copy. Anything genuinely new survives as a
        # restored comment with its '#' back.
        o_comments = set(norm(l.lstrip("#").strip()) for l in ours if is_comment(l))
        dropped_notes, restored_notes = [], []
        for line in list(bare):
            if norm(line) in o_comments:
                bare.remove(line)
                dropped_notes.append(line[:70])
            else:
                restored_notes.append(line[:70])
        if dropped_notes:
            print("     dropped theirs' un-commented copy of %d note(s) ours carries as a comment"
                  % len(dropped_notes))
        for p, why in merged.items():
            if not why.strip():
                sys.exit("FATAL %s: empty reason for %s" % (path, p))
        # re-emit ours' layout, our entry slots in place, then theirs-only keys
        out = []
        for l in ours:
            if is_entry(l) and "\t" in l:
                p = l.split("\t", 1)[0]
                out.append("%s\t%s" % (p, merged[p]))
            elif l in bare:
                # a note that lost its '#': restore it as a comment
                out.append("# " + l)
                MANUAL.append("%s :: bare note restored as a comment: %r" % (path, l[:70]))
            else:
                out.append(l)
        if only_t:
            while out and out[-1].strip() == "":
                out.pop()
            for p in only_t:
                out.append("%s\t%s" % (p, t_ent[p]))
        o_c = set(l for l in ours if is_comment(l))
        newc = [l for l in theirs if is_comment(l) and l not in o_c]
        if newc:
            out += [""] + newc
        note = ("ledger union: ours %d + theirs %d -> %d keys (%d theirs-only added, "
                "%d keys whose reason gained theirs' clause(s)); %d theirs-only comment line(s)"
                % (len(o_ent), len(t_ent), len(merged), len(only_t), len(extended), len(newc)))
        for p, n in extended:
            print("     reason extended by %d clause(s): %s" % (n, p))
        for p in only_t:
            print("     added: %s" % p)
        if joined:
            print("     !! OURS-ONLY CLAUSES (kept): %r" % joined)
        if bare:
            for b in bare:
                MANUAL.append("%s :: bare entry %r" % (path, b[:60]))
    elif rule == "section-union":
        o_order, o_sec = sec_split(ours)
        t_order, t_sec = sec_split(theirs)
        out = list(ours)
        while out and out[-1].strip() == "":
            out.pop()
        added, both, lost = [], [], []
        for h in t_order:
            if h not in o_sec:
                added.append(h)
                out += [""] + [h] + t_sec[h]
                while out and out[-1].strip() == "":
                    out.pop()
            elif o_sec[h] != t_sec[h]:
                both.append(h)
                REMEASURE.append("%s :: %s" % (path, h))
        for h in o_order:
            if h not in t_sec:
                lost.append(h)
        # SALVAGE: a section both sides rewrote is a MEASURED narrative - the tip
        # re-measures it, so ours stays and theirs' rewritten numbers are NOT
        # unioned. But a lane's own new BULLET is not a measurement: it is the
        # lane's record of its own delta. Carry every bullet run theirs added
        # that ours does not already carry, into the same section of ours.
        salvaged = []
        if both:
            sm = difflib.SequenceMatcher(None, base, theirs, autojunk=False)
            t_heads = head_sections(theirs)
            runs = []
            for tag, i1, i2, j1, j2 in sm.get_opcodes():
                if tag not in ("insert", "replace"):
                    continue
                run = []
                for l in theirs[j1:j2]:
                    if l.strip() and (l.lstrip().startswith("* ") or l.lstrip().startswith("- ")):
                        run.append(l)
                    else:
                        if run:
                            runs.append((j1, run))
                            run = []
                if run:
                    runs.append((j1, run))
            o_all = set(l.rstrip() for l in ours)
            for j0, run in runs:
                if all(l.rstrip() in o_all for l in run):
                    continue
                # which section of theirs does this run live in?
                host = None
                for h, (s, e) in t_heads.items():
                    if s <= j0 < e:
                        host = h
                        break
                if host is None or host not in o_sec:
                    continue
                # insert at the end of that section in `out`
                hs = head_sections(out)
                if host not in hs:
                    continue
                s, e = hs[host]
                ins = e
                while ins > s and out[ins - 1].strip() == "":
                    ins -= 1
                out = out[:ins] + run + out[ins:]
                salvaged.append((host, len(run)))
        note = ("section union: +%d theirs-only section(s), %d ours-only, %d both-rewritten "
                "(kept ours, RE-MEASURE flagged); %d lane bullet run(s) salvaged"
                % (len(added), len(lost), len(both), len(salvaged)))
        for h in added:
            print("     appended : %s" % h[:100])
        for h in both:
            print("     both rewrote, kept ours: %s" % h[:100])
        for h, n in salvaged:
            print("     salvaged %d bullet line(s) into: %s" % (n, h[:90]))
    else:  # block-union / scope-union / decl-union
        d = delta(base, theirs)
        while d and d[0].strip() == "":
            d.pop(0)
        while d and d[-1].strip() == "":
            d.pop()
        if rule == "scope-union":
            # A file with a TRAILER (a namespace close, an #endif, a set()'s
            # closing paren) must not take the lane's block at the tail: each
            # block belongs where the lane put it - inside the scope. Theirs'
            # own file is the oracle for that position, and a lane may have
            # SEVERAL insertion points (a source list AND a row-file list), so
            # walk the opcodes and anchor each block separately.
            sm = difflib.SequenceMatcher(None, base, theirs, autojunk=False)
            out = list(ours)
            placed, anchors_used = 0, []
            for tag, i1, i2, j1, j2 in sm.get_opcodes():
                if tag not in ("insert", "replace"):
                    continue
                block = [l for l in theirs[j1:j2] if l.strip()]
                if not block:
                    continue
                anchor = None
                for j in range(j1 - 1, -1, -1):
                    if theirs[j].strip():
                        anchor = theirs[j]
                        break
                if anchor is None or anchor not in out:
                    sys.exit("FATAL %s: anchor %r not in ours" % (path, (anchor or "")[:70]))
                hits = [i for i, l in enumerate(out) if l == anchor]
                if len(hits) != 1:
                    print("     WARN: anchor %r found %d times; using the last"
                          % (anchor[:60], len(hits)))
                pos = hits[-1]
                out = out[:pos + 1] + block + out[pos + 1:]
                placed += 1
                anchors_used.append(anchor.strip()[:60])
            if not placed:
                sys.exit("FATAL %s: nothing to place" % path)
            note = ("scope union: %d block(s) placed after their own anchor(s) in ours: %s"
                    % (placed, " | ".join(anchors_used)))
        else:
            out = list(ours)
            while out and out[-1].strip() == "":
                out.pop()
            out = out + [""] + d
            note = "block union: appended theirs' %d added line(s)" % len(d)
        if d:
            print("     first: %s" % d[0][:100])
            print("     last : %s" % d[-1][:100])

    text, bad = emit(out)
    miss_o, miss_t = hunk_report(path, text, base, ours, theirs)
    dups = len(text.split("\n")) - len(set(text.split("\n")))
    print("   %s" % note)
    if rule == "ledger-union":
        # line-level loss is EXPECTED here: a same-key reason is a clause-list union,
        # so the merged line is not byte-equal to either side's. Prove clause coverage.
        def coverage(src):
            m, miss = {}, []
            for l in text.split("\n"):
                if is_entry(l) and "\t" in l:
                    p, w = l.split("\t", 1)
                    m[p] = [norm(c) for c in w.split("; ") if c.strip()]
            for l in src:
                if is_entry(l) and "\t" in l:
                    p, w = l.split("\t", 1)
                    if p not in m:
                        miss.append("%s: key missing" % p)
                        continue
                    for c in w.split("; "):
                        if c.strip() and norm(c) not in m[p]:
                            miss.append("%s: clause missing %r" % (p, c[:60]))
            return miss
        co, ct = coverage(ours), coverage(theirs)
        print("   CLAUSE COVERAGE: ours clauses missing=%d, theirs clauses missing=%d"
              % (len(co), len(ct)))
        if co:
            print("   !! %r" % co[:3])
        if ct:
            print("   !! %r" % ct[:3])
        miss_o = miss_t = []          # superseded by the clause-level proof above
    print("   result %d lines; markers=%s; dup lines=%d; added-line loss ours=%d theirs=%d%s"
          % (len(text.split("\n")), bad or "none", dups, len(miss_o), len(miss_t),
             "" if text.endswith("\n") else "; !! NO TRAILING NEWLINE"))
    if bad:
        sys.exit("FATAL: markers survived in %s" % path)
    if miss_o:
        print("   !! ours-added lines missing (first 3): %r" % miss_o[:3])
    if miss_t:
        print("   !! theirs-added lines missing (first 3): %r" % miss_t[:3])
    if not text.endswith("\n"):
        sys.exit("FATAL: %s lost its trailing newline" % path)
    if dry:
        print("   (dry run, not written)")
        return
    with open(path, "w") as fh:
        fh.write(text)
    print("   WROTE %s" % path)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dry = "--dry" in sys.argv
    if len(args) != 1:
        sys.exit(__doc__)
    label = args[0]
    if label not in RULES:
        sys.exit("FATAL: no rule table for lane %r" % label)
    conflicts = subprocess.run(["git", "diff", "--name-only", "--diff-filter=U"],
                               capture_output=True, text=True).stdout.split()
    if not conflicts:
        sys.exit("FATAL: the conflicted set is EMPTY - there is nothing to resolve. "
                 "(Do not run this on an already-staged or aborted merge.)")
    print("== conflicted set: %d file(s)" % len(conflicts))
    unruled = [f for f in conflicts if f not in RULES[label]]
    # fail BEFORE writing anything if any conflicted path has no rule
    if unruled:
        sys.exit("FATAL: no rule for %r in lane %r" % (unruled, label))
    for f in conflicts:
        rule = RULES[label].get(f)
        if rule is None:
            print("== %s [NO RULE - needs one]" % f)
            MANUAL.append(f)
            continue
        resolve(f, rule, dry)
        print()
    if MANUAL:
        print("MANUAL / UNRULED (%d):" % len(MANUAL))
        for x in MANUAL:
            print("   - %s" % x)
    if REMEASURE:
        print("RE-MEASURE LATER (%d):" % len(REMEASURE))
        for x in REMEASURE:
            print("   - %s" % x)
    return 1 if (MANUAL or unruled) else 0


if __name__ == "__main__":
    sys.exit(main())
