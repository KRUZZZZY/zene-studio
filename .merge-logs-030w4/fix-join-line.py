#!/usr/bin/env python3
"""Repair the A16 JOIN statement in src/core/ControlReversibilityTable.cpp.

DEFECT (introduced by this train's join-union re-emission): a lane's comment that
sat INSIDE the identifier list was carried into the same physical line as the ids
that follow it, so `// feature row 77 ... four rows` commented out
`reversibilitySafeStartRowTable, reversibilityAutomationRampRowTable})` and the
statement stopped parsing (`error: expected unqualified-id before ')' token` at
the lambda's `}();`). The ID SET was right; the LAYOUT was not.

REPAIR: parse the statement, keep every `reversibility<...>RowTable` token in
order (deduped), move any comment text found inside the list to standalone
comment lines ABOVE the statement, and re-emit the list with no comment on any
line that carries code.

PROOFS printed: the id set before/after (must be identical), the comment-stripped
token count, brace/paren balance, and a simulated C++ read (every line's `//`
tail removed) that must still name every id.

usage: fix-join-line.py [--dry]
"""
import re
import sys

PATH = "src/core/ControlReversibilityTable.cpp"
ID = re.compile(r"^reversibility[A-Za-z0-9]*RowTable$")
INDENT = "\t\t"
CONT = "\t\t\t\t"


def code_part(line):
    """The part of a line a C++ compiler sees (no comment)."""
    out, i, in_str = [], 0, False
    while i < len(line):
        c = line[i]
        if c == '"':
            in_str = not in_str
        if not in_str and c == "/" and i + 1 < len(line) and line[i + 1] == "/":
            break
        out.append(c)
        i += 1
    return "".join(out)


def main():
    dry = "--dry" in sys.argv
    lines = open(PATH).read().split("\n")
    i0 = next(i for i, l in enumerate(lines) if ": {reversibility" in l)
    i1 = next(i for i in range(i0, len(lines)) if lines[i].rstrip().endswith("})"))
    region = lines[i0:i1 + 1]
    text = "\n".join(region)
    body = text[text.index("{") + 1:text.rindex("})")]

    ids, comments, seen = [], [], set()
    for raw in body.replace("\n", " ").split(","):
        item = raw.strip()
        if not item:
            continue
        if item.startswith("//"):
            c = item[2:].strip()
            c = re.sub(r"\s*reversibility[A-Za-z0-9]*RowTable\s*$", "", c)
            comments.append(c)
            for x in re.findall(r"reversibility[A-Za-z0-9]*RowTable", item):
                if x not in seen:
                    seen.add(x)
                    ids.append(x)
            continue
        # an id may be preceded by comment text on the same physical piece
        m = re.findall(r"reversibility[A-Za-z0-9]*RowTable", item)
        for x in m:
            if x not in seen:
                seen.add(x)
                ids.append(x)
        tail = item
        for x in m:
            tail = tail.replace(x, "")
        tail = tail.strip().strip(",").strip()
        if tail:
            comments.append("(salvaged) " + tail)

    # ids that only ever appeared inside a comment (the ones the layout swallowed)
    for c in list(comments):
        for x in re.findall(r"reversibility[A-Za-z0-9]*RowTable", c):
            if x not in seen:
                seen.add(x)
                ids.append(x)

    # where do the ids end up relative to the old one?
    first = lines[i0]
    head = first[:first.index("{") + 1]
    out_region = []
    for c in comments:
        out_region.append(INDENT + "// " + c)
    # wrap the list at ~104 characters like the file's neighbours do
    cur = INDENT + head
    for k, x in enumerate(ids):
        piece = ("" if cur.rstrip().endswith("{") else " ") + x + ("," if k + 1 < len(ids) else "})")
        if len(cur) + len(piece) > 104 and not cur.rstrip().endswith("{"):
            out_region.append(cur.rstrip())
            cur = CONT + x + ("," if k + 1 < len(ids) else "})")
        else:
            cur += piece
    out_region.append(cur.rstrip())

    new = lines[:i0] + out_region + lines[i1 + 1:]

    # ---- proofs
    old_ids = [x for x in re.findall(r"reversibility[A-Za-z0-9]*RowTable", text)]
    new_text = "\n".join(out_region)
    new_ids = [x for x in re.findall(r"reversibility[A-Za-z0-9]*RowTable", new_text)]
    code_ids = [x for line in out_region for x in re.findall(r"reversibility[A-Za-z0-9]*RowTable", code_part(line))]
    print("== %s" % PATH)
    print("   statement was lines %d..%d (%d line(s))" % (i0 + 1, i1 + 1, len(region)))
    print("   ids: old-tokens=%d  new-tokens=%d  new-CODE-tokens=%d  unique=%d"
          % (len(old_ids), len(new_ids), len(code_ids), len(set(new_ids))))
    print("   comments salvaged to above the statement: %d" % len(comments))
    for c in comments:
        print("     | %s" % c[:100])
    if set(old_ids) != set(new_ids):
        sys.exit("FATAL: id set changed: lost=%s gained=%s"
                 % (set(old_ids) - set(new_ids), set(new_ids) - set(old_ids)))
    if len(code_ids) != len(set(code_ids)):
        sys.exit("FATAL: duplicate id in the statement")
    if sorted(code_ids) != sorted(set(new_ids)):
        sys.exit("FATAL: some id is not visible to the compiler (still inside a comment)")
    bal = new_text.count("{") - new_text.count("}")
    par = new_text.count("(") - new_text.count(")")
    print("   balance: braces=%+d parens=%+d" % (bal, par))
    if bal != 0 or par != 0:
        sys.exit("FATAL: the re-emitted statement is not balanced")
    print("   PROOF: same id set, every id visible to the compiler, statement balanced")
    if dry:
        print("   (dry run; the re-emitted statement would be:)")
        for l in out_region:
            print("     %s" % l[:140])
        return
    open(PATH, "w").write("\n".join(new))
    print("   WROTE %s" % PATH)
    for l in out_region:
        print("     %s" % l[:140])


if __name__ == "__main__":
    main()
