#!/usr/bin/env python3
"""DOC-5 citation sweep — the measured list.

Every `*.md` name the product tree mentions is a citation of *something*.  This
tool resolves each one against three places and prints where it lives:

  product   the file is tracked in the product repo at HEAD
  product@  the file is tracked in the product repo on ANOTHER branch only
            (the merge train has not brought it to this branch yet)
  workspace the file is tracked in the program workspace repo
  disk      the file is in neither repo, but exists on disk under the
            workspace root or the AI-KOS knowledge root
  NEITHER   none of the above: the citation dangles

Citing files are classified, because the two classes are repaired differently:

  code      src/ include/ plugins/ modules/ recording/ tools/ cmake/ data/
            CMakeLists.txt .github/ and the product's own test code under tests/src/
  docs      docs/
  evidence  tests/integration-logs-*/ tests/reference/ tests/evidence-manifest.tsv
            — frozen records of runs and merges.  Their citations are history,
            not claims about the tree; they are counted and never repaired.
  harness   the socket harness under tests/ that is not product code

Known false-positive families are listed in FALSE_POSITIVES below and are reported
as such rather than as dangling citations: a name inside a longer identifier, a
name split by a line wrap, a name built from a shell variable, an external
project's own licence file.

Usage:
  tools/doc5-citations.py                 # the full TSV
  tools/doc5-citations.py --appendix      # the markdown table for docs/specs/README.md
  tools/doc5-citations.py --specs         # only rows that are specifications
  tools/doc5-citations.py --verify-snapshots   # check docs/specs/*.md pinned snapshots
  tools/doc5-citations.py --workspace DIR # override the workspace root

Regenerate the DOC-5 appendix with:
  tools/doc5-citations.py --appendix > /tmp/appendix.md
"""

import argparse
import hashlib
import os
import re
import subprocess
import sys
from collections import defaultdict

# A bare name, or a name with a path in front of it.  The lookbehind keeps
# `foo/bar.md` whole and refuses to start mid-identifier (so `tests/x.md` does
# not also yield `x.md`).
TOKEN = re.compile(r"(?<![\w/.\-+])((?:[A-Za-z0-9_.+\-]+/)*[A-Za-z0-9_][A-Za-z0-9_.+\-]*\.md)\b")

SCAN_ROOTS = ["src", "include", "plugins", "modules", "tests", "recording", "tools",
              "docs", "CMakeLists.txt", "cmake", "data", ".github"]

CODE_ROOTS = ("src/", "include/", "plugins/", "modules/", "recording/", "tools/",
              "cmake/", "data/", ".github/", "CMakeLists.txt")
EVIDENCE_ROOTS = ("tests/integration-logs", "tests/reference/", "tests/evidence-manifest.tsv")

SELF = {"tools/doc5-citations.py",     # the measuring instrument
        "docs/specs/README.md"}        # the DOC-5 record: it discusses citations, it is not one
WS_FALLBACK = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research"

# name -> why it is not a citation of a file that could be committed.
FALSE_POSITIVES = {
    "control.md":
        "the tail of `tests/agent-surface-negative-control.md` split by a line wrap "
        "(tests/agent-surface-gate.py:40)",
    "Home.md":
        "an element of upstream's VALID_CRUMBS file-name list, not a document "
        "(cmake/modules/CheckSubmodules.cmake:21)",
    "LICENSE.md":
        "the Ableton Link reference library's own licence file, not this repo's "
        "(include/LinkSync.h, src/core/ControlCommandsLink.cpp)",
    "VERSION.md":
        "a shell expansion — \"docs/RELEASE-NOTES-v$VERSION.md\" "
        "(tests/release-version-gate.sh:92)",
    "TAG.md":
        "a shell expansion — `docs/RELEASE-NOTES-$TAG.md` "
        "(docs/VERSION-0.2.1-ALPHA.md:53)",
    "build/zene-body.md":
        "a shell redirection TARGET in the release recipe, not a citation "
        "(docs/RELEASING.md:147)",
    "PLAN.md":
        "the abbreviated name of ableton-gap/PLAN-zene-studio.md inside the pinned "
        "snapshots docs/specs/AGENT-TOOLING.md and docs/specs/SPEC-zene-studio.md",
    "docs/KNOWN-LIMITATIONS-v0.2.0-alpha.md":
        "a path the citing documents themselves name as absent, on purpose "
        "(docs/INDEPENDENT-NOTES-READ.md:8, docs/AUDIT-FIX-PASS.md:178)",
}


def sh(args, cwd):
    return subprocess.run(args, cwd=cwd, capture_output=True, text=True)


def repo_root():
    out = sh(["git", "rev-parse", "--show-toplevel"], os.path.dirname(os.path.abspath(__file__)))
    if out.returncode != 0:
        sys.exit("not inside a git work tree")
    return out.stdout.strip()


def find_workspace(start):
    """The program workspace is the ancestor that holds AGENTS.md and ableton-gap/."""
    d = start
    for _ in range(6):
        d = os.path.dirname(d)
        if not d or d == "/":
            break
        if os.path.isfile(os.path.join(d, "AGENTS.md")) and os.path.isdir(os.path.join(d, "ableton-gap")):
            return d
    return None


def tracked(repo):
    out = sh(["git", "ls-files"], repo)
    return [l for l in out.stdout.split("\n") if l] if out.returncode == 0 else []


def by_base(paths):
    index = defaultdict(list)
    for p in paths:
        index[os.path.basename(p)].append(p)
    return index


def md_on_disk(root, maxdepth):
    if not os.path.isdir(root):
        return []
    out = sh(["find", root, "-maxdepth", str(maxdepth), "-name", "*.md",
              "-not", "-path", "*/build*", "-not", "-path", "*/.git/*"], "/")
    return [l for l in out.stdout.split("\n") if l]


def cite_class(path):
    if path in EVIDENCE_ROOTS or path.startswith("tests/integration-logs"):
        return "evidence"
    if path.startswith("tests/src/"):
        # the product's own test code: registered in tests/fork-sources.txt and
        # compiled into the suite, so a citation there is a code citation.
        return "code"
    if path.startswith(CODE_ROOTS):
        return "code"
    if path.startswith("docs/"):
        return "docs"
    if path.startswith("tests/"):
        return "harness"
    return "other"


def scan_citations(product):
    """token -> {citing file}, read out of the tree with one git grep."""
    grep = sh(["git", "grep", "-I", "-n", "-E", r"[A-Za-z0-9_][A-Za-z0-9_./+\-]*\.md", "--"]
              + SCAN_ROOTS, product)
    if grep.returncode not in (0, 1):
        sys.exit("git grep failed: " + grep.stderr)
    citers = defaultdict(set)
    for line in grep.stdout.split("\n"):
        parts = line.split(":", 2)
        if len(parts) != 3:
            continue
        path, _, content = parts
        if path in SELF:
            continue
        for m in TOKEN.finditer(content):
            citers[m.group(1)].add(path)
    return citers


def product_any_branch(product, base):
    """(loc, where) for a name that no branch of HEAD has: was it ever committed?"""
    # Two pathspecs, both needed: a pattern with no wildcard matches only the
    # repo-root copy, and `*/x.md` matches any path that ends with the literal
    # `/x.md` (git's `*` crosses slashes).  Plain `*x.md` would also match
    # `docs_x.md`, which is how a merge-side snapshot `docs_KNOWN-LIMITATIONS.md`
    # once passed for `KNOWN-LIMITATIONS.md`.
    added = sh(["git", "log", "--all", "--oneline", "--diff-filter=A", "--",
                base, "*/" + base], product).stdout.strip().split("\n")
    added = [l for l in added if l]
    if not added:
        return None
    sha = added[-1].split()[0]
    names = sh(["git", "branch", "--contains", sha], product).stdout
    names = " ".join(l.strip(" *+ ") for l in names.split("\n") if l.strip())
    if len(names.split()) > 3:
        names = "%s +%d branches" % (" ".join(names.split()[:3]), len(names.split()) - 3)
    return "product@", names


def locate(token, indexes):
    """Where does this cited name live?  product -> workspace -> any branch -> disk."""
    base = os.path.basename(token)
    for loc, index in (("product", indexes["prod"]), ("workspace", indexes["ws"])):
        if base in index:
            return loc, index[base][0]
    found = product_any_branch(indexes["product"], base)
    if found:
        return found
    if base in indexes["disk"]:
        return "disk", indexes["disk"][base][0]
    return "NEITHER", ""


def build_row(token, files, indexes):
    classes = defaultdict(list)
    for f in sorted(files):
        classes[cite_class(f)].append(f)
    loc, where = locate(token, indexes)
    return {
        "token": token, "base": os.path.basename(token), "loc": loc, "where": where,
        "n_code": len(classes["code"]), "n_docs": len(classes["docs"]),
        "n_evid": len(classes["evidence"]),
        "code": ";".join(classes["code"]), "docs": ";".join(classes["docs"]),
        "evid": ";".join(classes["evidence"]),
        "other": ";".join(classes["harness"] + classes["other"]),
        "fp": FALSE_POSITIVES.get(os.path.basename(token), ""),
    }


def row_sort_key(r):
    return (r["loc"] != "NEITHER", r["loc"] != "product@", -r["n_code"], -r["n_docs"], r["base"])


def is_spec(r):
    """Rows that belong in the DOC-5 list: every specification name, every name
    product code cites, and every unresolved name that is not a known false
    positive.  Rows only a frozen log cites are excluded — a merge log citing the
    merge's own side-file is a record of that merge."""
    if r["fp"]:
        return False
    if r["base"].upper().startswith("SPEC") or r["token"].startswith("specs/") \
            or r["token"].startswith("docs/specs/"):
        return True
    if r["n_code"]:
        return True
    if r["loc"] in ("NEITHER", "disk", "product@"):
        return bool(r["n_docs"]) or bool(r["n_evid"] == 0 and r["n_docs"] == 0)
    return False


def render_appendix(rows, specs_only):
    print("| cited name | location | code | docs | evidence | where it lives |")
    print("|---|---|---:|---:|---:|---|")
    for r in rows:
        if specs_only and not is_spec(r):
            continue
        loc, where = r["loc"], r["where"]
        if r["fp"]:
            loc, where = "false positive", r["fp"]
        print("| `%s` | %s | %d | %d | %d | %s |" % (
            r["token"], loc, r["n_code"], r["n_docs"], r["n_evid"],
            (where or "—").replace("|", "\\|")))


def render_tsv(rows, specs_only):
    print("TOKEN\tLOC\tLOCATION\tCODE\tDOCS\tEVID\tCITING_CODE\tEVIDENCE")
    for r in rows:
        if specs_only and not is_spec(r):
            continue
        print("\t".join([r["token"], r["loc"], r["where"], str(r["n_code"]), str(r["n_docs"]),
                         str(r["n_evid"]), r["code"], r["evid"]]))


def snapshot_header(blob):
    """(source, sha256, bytes) out of a pinned-snapshot header, or None."""
    head = blob.split(b"-->\n", 1)[0].decode()
    m_src = re.search(r"source\s*:\s*the program workspace,\s*(\S+)", head)
    m_sha = re.search(r"sha256\s*:\s*([0-9a-f]{64})", head)
    m_bytes = re.search(r"bytes\s*:\s*(\d+)", head)
    if not (m_src and m_sha and m_bytes):
        return None
    return m_src.group(1), m_sha.group(1), int(m_bytes.group(1))


def check_snapshot(name, blob, workspace):
    """Three claims per snapshot: the body is what the header says, the body is the
    workspace copy, and that copy still hashes to the header's value."""
    header = snapshot_header(blob)
    if header is None:
        return "%s no pinned-snapshot header (committed as a live document)" % name, True
    src, want_sha, want_bytes = header
    body = blob.split(b"-->\n", 1)[1]
    got = hashlib.sha256(body).hexdigest()
    try:
        ws_body = open(os.path.join(workspace, src), "rb").read()
    except FileNotFoundError:
        return "%s FAIL: workspace source %s is gone" % (name, src), False
    ok = (got == want_sha and len(body) == want_bytes and body == ws_body
          and hashlib.sha256(ws_body).hexdigest() == want_sha)
    line = "%-34s %s  sha256 %s  %d bytes  source %s" % (
        name, "ok  " if ok else "FAIL", got[:16], len(body), src)
    return line, ok


def verify_snapshots(product, workspace):
    d = os.path.join(product, "docs/specs")
    verified = live = bad = 0
    for name in sorted(os.listdir(d)):
        if not name.endswith(".md") or name == "README.md":
            continue
        blob = open(os.path.join(d, name), "rb").read()
        line, ok = check_snapshot(name, blob, workspace)
        print(line)
        if snapshot_header(blob) is None:
            live += 1
            continue
        verified += 1
        bad += 0 if ok else 1
    print("\n%d pinned snapshot(s) verified, %d disagree; %d live document(s) skipped"
          % (verified, bad, live))
    return 1 if bad else 0


def make_indexes(product, workspace, knowledge):
    prod = tracked(product)
    ws = tracked(workspace)
    disk = defaultdict(list)
    for root, depth in ((workspace, 4), (knowledge, 4)):
        for p in md_on_disk(root, depth):
            disk[os.path.basename(p)].append(p)
    return {"product": product, "prod": by_base(prod), "ws": by_base(ws), "disk": disk}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--appendix", action="store_true", help="emit the markdown table")
    ap.add_argument("--specs", action="store_true", help="specification rows only")
    ap.add_argument("--verify-snapshots", action="store_true",
                    help="check every docs/specs/*.md pinned snapshot against its header "
                         "and against the workspace copy it was taken from")
    ap.add_argument("--workspace", default=None)
    args = ap.parse_args()

    product = repo_root()
    workspace = args.workspace or find_workspace(product) or WS_FALLBACK
    knowledge = os.path.join(os.path.dirname(os.path.dirname(workspace)), "knowledge")

    if args.verify_snapshots:
        sys.exit(verify_snapshots(product, workspace))

    indexes = make_indexes(product, workspace, knowledge)
    rows = [build_row(t, f, indexes) for t, f in scan_citations(product).items()]
    rows.sort(key=row_sort_key)

    if args.appendix:
        render_appendix(rows, args.specs)
    else:
        render_tsv(rows, args.specs)


if __name__ == "__main__":
    main()
