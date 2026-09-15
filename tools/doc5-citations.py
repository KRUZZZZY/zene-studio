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
            CMakeLists.txt .github/ and the test harness under tests/ that is
            not itself a frozen log
  docs      docs/
  evidence  tests/integration-logs-*/ tests/reference/ tests/evidence-manifest.tsv
            — frozen records of runs and merges.  Their citations are history,
            not claims about the tree; they are counted and never repaired.
  meta      tests/*.txt manifests and similar

Known false-positive families are listed in FALSE_POSITIVES below and are
reported as such rather than as dangling citations: a name inside a longer
identifier, a name split by a line wrap, a name built from a shell variable, an
external project's own licence file.

Usage:
  tools/doc5-citations.py                 # the full TSV
  tools/doc5-citations.py --appendix      # the markdown table for docs/specs/README.md
  tools/doc5-citations.py --specs         # only rows that are specifications
  tools/doc5-citations.py --workspace DIR # override the workspace root

Regenerate the DOC-5 appendix with:
  tools/doc5-citations.py --appendix > /tmp/appendix.md
"""

import argparse
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
    "PLAN.md":
        "the abbreviated name of ableton-gap/PLAN-zene-studio.md inside the pinned "
        "snapshots docs/specs/AGENT-TOOLING.md and docs/specs/SPEC-zene-studio.md",
    "TAG.md":
        "a shell expansion — `docs/RELEASE-NOTES-$TAG.md` "
        "(docs/VERSION-0.2.1-ALPHA.md:53)",
    "build/zene-body.md":
        "a shell redirection TARGET in the release recipe, not a citation "
        "(docs/RELEASING.md:147)",
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
    if out.returncode != 0:
        return []
    return [l for l in out.stdout.split("\n") if l]


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


def verify_snapshots(product, workspace):
    """Every docs/specs/*.md that carries the pinned-snapshot header must satisfy
    three claims: the body is what the header's sha256 and byte count say, the
    body is byte-identical to the workspace copy it names, and that workspace copy
    still hashes to the same value.  Exit 0 = all three hold for every file."""
    import hashlib
    import re
    d = os.path.join(product, "docs/specs")
    bad = 0
    n = 0
    for name in sorted(os.listdir(d)):
        if not name.endswith(".md") or name == "README.md":
            continue
        blob = open(os.path.join(d, name), "rb").read()
        if not blob.startswith(b"<!--\n  In-repo copy of the specification"):
            print("%-34s no pinned-snapshot header (committed as a live document)" % name)
            continue
        n += 1
        head, body = blob.split(b"-->\n", 1)
        head = head.decode()
        m_src = re.search(r"source\s*:\s*the program workspace,\s*(\S+)", head)
        m_sha = re.search(r"sha256\s*:\s*([0-9a-f]{64})", head)
        m_bytes = re.search(r"bytes\s*:\s*(\d+)", head)
        if not (m_src and m_sha and m_bytes):
            print("%-34s FAIL: the header is missing source/sha256/bytes" % name)
            bad += 1
            continue
        src, want_sha, want_bytes = m_src.group(1), m_sha.group(1), int(m_bytes.group(1))
        got = hashlib.sha256(body).hexdigest()
        ws_path = os.path.join(workspace, src)
        try:
            ws_body = open(ws_path, "rb").read()
        except FileNotFoundError:
            print("%-34s FAIL: workspace source %s is gone" % (name, src))
            bad += 1
            continue
        ok = (got == want_sha and len(body) == want_bytes and body == ws_body
              and hashlib.sha256(ws_body).hexdigest() == want_sha)
        print("%-34s %s  sha256 %s  %d bytes  source %s" %
              (name, "ok  " if ok else "FAIL", got[:16], len(body), src))
        if not ok:
            print("    header says %s / %d bytes; workspace copy hashes %s / %d bytes" %
                  (want_sha[:16], want_bytes, hashlib.sha256(ws_body).hexdigest()[:16],
                   len(ws_body)))
            bad += 1
    print("\n%d pinned snapshot(s) checked, %d disagree" % (n, bad))
    return 1 if bad else 0


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
    workspace = args.workspace or find_workspace(product) or \
        "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research"
    knowledge = os.path.join(os.path.dirname(os.path.dirname(workspace)), "knowledge")

    if args.verify_snapshots:
        sys.exit(verify_snapshots(product, workspace))

    prod = tracked(product)
    ws = tracked(workspace)
    prod_bases = defaultdict(list)
    for f in prod:
        prod_bases[os.path.basename(f)].append(f)
    ws_bases = defaultdict(list)
    for f in ws:
        ws_bases[os.path.basename(f)].append(f)

    stat = {}
    for other in ("docs/specs/README.md", "specs/"):
        stat[other] = os.path.exists(os.path.join(product, other))

    disk_bases = defaultdict(list)
    for root, depth in ((workspace, 4), (knowledge, 4)):
        for p in md_on_disk(root, depth):
            disk_bases[os.path.basename(p)].append(p)

    grep = sh(["git", "grep", "-I", "-n", "-E", r"[A-Za-z0-9_][A-Za-z0-9_./+\-]*\.md", "--"] + SCAN_ROOTS,
              product)
    if grep.returncode not in (0, 1):
        sys.exit("git grep failed: " + grep.stderr)

    citers = defaultdict(set)          # token -> {file}
    SELF = "tools/doc5-citations.py"   # the measuring instrument is not a citation source
    for line in grep.stdout.split("\n"):
        if not line:
            continue
        parts = line.split(":", 2)
        if len(parts) != 3:
            continue
        path, _, content = parts
        if path == SELF:
            continue
        for m in TOKEN.finditer(content):
            citers[m.group(1)].add(path)

    rows = []
    for token, files in citers.items():
        base = os.path.basename(token)
        code = sorted(f for f in files if cite_class(f) == "code")
        docs = sorted(f for f in files if cite_class(f) == "docs")
        evid = sorted(f for f in files if cite_class(f) == "evidence")
        other = sorted(f for f in files if cite_class(f) in ("harness", "other"))
        if base in prod_bases:
            loc, where = "product", prod_bases[base][0]
        elif base in ws_bases:
            loc, where = "workspace", ws_bases[base][0]
        else:
            # Two pathspecs, both needed: a pattern with no wildcard matches only
            # the repo-root copy, and `*/x.md` matches any path that ends with
            # the literal `/x.md` (git's `*` crosses slashes).  Plain `*x.md`
            # would also match `docs_x.md`, which is how a merge-side snapshot
            # `docs_KNOWN-LIMITATIONS.md` once passed for `KNOWN-LIMITATIONS.md`.
            added = sh(["git", "log", "--all", "--oneline", "--diff-filter=A", "--",
                        base, "*/" + base], product).stdout.strip().split("\n")
            added = [l for l in added if l]
            if added:
                sha = added[-1].split()[0]
                br = sh(["git", "branch", "--contains", sha], product).stdout
                names = " ".join(l.strip(" *+ ") for l in br.split("\n") if l.strip())
                if len(names.split()) > 3:
                    names = "%s +%d branches" % (" ".join(names.split()[:3]),
                                                 len(names.split()) - 3)
                loc, where = "product@", names
            elif disk_bases.get(base):
                loc, where = "disk", disk_bases[base][0]
            else:
                loc, where = "NEITHER", ""
        fp = FALSE_POSITIVES.get(base, "")
        rows.append({
            "token": token, "base": base, "loc": loc, "where": where,
            "n_code": len(code), "n_docs": len(docs), "n_evid": len(evid),
            "code": ";".join(code), "docs": ";".join(docs), "evid": ";".join(evid),
            "other": ";".join(other), "fp": fp,
        })

    def is_spec(r):
        """Rows that belong in the DOC-5 list: every specification name, every
        name product code cites, and every unresolved name that is not a known
        false positive.  Rows only a frozen log cites are excluded — a merge log
        citing the merge's own side-file is a record of that merge."""
        if r["fp"]:
            return False
        if r["base"].upper().startswith("SPEC") or r["token"].startswith("specs/") \
                or r["token"].startswith("docs/specs/"):
            return True
        if r["n_code"]:
            return True
        if r["loc"] in ("NEITHER", "disk", "product@"):
            return bool(r["docs"]) or bool(r["n_evid"] == 0 and r["n_docs"] == 0)
        return False

    rows.sort(key=lambda r: (r["loc"] != "NEITHER", r["loc"] != "product@", -r["n_code"],
                             -r["n_docs"], r["base"]))

    if args.appendix:
        print("| cited name | location | code | docs | evidence | where it lives |")
        print("|---|---|---:|---:|---:|---|")
        for r in rows:
            if args.specs and not is_spec(r):
                continue
            loc = r["loc"]
            if r["fp"]:
                loc = "false positive"
                r = dict(r, where=r["fp"])
            print("| `%s` | %s | %d | %d | %d | %s |" % (
                r["token"], loc, r["n_code"], r["n_docs"], r["n_evid"],
                (r["where"] or "—").replace("|", "\\|")))
        return

    print("TOKEN\tLOC\tLOCATION\tCODE\tDOCS\tEVID\tCITING_CODE\tEVIDENCE")
    for r in rows:
        if args.specs and not is_spec(r):
            continue
        print("\t".join([r["token"], r["loc"], r["where"], str(r["n_code"]), str(r["n_docs"]),
                         str(r["n_evid"]), r["code"], r["evid"]]))


if __name__ == "__main__":
    main()
