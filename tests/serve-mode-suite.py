#!/usr/bin/env python3
"""Run the control-socket suite against serve mode (`zene --engine`, Q050-517).

The suite is not copied: every registered ctest that drives the real binary through
tests/control_socket_harness.py is run again with ZENE_HARNESS_ENGINE_SERVE=1, which makes the
harness add `--engine` to the launch line. Same scripts, same assertions, no interface.

A test that exercises the Qt interface itself cannot pass without one. Those are named in
tests/serve-mode-exclusions.txt, one per line, `<ctest name><TAB><reason>`. The list is held honest
from both sides:
  - a name that is not a harness test in this build is an error (a stale or misspelt entry);
  - a blank reason is an error;
  - `--audit` runs ONLY the excluded tests and fails if one of them passes, because an exclusion
    that is no longer needed hides a test that should be running.

Usage:
    python3 tests/serve-mode-suite.py <build-dir> [-j N] [--audit] [--list]
Exit codes: 0 the run held; 1 a test failed (or, with --audit, an excluded test passed);
2 cannot run (no build, no tests selected, a malformed exclusion list).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXCLUSIONS = os.path.join(HERE, "serve-mode-exclusions.txt")
HARNESS = "control_socket_harness"
IMPORT = re.compile(r"^\s*(?:from\s+([A-Za-z_][A-Za-z0-9_]*)\s+import|import\s+([A-Za-z_][A-Za-z0-9_]*))", re.M)


def local_imports(path):
    """The modules `path` imports that are files in tests/ (the only ones that can reach the harness)."""
    try:
        text = open(path, errors="replace").read()
    except OSError:
        return []
    names = {a or b for a, b in IMPORT.findall(text)}
    return [name for name in names if os.path.exists(os.path.join(HERE, name + ".py"))]


def uses_harness(script, seen=None):
    """True when `script` reaches control_socket_harness through its imports."""
    seen = set() if seen is None else seen
    for name in local_imports(script):
        if name == HARNESS:
            return True
        if name not in seen:
            seen.add(name)
            if uses_harness(os.path.join(HERE, name + ".py"), seen):
                return True
    return False


def test_script(test):
    """The tests/*.py a ctest entry runs, or None."""
    for argument in test.get("command") or []:
        if argument.endswith(".py") and os.path.dirname(os.path.abspath(argument)) == HERE:
            return argument
    return None


def harness_tests(tests_dir):
    """Names of the registered ctests that drive the binary through the harness, sorted."""
    listing = subprocess.run(["ctest", "--show-only=json-v1"], cwd=tests_dir, capture_output=True,
                             text=True, check=False)
    if listing.returncode != 0:
        raise RuntimeError("ctest --show-only failed in %s: %s" % (tests_dir, listing.stderr[-400:]))
    names = []
    for test in json.loads(listing.stdout).get("tests") or []:
        script = test_script(test)
        if script and uses_harness(script):
            names.append(test["name"])
    return sorted(names)


def read_exclusions(path):
    """{name: reason}. Raises ValueError on a line without a reason."""
    excluded = {}
    for number, line in enumerate(open(path, errors="replace").read().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        name, _, reason = line.partition("\t")
        if not reason.strip():
            raise ValueError("%s:%d: '%s' has no reason; an exclusion without one is not honoured"
                             % (path, number, name.strip()))
        excluded[name.strip()] = reason.strip()
    return excluded


def run_ctest(tests_dir, names, jobs, timeout):
    env = dict(os.environ)
    env["ZENE_HARNESS_ENGINE_SERVE"] = "1"
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    pattern = "^(%s)$" % "|".join(re.escape(name) for name in names)
    command = ["ctest", "-R", pattern, "-j", str(jobs), "--timeout", str(timeout), "--output-on-failure"]
    return subprocess.run(command, cwd=tests_dir, env=env, check=False).returncode


def failed_tests(tests_dir):
    """Names ctest listed as failed in its last run, read from its own log (None when there is none)."""
    failed_log = os.path.join(tests_dir, "Testing", "Temporary", "LastTestsFailed.log")
    try:
        lines = open(failed_log, errors="replace").read().splitlines()
    except OSError:
        return None
    return {line.split(":", 1)[1] for line in lines if ":" in line}


def audit(tests_dir, excluded, jobs, timeout):
    """Run only the excluded tests; one that passes is a stale exclusion."""
    failed_log = os.path.join(tests_dir, "Testing", "Temporary", "LastTestsFailed.log")
    if os.path.exists(failed_log):
        os.remove(failed_log)
    code = run_ctest(tests_dir, sorted(excluded), jobs, timeout)
    failed = set() if code == 0 else failed_tests(tests_dir)
    if failed is None:
        print("serve-mode-suite: ctest failed and left no failure list; cannot audit", file=sys.stderr)
        return 2
    stale = sorted(set(excluded) - failed)
    for name in stale:
        print("STALE EXCLUSION: %s passes in serve mode (listed reason: %s)" % (name, excluded[name]))
    print("serve-mode-suite --audit: %d excluded, %d still fail as declared, %d stale"
          % (len(excluded), len(excluded) - len(stale), len(stale)))
    return 1 if stale else 0


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("build_dir")
    parser.add_argument("-j", "--jobs", type=int, default=4)
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--audit", action="store_true")
    parser.add_argument("--list", action="store_true")
    return parser.parse_args(argv)


def select(tests_dir):
    """(selected names, {excluded name: reason}), with the exclusion list validated."""
    names = harness_tests(tests_dir)
    if not names:
        raise RuntimeError("no harness test is registered in %s: 0 tests is an error, never a pass" % tests_dir)
    excluded = read_exclusions(EXCLUSIONS)
    unknown = sorted(set(excluded) - set(names))
    if unknown:
        raise ValueError("%s names tests that are not harness tests in this build: %s"
                         % (EXCLUSIONS, ", ".join(unknown)))
    return names, excluded


def main(argv):
    options = parse_arguments(argv)
    tests_dir = os.path.join(os.path.abspath(options.build_dir), "tests")
    try:
        names, excluded = select(tests_dir)
    except (RuntimeError, ValueError, OSError) as error:
        print("serve-mode-suite: %s" % error, file=sys.stderr)
        return 2
    running = [name for name in names if name not in excluded]
    print("serve-mode-suite: %d harness tests, %d excluded (interface), %d to run with --engine"
          % (len(names), len(excluded), len(running)))
    if options.list:
        print("\n".join(running))
        return 0
    if options.audit:
        return audit(tests_dir, excluded, options.jobs, options.timeout)
    return 1 if run_ctest(tests_dir, running, options.jobs, options.timeout) != 0 else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
