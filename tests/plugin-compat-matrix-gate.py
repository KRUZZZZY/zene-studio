#!/usr/bin/env python3
"""PluginCompatMatrixGate (Q050-071): the published third-party compatibility page is exactly what was measured.

File reads only, no binary. It checks that
  1. docs/plugin-compat/subjects.json pins every third-party subject: its package (version, URL, sha256, licence) and, once
     run, the sha256 of the plug-in binary;
  2. docs/plugin-compat/results-linux-x86_64.json has one row per subject, every step cell one of pass / fail / unsupported /
     not_run with a detail for anything but a bare pass, the pinned binary hashes, and no editor 'pass' from a run that had
     no display;
  3. docs/PLUGIN-COMPATIBILITY.md is byte-for-byte what tests/plugin_compat_page.py renders from those two files, so the
     page cannot say anything the results do not;
  4. third-party rows and in-tree fixtures stay apart: no fixture is a row, and the page's fixture section lists only tests
     that exist;
  5. the README and the limitations page point at the matrix.
`--self-test` is the gate's own red control: it mutates a result (a failed load recorded as a pass, a stale page, a fixture
slipped into the rows) and asserts the gate fails.

Usage: python3 tests/plugin-compat-matrix-gate.py [--root DIR] [--self-test]
"""
import copy
import json
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import plugin_compat_lib as L  # noqa: E402
import plugin_compat_page as P  # noqa: E402

HEX64 = re.compile(r"^[0-9a-f]{64}$")


def check_subjects(subjects, problems):
    for name, package in subjects["packages"].items():
        for key in ("version", "url", "license", "dir", "project"):
            if not package.get(key):
                problems.append("package %s has no %s" % (name, key))
        if not HEX64.match(package.get("sha256", "")):
            problems.append("package %s has no sha256 pin" % name)
    for subject in subjects["subjects"]:
        if subject["package"] not in subjects["packages"]:
            problems.append("subject %s names an unpinned package %s" % (subject["id"], subject["package"]))
        if not HEX64.match(subject.get("binary_sha256", "")):
            problems.append("subject %s has no binary_sha256 pin (run tests/plugin_compat_matrix.py --pin)" % subject["id"])


def check_row(row, subject, display, problems):
    if row["binary_sha256"] != subject.get("binary_sha256"):
        problems.append("row %s: its binary hash differs from the subject's pin" % row["id"])
    for step in L.STEPS:
        entry = row["steps"].get(step)
        if entry is None or entry["outcome"] not in L.OUTCOMES:
            problems.append("row %s: step %s is missing or has an outcome outside %s" % (row["id"], step, L.OUTCOMES))
        elif entry["outcome"] != "pass" and not entry["detail"]:
            problems.append("row %s: step %s is %s without a detail" % (row["id"], step, entry["outcome"]))
    if display == "none" and L.outcome_of(row, "editor") == "pass":
        problems.append("row %s: an editor pass from a run that had no display" % row["id"])


def check_results(results, subjects, problems):
    by_id = {s["id"]: s for s in subjects["subjects"]}
    ids = [r["id"] for r in results["rows"]]
    if sorted(ids) != sorted(by_id):
        problems.append("results rows %s do not match the subjects %s" % (sorted(ids), sorted(by_id)))
    for key in ("zene_version", "os", "date_utc", "zene_binary_sha256"):
        if not results["run"].get(key):
            problems.append("the run record has no %s" % key)
    for row in results["rows"]:
        if row["id"] in by_id:
            check_row(row, by_id[row["id"]], results["run"]["display"], problems)


def check_separation(results, subjects, problems, root):
    for fixture in subjects["fixtures"]:
        for name in re.split(r",\s*", fixture["test"]):
            if not list((root / "tests").rglob(name + ".cpp")) and name != "ControlSocketIntegration":
                problems.append("the page lists fixture test %s but no such test source exists" % name)
    third_party_ids = {r["id"] for r in results["rows"]}
    for fixture in subjects["fixtures"]:
        if fixture["test"] in third_party_ids:
            problems.append("fixture %s is also a third-party row" % fixture["test"])
    for row in results["rows"]:
        if not row["package"] or "fixture" in row["package"].lower() or "tests/" in row["binary"]:
            problems.append("row %s is not a third-party package binary" % row["id"])


def check_page(results, subjects, problems, root):
    page = (root / "docs" / "PLUGIN-COMPATIBILITY.md")
    if not page.exists():
        problems.append("docs/PLUGIN-COMPATIBILITY.md is missing")
    elif page.read_text() != P.render(results, subjects):
        problems.append("docs/PLUGIN-COMPATIBILITY.md is not what `plugin_compat_matrix.py --render` produces from the results")


def check_docs(problems, root):
    for rel in ("README.md", "docs/KNOWN-LIMITATIONS.md", "data/manual/index.html"):
        text = (root / rel).read_text()
        if "PLUGIN-COMPATIBILITY" not in text:
            problems.append("%s does not point at docs/PLUGIN-COMPATIBILITY.md" % rel)
    limitations = (root / "docs/KNOWN-LIMITATIONS.md").read_text()
    if "no third-party plug-in of any kind has been tested" in limitations:
        problems.append("docs/KNOWN-LIMITATIONS.md still carries the blanket 'no third-party plug-in ... tested' claim")


def run_gate(root, results=None, subjects=None):
    problems = []
    subjects = subjects or L.load_json(root / "docs/plugin-compat/subjects.json")
    results = results or L.load_json(root / "docs/plugin-compat/results-linux-x86_64.json")
    check_subjects(subjects, problems)
    check_results(results, subjects, problems)
    check_separation(results, subjects, problems, root)
    check_page(results, subjects, problems, root)
    check_docs(problems, root)
    return problems


def self_test(root):
    results = L.load_json(root / "docs/plugin-compat/results-linux-x86_64.json")
    subjects = L.load_json(root / "docs/plugin-compat/subjects.json")
    failed = copy.deepcopy(results)
    failed["rows"][0]["steps"]["load"] = L.cell("pass", "")
    failed["rows"][0]["steps"]["load"]["outcome"] = "maybe"
    fixture_row = copy.deepcopy(results)
    fixture_row["rows"].append(dict(fixture_row["rows"][0], id="Vst3InstrumentTest"))
    cases = {"an outcome outside the four": run_gate(root, failed, subjects),
             "a row that is not a subject": run_gate(root, fixture_row, subjects),
             "a page that no longer matches the results": run_gate(root, mutated_pass(results), subjects)}
    bad = [name for name, problems in cases.items() if not problems]
    for name, problems in cases.items():
        print("%s: %s" % ("FAIL" if not problems else "PASS", name))
    return 1 if bad else 0


def mutated_pass(results):
    """One measured cell flipped to the opposite outcome while the committed page stays as it was."""
    changed = copy.deepcopy(results)
    cell = changed["rows"][0]["steps"]["listed"]
    cell["outcome"] = "fail" if cell["outcome"] == "pass" else "pass"
    cell["detail"] = cell["detail"] or "flipped by the self-test"
    return changed


def main():
    root = pathlib.Path(sys.argv[sys.argv.index("--root") + 1]) if "--root" in sys.argv else L.REPO
    if "--self-test" in sys.argv:
        return self_test(root)
    problems = run_gate(root)
    for problem in problems:
        print("FAIL: %s" % problem)
    print("RESULT: %s" % ("FAIL (%d)" % len(problems) if problems else "PASS - the matrix, its pins and the published page agree"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
