#!/usr/bin/env python3
"""Merge train 3F: run a manifest's OWN documented command, extracted from its header.

Rather than transcribing the command (which can drift from the file), this reads the
command block under "Regenerate with" out of the manifest's own header text, strips the
leading '#', substitutes the revision, and runs it.  Nothing about the command lives in
this script - if the file's header changes, the command changes with it.

usage: run_manifest_cmd.py <manifest> [HEAD|INDEX]
  HEAD   -> the command as the header writes it, against the revision HEAD
  INDEX  -> the same, against the index (the pre-commit form: the added files come from
            `git diff --name-only --cached --diff-filter=A`, which is what a merge that
            has not been committed yet needs)
"""
import re
import subprocess
import sys

W = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-foreign"


def extract(path):
    """The command text under 'Regenerate with', as the header writes it."""
    lines = open(f"{W}/{path}", encoding="utf-8").read().splitlines()
    start = None
    for i, l in enumerate(lines):
        if l.startswith("#") and re.match(r"#\s*Regenerate\b", l):
            start = i + 1
            break
    if start is None:
        raise SystemExit(f"{path}: no 'Regenerate' block")
    block = []
    for l in lines[start:]:
        if not l.startswith("#"):
            break
        if "git " not in l and not block:
            continue
        body = l[1:]
        if body.startswith(" ") or body.startswith("\t"):
            body = body[1:] if body.startswith(" ") else body
        block.append(body)
        if "LC_ALL=C sort" in body:
            break
    return " ".join(x.rstrip("\\").rstrip() for x in block)


def run(path, rev):
    cmd = extract(path)
    if rev == "INDEX":
        cmd = cmd.replace("--diff-filter=A 4e677cb6c6ab HEAD",
                          "--diff-filter=A --cached 4e677cb6c6ab")
    elif rev != "HEAD":
        cmd = cmd.replace("HEAD", rev)
    r = subprocess.run(["bash", "-c", cmd], cwd=W, capture_output=True, text=True)
    if r.returncode != 0:
        print("COMMAND FAILED", r.returncode, r.stderr[:500])
        raise SystemExit(2)
    return cmd, [l for l in r.stdout.splitlines() if l.strip()]


def entries(path):
    txt = open(f"{W}/{path}", encoding="utf-8").read().splitlines()
    k = next((i for i, l in enumerate(txt) if l.strip() and not l.lstrip().startswith("#")), len(txt))
    return [l for l in txt[k:] if l.strip()]


def write_body(path, new):
    """Rewrite the manifest as header + the command's own output (its documented shape)."""
    txt = open(f"{W}/{path}", encoding="utf-8").read().splitlines()
    k = next((i for i, l in enumerate(txt) if l.strip() and not l.lstrip().startswith("#")), len(txt))
    open(f"{W}/{path}", "w", encoding="utf-8").write(
        "\n".join(txt[:k] + sorted(new, key=lambda x: x.encode())) + "\n")


if __name__ == "__main__":
    manifest, rev = sys.argv[1], (sys.argv[2] if len(sys.argv) > 2 else "HEAD")
    cmd, new = run(manifest, rev)
    cur = entries(manifest)
    added = [e for e in new if e not in set(cur)]
    removed = [e for e in cur if e not in set(new)]
    print(f"manifest {manifest}  rev={rev}  entries={len(cur)}  command prints={len(new)}  "
          f"dupes={len(new) - len(set(new))}  +{len(added)} -{len(removed)}  "
          f"{'REPRODUCES' if not added and not removed else 'DOES-NOT-REPRODUCE'}")
    for e in added:
        print("   +", e)
    for e in removed:
        print("   -", e)
    print("   command:", cmd[:200], "...")
    if "--write" in sys.argv:
        write_body(manifest, new)
        print(f"   WROTE {len(new)} entries as header + the command's output")
    sys.exit(0 if not added and not removed else 1)
