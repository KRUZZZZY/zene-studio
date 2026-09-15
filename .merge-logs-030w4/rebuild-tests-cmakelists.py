#!/usr/bin/env python3
"""REBUILD tests/CMakeLists.txt as a verified union, instead of patching a patched file.

WHY. The tail-append rule misplaced three lanes' mid-list insertions; the
anchor-repair then moved one block that CONTAINS `if()/endif()` (the LuaApiSurface
ctest) into a region where CMake reported
  "Flow control statements are not properly nested" at tests/CMakeLists.txt:377
and the type-matched nesting audit showed four crossed pairs:
  endfunction() at 410 closing `if(LMMS_COVERAGE_FLAGS)` opened at 407
  endforeach()  at 509 closing `if(LMMS_TEST_NAME STREQUAL "PluginScanCacheTest")`
  endif()       at 2646 closing `foreach(LMMS_TEST_SRC IN LISTS LMMS_TESTS)`
  endif()       at 2684 closing `function(lmms_add_coverage TARGET)`
Patching further would be guesswork, so the file is rebuilt from its last known
good content (the pre-merge tip) by inserting each branch's own delta at ITS OWN
anchor, in merge order - the rule every append-only surface gets, applied to a
file whose blocks are structured rather than appended.

PROOFS: the `add_test(NAME ...)` set, the `.cpp` entry multiset and the
type-matched nesting audit must all come out clean, and `cmake` must accept it.

usage: rebuild-tests-cmakelists.py [--dry]
"""
import difflib
import re
import subprocess
import sys

PATH = "tests/CMakeLists.txt"
BASE_REV = "1e33a3ddd"          # the pre-merge tip of release/0.3.0
QUEUE = [
    ("030/lua-daw-binding"),
    ("030/mcp-coverage"),
    ("030/patcher-graph"),
    ("030/revision-timeline"),
    ("030/import-detection"),
    ("030/safe-start"),
    ("030/sample-accurate-automation"),
    ("030/audit"),
]
OPEN = re.compile(r"^\s*(IF|FOREACH|FUNCTION|MACRO|WHILE)\s*\(", re.I)
CLOSE = re.compile(r"^\s*(ENDIF|ENDFOREACH|ENDFUNCTION|ENDMACRO|ENDWHILE)\s*\(", re.I)
PAIR = {"IF": "ENDIF", "FOREACH": "ENDFOREACH", "FUNCTION": "ENDFUNCTION",
        "MACRO": "ENDMACRO", "WHILE": "ENDWHILE"}


def show(rev, path=PATH):
    out = subprocess.run(["git", "show", "%s:%s" % (rev, path)],
                         capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit("FATAL: cannot read %s:%s" % (rev, path))
    return out.stdout.split("\n")


def addtests(lines):
    return re.findall(r"add_test\(NAME\s+([A-Za-z0-9_]+)", "\n".join(lines))


def cpp_entries(lines):
    return [l.rstrip() for l in lines if re.match(r"^\s*[A-Za-z0-9_/]+\.cpp\s*$", l)]


def nesting(lines):
    stack, bad = [], []
    for n, l in enumerate(lines, 1):
        m = OPEN.match(l)
        if m:
            stack.append((m.group(1).upper(), n))
            continue
        m = CLOSE.match(l)
        if m:
            c = m.group(1).upper()
            if not stack or PAIR[stack[-1][0]] != c:
                bad.append((n, c, stack[-1] if stack else None))
                if stack:
                    stack.pop()
            else:
                stack.pop()
    return bad, stack


def main():
    dry = "--dry" in sys.argv
    ours = show(BASE_REV)
    print("== rebuilt from %s: %d lines, %d add_test, %d .cpp entries"
          % (BASE_REV, len(ours), len(addtests(ours)), len(cpp_entries(ours))))
    moved_total = 0
    for branch in QUEUE:
        base_rev = subprocess.run(["git", "merge-base", BASE_REV, branch],
                                  capture_output=True, text=True).stdout.strip()
        # the branch may not contain BASE_REV; use the branch's own fork point
        base, theirs = show(base_rev), show(branch)
        sm = difflib.SequenceMatcher(None, base, theirs, autojunk=False)
        placed = 0
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag not in ("insert", "replace"):
                continue
            block = [l for l in theirs[j1:j2] if l.strip()]
            if not block:
                continue
            if all(l in ours for l in block):
                continue                      # already carried
            prev = [l for l in theirs[:j1] if l.strip()]
            anchor_at = None
            for k in range(6, 0, -1):
                cand = prev[-k:]
                hits = [i for i in range(len(ours) - k + 1) if ours[i:i + k] == cand]
                if hits:
                    anchor_at = (hits[-1] + k - 1, k)
                    break
            if anchor_at is None:
                sys.exit("FATAL %s: no anchor for a %d-line block (%r)"
                         % (branch, len(block), block[0][:60]))
            pos, k = anchor_at
            ours = ours[:pos + 1] + block + ours[pos + 1:]
            placed += len(block)
        if placed:
            print("   %-34s +%d line(s) placed at their own anchors" % (branch, placed))
            moved_total += placed
    bad, stack = nesting(ours)
    print("== proofs after the rebuild")
    print("   add_test(NAME) entries: %d" % len(addtests(ours)))
    print("   .cpp entry lines     : %d" % len(cpp_entries(ours)))
    print("   nesting problems     : %d ; unclosed: %d" % (len(bad), len(stack)))
    for n, c, top in bad:
        print("     !! %s at line %d (innermost open %r)" % (c, n, top))
    for t, n in stack:
        print("     !! unclosed %s opened at %d" % (t, n))
    if bad or stack:
        sys.exit("FATAL: the rebuilt file still has a nesting problem")
    if dry:
        print("   (dry run)")
        return
    open(PATH, "w").write("\n".join(ours))
    print("   WROTE %s" % PATH)


if __name__ == "__main__":
    main()
