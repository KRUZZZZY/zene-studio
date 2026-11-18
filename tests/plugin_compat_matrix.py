#!/usr/bin/env python3
"""Run the third-party plug-in compatibility matrix (Q050-071) against a Zene build and write the results.

  python3 tests/plugin_compat_matrix.py --zene build/zene --probe build/tests/PluginCompatEditorProbe \
      --root /tmp/zene-matrix/root --fetch /tmp/zene-matrix/pkgs --display :97 --out docs/plugin-compat/results-linux-x86_64.json
  python3 tests/plugin_compat_matrix.py ... --baseline docs/plugin-compat/results-linux-x86_64.json   # the next build: rerun and diff
  python3 tests/plugin_compat_matrix.py ... --pin                                                   # record the binaries' sha256 in subjects.json
  python3 tests/plugin_compat_matrix.py --render                                                    # rewrite docs/PLUGIN-COMPATIBILITY.md

Each subject (docs/plugin-compat/subjects.json) is a pinned package at a version; --fetch downloads and verifies it, --root
is where the packages are unpacked, one directory each, outside the repository. One fresh Zene instance per subject (a host
crash is that subject's failure, not the run's): listing, load and the state round-trip go over the control socket; the
editor goes through tests/src/gui/PluginCompatEditorProbe.cpp, because plugin.editor_open refuses on a socket-driven
instance. What a cell means is in tests/plugin_compat_lib.py. The in-tree fixtures are not run here and never appear as rows.
"""
import argparse
import datetime
import json
import os
import pathlib
import platform
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import plugin_compat_lib as L  # noqa: E402
import plugin_compat_page as P  # noqa: E402
import plugin_compat_steps as S  # noqa: E402


def environment(args):
    release = {}
    with open("/etc/os-release") as handle:
        release = dict(line.rstrip().split("=", 1) for line in handle if "=" in line)
    return {"os": release.get("PRETTY_NAME", "").strip('"'), "kernel": platform.release(), "machine": platform.machine(),
            "display": args.display_note, "zene_binary_sha256": L.sha256_of(args.zene),
            "date_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")}


def run_all(args, subjects):
    rows = []
    for subject in subjects["subjects"]:
        binary = L.binary_of(args.root, subject)
        row = {key: subject[key] for key in ("id", "title", "format", "kind", "package")}
        row["host_modules"] = subject["host_modules"]
        row["needs"] = subject.get("needs", [])
        row["binary"] = subject["file"]
        row["binary_sha256"] = L.sha256_of(binary) if binary else "missing"
        row["steps"] = S.measure(args, subject)
        rows.append(row)
        print("%-28s %s" % (subject["id"], " ".join("%s=%s" % (s, L.outcome_of(row, s)) for s in L.STEPS)), flush=True)
    return rows


def report_changes(changes):
    worse = [c for c in changes if c["was"] == "pass" and c["now"] != "pass"]
    for change in changes:
        print("CHANGED %-40s %s -> %s %s" % (change["case"], change["was"], change["now"], change["detail"][:80]))
    print("%d case(s) changed, %d regression(s)" % (len(changes), len(worse)))
    return 1 if worse else 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--zene")
    parser.add_argument("--probe")
    parser.add_argument("--root", default="/tmp/zene-matrix/root")
    parser.add_argument("--fetch", help="download and verify the pinned packages into this directory first")
    parser.add_argument("--display", default="", help="an X display for the editor probe (empty: editors are not_run)")
    parser.add_argument("--display-note", default="none")
    parser.add_argument("--subjects", default=str(L.SUBJECTS))
    parser.add_argument("--out")
    parser.add_argument("--baseline")
    parser.add_argument("--only", action="append", default=[], help="run only this subject id (repeatable)")
    parser.add_argument("--pin", action="store_true", help="record each plug-in binary's sha256 in subjects.json after the run")
    parser.add_argument("--render", action="store_true")
    args = parser.parse_args()
    if args.render:
        P.write_page(L.load_json(L.RESULTS), L.load_json(L.SUBJECTS))
        return 0
    subjects = L.load_json(args.subjects)
    if args.only:
        subjects["subjects"] = [s for s in subjects["subjects"] if s["id"] in args.only]
    if args.fetch:
        for name, package in subjects["packages"].items():
            L.fetch_package(name, package, args.root, args.fetch)
    results = {"schema": 1, "run": environment(args), "rows": run_all(args, subjects)}
    results["run"]["zene_version"] = S.zene_version(args)
    if args.out:
        L.write_json(args.out, results)
    if args.pin:
        pins = {row["id"]: row["binary_sha256"] for row in results["rows"]}
        full = L.load_json(args.subjects)
        for subject in full["subjects"]:
            subject["binary_sha256"] = pins.get(subject["id"], subject.get("binary_sha256", ""))
        L.write_json(args.subjects, full)
    return report_changes(L.compare_results(L.load_json(args.baseline), results)) if args.baseline else 0


if __name__ == "__main__":
    sys.exit(main())
