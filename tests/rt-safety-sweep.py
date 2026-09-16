#!/usr/bin/env python3
"""rt-safety-sweep.py - the rt-safety whole-tree sweep (AGENTS.md realtime rule).

Row 52 of docs/FEATURE-LIST-0.3.0.md / board card #678. This is the SWEEPING
check, not another per-feature probe: it walks every audio-thread path the tree
declares (tests/rt-safety-scope.txt), scans the bodies of the functions named
there for allocation, locking and container growth, and judges what it finds
against an explicit allowlist with reasons (tests/rt-safety-allowlist.txt) whose
counts are a ratchet that may only fall.

The rule it enforces (AGENTS.md, "Realtime rule"):
    no allocation, no locking, no unbounded growth on audio-thread paths.

WHAT IT PROVES, AND WHAT IT DOES NOT
------------------------------------
It proves that on THIS tree, on the paths the scope file declares, every mention
of an allocating, locking or growing construct is either absent or allowlisted
with a reason and at its allowed count. It does NOT prove the audio thread is
real-time safe: see the BOUND block printed at the end of every run (and
tests/rt_safety_source.py, which holds that text). The runtime half of the rule
stays with the allocation-counter tests (tests/src/core/AllocationProbe.h).

THE POSITIVE CONTROL - the sweep is not taken on trust
------------------------------------------------------
tests/rt_safety_selftest.py (ctest RtSafetySelfTest) plants a deliberate
allocation, a deliberate lock and a deliberate container growth on fixture
audio-thread paths and requires THIS code to fail on each of them, name the
offending path:symbol:rule, pass on a clean fixture, and stay silent about a
violation that sits outside the declared scope. A sweep that cannot fail proves
nothing. The programme's own landing record (docs/RT-SAFETY-SWEEP.md) carries
the same control run against the REAL tree.

USAGE
-----
    python3 tests/rt-safety-sweep.py                        # the gate, repo root
    python3 tests/rt-safety-sweep.py --check --json out.json
    python3 tests/rt-safety-sweep.py --reanchor "reason"    # lower counts / drop stale lines
    python3 tests/rt-safety-sweep.py --list-scope

Exit codes: 0 = pass (every hit allowlisted at its allowed count), 1 = violation
(a new hit, growth past a line, a stale line, or a declared symbol that no longer
resolves), 2 = setup error (a ledger that does not parse, an empty scope, a
declared path that is not in the tree) - no verdict was reached.
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from rt_safety_lib import (  # noqa: E402  (path set above)
    RULES, format_report, parse_allowlist, parse_scope, render_allowlist, run_sweep,
)

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_TREE = os.path.dirname(HERE)
DEFAULT_SCOPE = os.path.join(HERE, "rt-safety-scope.txt")
DEFAULT_ALLOWLIST = os.path.join(HERE, "rt-safety-allowlist.txt")


def log(message):
    print(message, flush=True)


def read(path):
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def build_parser():
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--tree", default=DEFAULT_TREE,
                        help="repository root to measure (default: the parent of tests/)")
    parser.add_argument("--scope", default=DEFAULT_SCOPE)
    parser.add_argument("--allowlist", default=DEFAULT_ALLOWLIST)
    parser.add_argument("--check", action="store_true",
                        help="report only (the default: nothing is written without --reanchor)")
    parser.add_argument("--reanchor", metavar="REASON", default=None,
                        help="rebuild the allowlist at the measured counts; a blank reason is "
                             "refused and every dropped line is printed")
    parser.add_argument("--json", dest="json_path", default=None,
                        help="write the run's measurements to this path as JSON evidence")
    parser.add_argument("--list-scope", action="store_true")
    parser.add_argument("--list-rules", action="store_true")
    return parser


def list_rules():
    for rule in RULES:
        log("%-14s %-11s %s" % (rule.id, rule.category, rule.note))
    return 0


def list_scope(tree, scope_path):
    scope, errors = parse_scope(read(scope_path))
    for error in errors:
        log("FAIL: %s" % error)
    for entry in scope:
        exists = "ok" if os.path.isfile(os.path.join(tree, entry.path)) else "MISSING FILE"
        log("%-46s %-42s %s" % (entry.path, entry.symbol, exists))
    log("%d declared path:symbol pair(s)" % len(scope))
    return 2 if errors else 0


def validate(options):
    """Setup errors, where there is no verdict to give: 0 means carry on."""
    if not os.path.isdir(options.tree):
        log("FAIL: no tree at %s" % options.tree)
        return 2
    if not os.path.isfile(options.scope):
        log("FAIL: no scope file at %s" % options.scope)
        return 2
    if options.reanchor is not None and not options.reanchor.strip():
        log('usage: --reanchor "reason" - an unrecorded re-anchor is not allowed')
        return 2
    if not os.path.isfile(options.allowlist) and options.reanchor is not None:
        log("FAIL: no allowlist at %s to re-anchor" % options.allowlist)
        return 2
    return 0


def reanchor(options):
    if _allowlist_parses(options.allowlist) is None:
        return 2
    verdict = run_sweep(options.tree, read(options.scope), read(options.allowlist))
    if verdict.stats.get("reason"):
        log("FAIL: no verdict to re-anchor: %s" % verdict.stats["reason"])
        for problem in verdict.problems:
            log("FAIL: %s" % problem)
        return verdict.exit_code
    fresh = [problem for problem in verdict.problems if not problem.startswith("STALE")]
    if fresh:
        return refuse_reanchor(fresh)
    allow_entries, _ = parse_allowlist(read(options.allowlist))
    dropped = [entry for entry in allow_entries if verdict.counts.get(entry.key, 0) == 0]
    scope, _ = parse_scope(read(options.scope))
    with open(options.allowlist, "w", encoding="utf-8") as handle:
        handle.write(render_allowlist(allow_entries, verdict.counts, scope))
    log("RE-ANCHORED: %s rewritten at the measured counts" % options.allowlist)
    log("reason: %s" % options.reanchor.strip())
    for entry in dropped:
        log("  dropped: %s (no hit left in the tree)" % entry.key)
    return 0


def _allowlist_parses(path):
    """The entries, or None after printing why the ledger was refused."""
    entries, errors = parse_allowlist(read(path))
    for error in errors:
        log("FAIL: %s" % error)
    return entries if not errors else None


def refuse_reanchor(fresh):
    log("--reanchor refused: the run still has %d real problem(s); the valve records a "
        "tolerated state, it does not bury a new hit:" % len(fresh))
    for problem in fresh:
        log("FAIL: %s" % problem)
    return 1


def check(options):
    verdict = run_sweep(options.tree, read(options.scope), read(options.allowlist))
    log(format_report(options.tree, verdict, os.path.relpath(options.scope, options.tree),
                      os.path.relpath(options.allowlist, options.tree)))
    if options.json_path:
        write_json(options, verdict)
    return verdict.exit_code


def write_json(options, verdict):
    payload = {
        "tree": options.tree,
        "scope": os.path.relpath(options.scope, options.tree),
        "allowlist": os.path.relpath(options.allowlist, options.tree),
        "exit_code": verdict.exit_code,
        "stats": verdict.stats,
        "counts": verdict.counts,
        "problems": verdict.problems,
        "rules": {rule.id: rule.category for rule in RULES},
        "hits": [{"path": hit.path, "symbol": hit.symbol, "rule": hit.rule,
                  "category": hit.category, "line": hit.line} for hit in verdict.hits],
    }
    with open(options.json_path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")
    log("wrote %s" % options.json_path)


def main(argv=None):
    options = build_parser().parse_args(argv if argv is not None else sys.argv[1:])
    if options.list_rules:
        return list_rules()
    problem = validate(options)
    if problem:
        return problem
    if options.list_scope:
        return list_scope(options.tree, options.scope)
    if options.reanchor is not None:
        return reanchor(options)
    return check(options)


if __name__ == "__main__":
    sys.exit(main())
