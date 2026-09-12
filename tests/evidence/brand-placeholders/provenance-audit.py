#!/usr/bin/env python3
"""Provenance audit: is each shipped identity image still upstream LMMS artwork?

Every verdict here is a command result, never an impression:

  * "byte-identical" is `git show origin/master:<path> | cmp - <path>` (exit 0),
    using the pre-rename upstream path for files git records as renames;
  * "drawing data identical" compares the `<path d="...">` payload of the two SVGs,
    which is what a rename that edits <dc:title> leaves behind;
  * the metadata column is read out of the file (SVG RDF, PNG tEXt/iTXt chunks).

Usage: python3 provenance-audit.py        (writes to stdout; captured as provenance-audit.txt)
"""

from __future__ import annotations

import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
os.chdir(ROOT)

SVG_ART = re.compile(r'<path[^>]*\bd="([^"]*)"')


def run(cmd: str) -> bytes:
    return subprocess.run(cmd, shell=True, capture_output=True).stdout


def git_bytes(rev_path: str) -> bytes | None:
    r = subprocess.run(f"git show {rev_path}", shell=True, capture_output=True)
    return r.stdout if r.returncode == 0 else None


# Provenance is a question about the state BEFORE the placeholder change, so the
# "shipped" bytes come from a revision, not the working tree.  The default is the branch
# this placeholder work was based on; override with SHIPPED_REV=<rev> to audit any other
# point (and note the rename map is only meaningful on a revision that still carries the
# upstream artwork - replace the art and git stops pairing the renames).
def _default_shipped() -> str:
    if os.environ.get("SHIPPED_REV"):
        return os.environ["SHIPPED_REV"]
    for cand in ("post-alpha/rename-complete", "HEAD~2"):
        r = subprocess.run(f"git rev-parse --verify --quiet {cand}^{{commit}}",
                           shell=True, capture_output=True)
        if r.returncode == 0:
            return cand
    return "HEAD"


SHIPPED = _default_shipped()


def shipped_bytes(path: str) -> bytes | None:
    if SHIPPED:
        return git_bytes(f"{SHIPPED}:{path}")
    try:
        with open(path, "rb") as fh:
            return fh.read()
    except OSError:
        return None


def identical_to(upstream_rel: str, path: str) -> bool:
    up = git_bytes(f"origin/master:{upstream_rel}")
    cur = shipped_bytes(path)
    return up is not None and cur is not None and cur == up


def printable(t: str) -> str:
    return "".join(c if 32 <= ord(c) < 127 else "." for c in t)


def png_meta(path: str) -> str:
    d = shipped_bytes(path) or b""
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        return "(not a PNG)"
    out, i = [], 8
    while i + 12 <= len(d):
        ln = struct.unpack(">I", d[i:i + 4])[0]
        typ = d[i + 4:i + 8].decode("latin1")
        if typ in ("tEXt", "iTXt", "zTXt"):
            body = d[i + 8:i + 8 + ln].decode("latin-1", "replace")
            out.append(f"{typ}: {body[:60]}")
        if typ == "IHDR":
            w, h = struct.unpack(">II", d[i + 8:i + 16])
            out.append(f"IHDR {w}x{h} bit={d[i+16]} colourtype={d[i+17]}")
        i += 12 + ln
    return printable(" | ".join(out)) if out else "(no text chunks)"


def svg_meta(path: str) -> str:
    txt = (shipped_bytes(path) or b"").decode("utf-8", "replace")
    bits = re.findall(r"<dc:(title|creator|description|rights|date)>([^<]*)", txt)
    cc = re.findall(r'cc:License rdf:about="([^"]*)"', txt)
    doc = re.findall(r'sodipodi:docname="([^"]*)"', txt)
    exp = re.findall(r'inkscape:export-filename="([^"]*)"', txt)
    parts = [f"{k}={v.strip()}" for k, v in bits]
    if cc:
        parts.append("license=" + cc[0])
    if doc:
        parts.append(f"sodipodi:docname={doc[0]}")
    if exp:
        parts.append(f"inkscape:export-filename={exp[0]}")
    return " | ".join(parts) if parts else "(no RDF metadata)"


def svg_art_md5(rev_path: str) -> str | None:
    import hashlib
    d = git_bytes(rev_path)
    if d is None:
        return None
    m = SVG_ART.search(d.decode("utf-8", "replace"))
    return hashlib.md5(m.group(1).encode()).hexdigest() if m else None


def rename_map() -> dict[str, str]:
    """dst -> src for every rename upstream->SHIPPED, from git's own -M detection.

    The map is taken at the PRE-CHANGE revision on purpose: replacing the artwork makes
    the content dissimilar enough that git stops pairing the rename, and the pairing is
    exactly what tells us which upstream file a shipped file came from.
    """
    out = run(f"git diff --name-status -M origin/master..{SHIPPED}").decode()
    ren = {}
    for line in out.splitlines():
        if line.startswith("R"):
            _, src, dst = line.split("\t")
            ren[dst] = src
    return ren


IDENTITY = [
    "data/themes/default/zene-plugin-logo.svg",
    "cmake/linux/icons/scalable/apps/zene.svg",
    "cmake/linux/icons/scalable/mimetypes/application-x-zene-project.svg",
    "cmake/nsis/assets/Logo.png",
    "cmake/nsis/assets/SmallLogo.png",
    "cmake/nsis/icon.ico",
    "cmake/nsis/project.ico",
    "cmake/apple/icon.icns",
    "cmake/apple/project.icns",
    "cmake/apple/background.png",
    "cmake/apple/background@2x.png",
    "data/themes/default/splash.png",
    "data/backgrounds/vinnie.png",
    "data/backgrounds/newbg.png",
    "data/backgrounds/zene_tile.png",
]
ICON_DIRS = ["16x16", "16x16@2", "24x24", "24x24@2", "32x32", "32x32@2", "48x48", "48x48@2",
             "64x64", "64x64@2", "128x128", "128x128@2", "256x256"]
for d in ICON_DIRS:
    IDENTITY.append(f"cmake/linux/icons/{d}/apps/zene.png")
    # Renamed 2026-09-12 from application-x-lmms-project.png so the raster icon name matches
    # the declared MIME type application/x-zene-project at every size; the pixel bytes did not
    # change with the rename.  Auditing a revision older than that rename finds the same file
    # at .../application-x-lmms-project.png -- see docs/QDEBUG-CLASS-AND-MIME-RENAME.md.
    IDENTITY.append(f"cmake/linux/icons/{d}/mimetypes/application-x-zene-project.png")


def classify(path: str, upstream_path: str) -> str:
    """Which provenance bucket this shipped file falls in. One verdict string per file."""
    if identical_to(path, path):
        return "identical_same_path"
    if upstream_path != path and identical_to(upstream_path, path):
        return "identical_renamed"
    if path.endswith(".svg"):
        art_current = svg_art_md5(f"{SHIPPED}:{path}")
        art_upstream = svg_art_md5(f"origin/master:{upstream_path}")
        if art_current and art_current == art_upstream:
            return "art_only"
    return "differs"


VERDICT_TEXT = {
    "identical_same_path": "IDENTICAL (same path)",
    "identical_renamed": "IDENTICAL (renamed)",
    "art_only": "ART IDENTICAL, metadata renamed",
    "differs": "DIFFERS",
}


def print_rename_map(ren: dict[str, str]) -> None:
    print(f"== Part A. Rename map origin/master -> {SHIPPED} (images only) ==")
    for dst, src in sorted(ren.items()):
        if dst.lower().endswith((".svg", ".png", ".ico", ".icns", ".xpm")):
            print(f"   {src}  ->  {dst}")
    print()


def audit_rows(ren: dict[str, str]) -> dict[str, int]:
    counts = {"identical_same_path": 0, "identical_renamed": 0, "art_only": 0, "differs": 0}
    print(f"== Part B. Identity-artwork provenance ({len(IDENTITY)} shipped files) ==")
    print(f"   (the shipped state audited is {SHIPPED!r}; set SHIPPED_REV to change it)")
    hdr = f"{'path':<68} {'bytes':>7}  {'vs origin/master':<34} metadata"
    print(hdr)
    print("-" * len(hdr))
    for path in sorted(IDENTITY):
        verdict = classify(path, ren.get(path, path))
        counts[verdict] += 1
        size = len(shipped_bytes(path) or b"")
        meta = svg_meta(path) if path.endswith(".svg") else png_meta(path)
        print(f"{path:<68} {size:>7}  {VERDICT_TEXT[verdict]:<34} {meta[:150]}")
    print()
    return counts


def print_reproduction() -> None:
    print("== Part D. Raw commands, for reproduction ==")
    print("   git show origin/master:<upstream-path> | cmp - <shipped-path>; echo $?")
    print(f"   git diff --name-status -M origin/master..{SHIPPED} | grep '^R'")
    print("   git diff origin/master:data/themes/default/lmms-plugin-logo.svg "
          f"{SHIPPED}:data/themes/default/zene-plugin-logo.svg")


def main() -> int:
    ren = rename_map()
    print_rename_map(ren)
    counts = audit_rows(ren)
    print("== Part C. Totals ==")
    for k in ("identical_same_path", "identical_renamed", "art_only", "differs"):
        print(f"   {k:<24} {counts[k]}")
    print()
    print_reproduction()
    return 0


if __name__ == "__main__":
    sys.exit(main())
