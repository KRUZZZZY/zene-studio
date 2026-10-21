#!/usr/bin/env python3
"""Plumbing of the third-party plug-in compatibility matrix (Q050-071): subjects, packages, hashes, outcomes, the page.

The runner is tests/plugin_compat_matrix.py; the gate that keeps the published page honest is
tests/plugin-compat-matrix-gate.py. Both import this module.

A matrix row is one pinned third-party plug-in (a package at a version, a file at a sha256) run through the same
registered commands an agent uses. Every cell is one of four outcomes, and the four mean different things:

  pass         the host did it and it was observed (loaded and listed; editor opened, drew and closed; state restored)
  fail         the host tried and it went wrong (refused, crashed, did not restore) - the detail says how
  unsupported  the host declines by design (an editor path it does not implement), or the plug-in has none
  not_run      it was not measured (no display, nothing to compare, an earlier step failed) - never read as a pass

Third-party rows are kept apart from the in-tree fixtures: the fixtures are tests (listed in the page's second section)
and never appear as rows.
"""
import hashlib
import json
import os
import pathlib
import subprocess
import tarfile
import urllib.request

OUTCOMES = ("pass", "fail", "unsupported", "not_run")
STEPS = ("listed", "load", "editor", "state")
STEP_TITLES = {"listed": "Listed", "load": "Load", "editor": "Editor", "state": "State round-trip"}
FORMAT_ORDER = {"vst3": 0, "clap": 1, "lv2": 2}
REPO = pathlib.Path(__file__).resolve().parent.parent
SUBJECTS = REPO / "docs" / "plugin-compat" / "subjects.json"
RESULTS = REPO / "docs" / "plugin-compat" / "results-linux-x86_64.json"
PAGE = REPO / "docs" / "PLUGIN-COMPATIBILITY.md"


def load_json(path):
    with open(path) as handle:
        return json.load(handle)


def write_json(path, data):
    with open(path, "w") as handle:
        json.dump(data, handle, indent=1, sort_keys=True)
        handle.write("\n")


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def inside_package(package_root, path):
    """A package may ship a symlink to an absolute path of the system it was built for; read it inside the unpacked package."""
    if path.is_symlink() and not path.exists():
        target = os.readlink(path)
        return pathlib.Path(package_root) / target.lstrip("/") if target.startswith("/") else path
    return path


def binary_of(root, subject):
    """The plug-in's own binary under the extracted package: the file for a CLAP, the DSP library inside a bundle."""
    package_root = pathlib.Path(root) / subject["package_dir"]
    path = package_root / subject.get("binary", subject["file"])
    if path.is_file():
        return path
    libraries = [p for p in sorted(path.rglob("*")) if p.suffix in (".so", ".clap") and "ui" not in p.stem.lower()] if path.is_dir() else []
    return inside_package(package_root, libraries[0]) if libraries else None


def fetch_package(name, package, root, cache):
    """Download one pinned package into \a cache and unpack it into its own directory under \a root."""
    os.makedirs(cache, exist_ok=True)
    archive = pathlib.Path(cache) / package["url"].rsplit("/", 1)[1]
    if not archive.exists():
        urllib.request.urlretrieve(package["url"], archive)
    if sha256_of(archive) != package["sha256"]:
        raise SystemExit("%s: %s does not match its pinned sha256 - the mirror changed it" % (name, archive))
    target = pathlib.Path(root) / package["dir"]
    if not target.exists():
        target.mkdir(parents=True)
        subprocess.run(["bsdtar", "-xf", str(archive), "-C", str(target)], check=True)
        repair_symlinks(target)
    return target


def repair_symlinks(package_root):
    """A package's absolute symlinks (/usr/lib/calf/libcalf.so) point into the system it was built for: make the ones that
    name a file of the package itself relative, so the plug-in loads from where it was unpacked."""
    for link in pathlib.Path(package_root).rglob("*"):
        if not link.is_symlink():
            continue
        target = os.readlink(link)
        inside = pathlib.Path(package_root) / target.lstrip("/")
        if target.startswith("/") and inside.exists():
            link.unlink()
            link.symlink_to(os.path.relpath(inside, link.parent))


def cell(outcome, detail=""):
    return {"outcome": outcome, "detail": detail}


def outcome_of(row, step):
    return row["steps"].get(step, cell("not_run", "not measured"))["outcome"]


def case_names(subject):
    """The named regression cases of a subject: one per step, stable across builds."""
    return ["%s/%s" % (subject["id"], step) for step in STEPS]


def compare_results(baseline, current):
    """Per named case: what the baseline said, what this run says, for every case whose outcome moved."""
    old = {"%s/%s" % (r["id"], s): outcome_of(r, s) for r in baseline["rows"] for s in STEPS}
    changes = []
    for row in current["rows"]:
        for step in STEPS:
            name = "%s/%s" % (row["id"], step)
            now = outcome_of(row, step)
            if old.get(name) != now:
                changes.append({"case": name, "was": old.get(name, "absent"), "now": now,
                                "detail": row["steps"].get(step, {}).get("detail", "")})
    return changes


def sorted_rows(results):
    return sorted(results["rows"], key=lambda r: (FORMAT_ORDER.get(r["format"], 9), r["kind"], r["title"]))


def package_line(row, packages):
    package = packages[row["package"]]
    line = "%s %s (%s)" % (row["package"], package["version"], package["repo"])
    for need in row.get("needs", []):
        line += "; needs %s %s" % (need, packages[need]["version"])
    return line


def known_failures(rows):
    """Every non-pass, non-unsupported cell of the rows, with the subject and the detail that explains it."""
    out = []
    for row in rows:
        for step in STEPS:
            entry = row["steps"].get(step, cell("not_run", ""))
            if entry["outcome"] in ("fail", "not_run"):
                out.append((row["id"], step, entry["outcome"], entry["detail"]))
    return out
