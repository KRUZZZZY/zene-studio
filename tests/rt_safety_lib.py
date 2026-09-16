#!/usr/bin/env python3
"""rt_safety_lib.py - the rt-safety sweep's ledgers and verdict.

The judgement half of the real-time-safety whole-tree sweep (row 52, board card
#678). tests/rt_safety_source.py holds the rule set and resolves a declared
`path:symbol` pair to the lines it owns; this module holds the two ledgers that
say WHAT is measured and WHAT is tolerated, judges the measurement, and prints
the report every run carries.

WHAT THIS IS FOR
----------------
AGENTS.md carries one standing rule for the audio thread:

    Real-time rule: no allocation, no locking, no unbounded growth on
    audio-thread paths.

Until this programme existed the rule was held per feature: an allocation-counter
test (tests/src/core/AllocationProbe.h) proved one path at a time, on the paths
somebody wrote such a test for, and nothing measured the rule across the tree.
docs/CONVENTIONS.md row 9 said so in as many words: "partially enforced - a rule
held by tests where they exist, not by a sweeping gate".

THE TWO LEDGERS
---------------
  tests/rt-safety-scope.txt      the DECLARED audio-thread path set:
                                 `<path><TAB><symbol><TAB><reason>`. Blank reason
                                 refused (exit 2); a symbol that does not resolve
                                 is a failure (exit 1); a path that is not in the
                                 tree is a setup error (exit 2); an empty scope is
                                 an error, never a pass.
  tests/rt-safety-allowlist.txt  the tolerated hits:
                                 `<path><TAB><symbol><TAB><rule><TAB><count><TAB><reason>`.
                                 Blank reason refused; unknown rule id refused.

HOW A HIT IS JUDGED
-------------------
  key = <path>:<symbol>:<rule-id>   e.g. src/core/Mixer.cpp:Mixer::masterMix:lock-guard
  allowed  = the count on that key's allowlist line (0 if there is no line)
  measured = the number of hits found in the declared body

  measured > allowed  -> FAIL, "NEW rt-safety hit" (or growth past the line)
  measured < allowed  -> FAIL, "STALE allowlist line" (the debt was paid:
                         lower the count, or delete the line)
  key with no hits    -> FAIL, "STALE allowlist line" (it covers nothing now)

That is the same shape as tests/agent-surface-gate.py's ratchet, and for the
same reason: a ledger that can only be edited in one direction cannot be used to
bury the next one. The only valve is --reanchor "reason" (the CLI), which
refuses a blank reason and refuses to run while a real problem is outstanding.
"""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple

from rt_safety_source import (  # noqa: F401  (re-exported for the tests' imports)
    BOUND, CATEGORIES, RULES, RULES_BY_ID, Region, Rule, find_bodies,
    regions_for, scan_rules, strip_comments_and_literals,
)


@dataclass
class ScopeEntry:
    path: str
    symbol: str
    reason: str
    lineno: int


@dataclass
class AllowEntry:
    path: str
    symbol: str
    rule: str
    count: int
    reason: str
    lineno: int

    @property
    def key(self) -> str:
        return "%s:%s:%s" % (self.path, self.symbol, self.rule)


@dataclass
class Hit:
    path: str
    symbol: str
    rule: str
    category: str
    line: int
    text: str

    @property
    def key(self) -> str:
        return "%s:%s:%s" % (self.path, self.symbol, self.rule)


@dataclass
class Verdict:
    exit_code: int
    problems: List[str] = field(default_factory=list)
    hits: List[Hit] = field(default_factory=list)
    counts: Dict[str, int] = field(default_factory=dict)
    stats: Dict[str, Any] = field(default_factory=dict)


# ---------------------------------------------------------------------------
# the two ledgers
# ---------------------------------------------------------------------------

def _data_lines(text: str) -> Iterable[Tuple[int, str]]:
    for lineno, raw in enumerate(text.split("\n"), start=1):
        if raw.strip() == "" or raw.lstrip().startswith("#"):
            continue
        yield lineno, raw


def parse_scope(text: str) -> Tuple[List[ScopeEntry], List[str]]:
    """`<path><TAB><symbol><TAB><reason>` per line. A blank reason is refused."""
    entries: List[ScopeEntry] = []
    errors: List[str] = []
    seen = set()
    for lineno, raw in _data_lines(text):
        fields = raw.rstrip("\n").split("\t")
        if len(fields) < 3 or not fields[0].strip() or not fields[1].strip():
            errors.append("rt-safety-scope.txt:%d: expected <path><TAB><symbol><TAB><reason>, "
                          "got %r" % (lineno, raw))
            continue
        path, symbol = fields[0].strip(), fields[1].strip()
        reason = "\t".join(fields[2:]).strip()
        if not reason:
            errors.append("rt-safety-scope.txt:%d: %s:%s has no reason - a declared "
                          "audio-thread path states WHY it is one" % (lineno, path, symbol))
            continue
        if (path, symbol) in seen:
            errors.append("rt-safety-scope.txt:%d: duplicate entry %s:%s" % (lineno, path, symbol))
            continue
        seen.add((path, symbol))
        entries.append(ScopeEntry(path, symbol, reason, lineno))
    return entries, errors


def parse_allowlist(text: str) -> Tuple[List[AllowEntry], List[str]]:
    """`<path><TAB><symbol><TAB><rule><TAB><count><TAB><reason>` per line."""
    entries: List[AllowEntry] = []
    errors: List[str] = []
    for lineno, raw in _data_lines(text):
        fields = raw.rstrip("\n").split("\t")
        if len(fields) < 5:
            errors.append("rt-safety-allowlist.txt:%d: expected <path><TAB><symbol><TAB><rule>"
                          "<TAB><count><TAB><reason>, got %r" % (lineno, raw))
            continue
        path, symbol, rule = fields[0].strip(), fields[1].strip(), fields[2].strip()
        reason = "\t".join(fields[4:]).strip()
        if rule not in RULES_BY_ID:
            errors.append("rt-safety-allowlist.txt:%d: %r is not a rule id (known: %s)"
                          % (lineno, rule, ", ".join(sorted(RULES_BY_ID))))
            continue
        entry = _allow_entry(fields, path, symbol, rule, reason, lineno, errors)
        if entry is not None:
            entries.append(entry)
    return entries, errors


def _allow_entry(fields: Sequence[str], path: str, symbol: str, rule: str, reason: str,
                 lineno: int, errors: List[str]) -> Optional[AllowEntry]:
    """The count is the one field that has to be a positive integer."""
    try:
        count = int(fields[3].strip())
    except ValueError:
        errors.append("rt-safety-allowlist.txt:%d: count %r is not an integer"
                      % (lineno, fields[3]))
        return None
    if count < 1:
        errors.append("rt-safety-allowlist.txt:%d: count must be >= 1 (a line that allows "
                      "nothing is a line to delete)" % lineno)
        return None
    if not reason:
        errors.append("rt-safety-allowlist.txt:%d: %s:%s:%s has no reason - an allowlist "
                      "entry without one is a silenced hit" % (lineno, path, symbol, rule))
        return None
    return AllowEntry(path, symbol, rule, count, reason, lineno)


# ---------------------------------------------------------------------------
# the sweep
# ---------------------------------------------------------------------------

def _collect(tree: str, scope: Sequence[ScopeEntry]) -> Tuple[List[Hit], int, Dict[str, int],
                                                              Optional[Tuple[int, str]]]:
    """Every hit inside every declared region, with the lines it was found in."""
    hits: List[Hit] = []
    total_lines = 0
    modes: Dict[str, int] = {"qualified": 0, "in-class": 0, "whole-file": 0}
    for entry in scope:
        regions, error = regions_for(tree, entry)
        if error is not None:
            kind = 2 if error.startswith("no such file") else 1
            return hits, total_lines, modes, (kind, "%s:%s %s" % (entry.path, entry.symbol, error))
        with open(os.path.join(tree, entry.path), "r", encoding="utf-8",
                  errors="replace") as handle:
            stripped = strip_comments_and_literals(handle.read())
        for region in regions:
            body = stripped.split("\n")[region.start - 1:region.end]
            total_lines += len(body)
            modes[region.mode] = modes.get(region.mode, 0) + 1
            for lineno, rule, text in scan_rules("\n".join(body)):
                hits.append(Hit(entry.path, entry.symbol, rule.id, rule.category,
                                region.start + lineno - 1, text))
    return hits, total_lines, modes, None


def _judge(counts: Dict[str, int], allow: Sequence[AllowEntry],
           scope: Sequence[ScopeEntry], hits: Sequence[Hit]) -> List[str]:
    """The ratchet: new hits and growth fail, stale lines fail, undeclared fail."""
    problems: List[str] = []
    allowed_by_key = {entry.key: entry for entry in allow}
    for key in sorted(counts):
        problems.extend(_judge_key(key, counts[key], allowed_by_key.get(key), hits))
    for key in sorted(allowed_by_key):
        problems.extend(_judge_allow_line(key, counts.get(key, 0), allowed_by_key[key]))
    declared = {(entry.path, entry.symbol) for entry in scope}
    for entry in allow:
        if (entry.path, entry.symbol) not in declared:
            problems.append("allowlist line %d names %s:%s, which the scope file does not "
                            "declare" % (entry.lineno, entry.path, entry.symbol))
    return problems


def _judge_key(key: str, measured: int, entry: Optional[AllowEntry],
               hits: Sequence[Hit]) -> List[str]:
    if entry is None:
        sample = next(hit for hit in hits if hit.key == key)
        return ["NEW rt-safety hit: %s [%s] - %d occurrence(s), first at %s:%d: %s"
                % (key, sample.category, measured, sample.path, sample.line, sample.text[:120])]
    if measured > entry.count:
        return ["rt-safety hit GREW: %s - allowed %d, measured %d (the allowlist count is a "
                "ratchet; raise it only with --reanchor \"reason\" and only for a hit this tree "
                "accepts)" % (key, entry.count, measured)]
    return []


def _judge_allow_line(key: str, measured: int, entry: AllowEntry) -> List[str]:
    if measured == 0:
        return ["STALE allowlist line: %s covers nothing now (the hit is gone: delete the line)"
                % key]
    if measured < entry.count:
        return ["STALE allowlist line: %s allowed %d but the tree measures %d (lower the count "
                "to %d, or delete the line)" % (key, entry.count, measured, measured)]
    return []


def _preflight(scope_text: str, allowlist_text: str) -> Tuple[List[ScopeEntry], List[AllowEntry],
                                                             Optional[Verdict]]:
    """Parse both ledgers; a refusal here is a setup error, not a judgement."""
    scope, scope_errors = parse_scope(scope_text)
    allow, allow_errors = parse_allowlist(allowlist_text)
    errors = scope_errors + allow_errors
    if errors:
        return scope, allow, Verdict(2, problems=errors,
                                     stats={"reason": "the ledgers do not parse"})
    if not scope:
        return scope, allow, Verdict(
            2, problems=["the scope file declares no audio-thread path. 0 entries is an ERROR, "
                         "never a pass: a sweep with nothing to measure reports what it did not "
                         "look at."], stats={"reason": "empty scope"})
    return scope, allow, None


def _counts(hits: Sequence[Hit]) -> Dict[str, int]:
    counts: Dict[str, int] = {}
    for hit in hits:
        counts[hit.key] = counts.get(hit.key, 0) + 1
    return counts


def _stats(scope: Sequence[ScopeEntry], allow: Sequence[AllowEntry], counts: Dict[str, int],
           total_lines: int, modes: Dict[str, int], hits: Sequence[Hit]) -> Dict[str, Any]:
    by_category: Dict[str, int] = {category: 0 for category in CATEGORIES}
    for hit in hits:
        by_category[hit.category] = by_category.get(hit.category, 0) + 1
    allowed_keys = {entry.key for entry in allow}
    fresh = 0
    for key, measured in counts.items():
        if key not in allowed_keys:
            fresh += 1
        elif measured > _allowed_count(allow, key):
            fresh += 1
    return {
        "scope_entries": len(scope),
        "allowlist_entries": len(allow),
        "regions_scanned": total_lines,
        "resolutions": modes,
        "hits": len(hits),
        "hits_by_category": by_category,
        "allowlisted": sum(1 for key in counts if key in allowed_keys),
        "fresh": fresh,
    }


def run_sweep(tree: str, scope_text: str, allowlist_text: str) -> Verdict:
    """Measure `tree` against the scope, judge it against the allowlist."""
    scope, allow, refusal = _preflight(scope_text, allowlist_text)
    if refusal is not None:
        return refusal
    hits, total_lines, modes, failure = _collect(tree, scope)
    if failure is not None:
        kind, message = failure
        return Verdict(kind, problems=[message],
                       stats={"reason": "the declared scope does not resolve"})
    counts = _counts(hits)
    problems = _judge(counts, allow, scope, hits)
    return Verdict(1 if problems else 0, problems=problems, hits=hits, counts=counts,
                   stats=_stats(scope, allow, counts, total_lines, modes, hits))


def _allowed_count(allow: Sequence[AllowEntry], key: str) -> int:
    return next((entry.count for entry in allow if entry.key == key), 0)


# ---------------------------------------------------------------------------
# the report
# ---------------------------------------------------------------------------

def format_report(tree: str, verdict: Verdict, scope_path: str, allowlist_path: str) -> str:
    lines: List[str] = ["rt-safety whole-tree sweep",
                        "  tree      : %s" % tree,
                        "  scope     : %s" % scope_path,
                        "  allowlist : %s" % allowlist_path]
    stats = verdict.stats
    if stats.get("reason"):
        lines.append("  verdict   : no verdict reached (%s)" % stats["reason"])
        lines.extend("  FAIL: %s" % problem for problem in verdict.problems)
        return "\n".join(lines)
    lines.extend(_report_measurements(verdict))
    if verdict.problems:
        lines.append("")
        lines.append("---- %d problem(s) ----" % len(verdict.problems))
        lines.extend("FAIL: %s" % problem for problem in verdict.problems)
    lines.append("")
    lines.append("BOUND of this programme (what it does NOT cover):")
    lines.extend("  * %s" % bound for bound in BOUND)
    if verdict.exit_code == 0:
        lines.append("")
        lines.append("PASS: every hit on a declared audio-thread path is allowlisted with a "
                     "reason and at its allowed count.")
    return "\n".join(lines)


def _report_measurements(verdict: Verdict) -> List[str]:
    stats = verdict.stats
    resolutions = stats.get("resolutions")
    if not isinstance(resolutions, dict):
        resolutions = {}
    by_category = stats.get("hits_by_category") or {}
    return [
        "  declared  : %d path:symbol pair(s)" % stats["scope_entries"],
        "  resolved  : %d in-class form, %d whole-file"
        % (resolutions.get("in-class", 0), resolutions.get("whole-file", 0)),
        "  measured  : %d region line(s); %d hit(s) in %d key(s)"
        % (stats["regions_scanned"], stats["hits"], len(verdict.counts)),
        "  by rule   : %s" % ", ".join("%s %d" % (category, by_category.get(category, 0))
                                       for category in CATEGORIES),
        "  allowlist : %d line(s); %d key(s) allowlisted, %d key(s) fresh"
        % (stats["allowlist_entries"], stats["allowlisted"], stats["fresh"]),
    ]


ALLOWLIST_HEADER: Tuple[str, ...] = (
    "# tests/rt-safety-allowlist.txt - the rt-safety sweep's ledger of ACCEPTED hits.",
    "#",
    "# Read by tests/rt-safety-sweep.py (ctest RtSafetySweep) and by",
    "# tests/rt_safety_selftest.py's controls. Format, TAB separated:",
    "#",
    "#   <path><TAB><symbol><TAB><rule-id><TAB><count><TAB><reason>",
    "#",
    "# * A line without a reason is refused (exit 2), and so is a rule id that is not in",
    "#   tests/rt_safety_source.py's RULES.",
    "# * <count> is a RATCHET: the sweep fails when the tree measures MORE than this",
    "#   (a new hit, or growth past the line) and when it measures FEWER (the debt was",
    "#   paid: lower the count or delete the line). It may only fall.",
    "# * The only way a count rises is --reanchor \"reason\", which refuses a blank reason",
    "#   and refuses to run while a real problem is outstanding.",
    "#",
    "# The reasons below name the CLASS of each accepted hit: inherited upstream code that",
    "# this line of the product has not replaced yet ('inherited'), a fork path whose hit",
    "# is bounded by a declared capacity, or a mention that cannot run on the audio thread.",
    "# Every entry is a debt, not an endorsement: the count is the size of what is left.",
    "",
)


def render_allowlist(entries: Sequence[AllowEntry], counts: Dict[str, int],
                     scope: Sequence[ScopeEntry]) -> str:
    """Rebuild the allowlist at the measured counts, keeping every reason."""
    body: List[str] = []
    by_symbol: Dict[Tuple[str, str], List[AllowEntry]] = {}
    for entry in entries:
        by_symbol.setdefault((entry.path, entry.symbol), []).append(entry)
    for (path, symbol), group in by_symbol.items():
        body.append(_symbol_comment(path, symbol, scope))
        for entry in sorted(group, key=lambda item: item.rule):
            measured = counts.get(entry.key, 0)
            if measured:
                body.append("%s\t%s\t%s\t%d\t%s"
                            % (path, symbol, entry.rule, measured, entry.reason))
    return "\n".join(list(ALLOWLIST_HEADER) + body) + "\n"


def _symbol_comment(path: str, symbol: str, scope: Sequence[ScopeEntry]) -> str:
    reason = next((entry.reason for entry in scope if entry.path == path and entry.symbol == symbol),
                  None)
    return "# %s:%s%s" % (path, symbol, " - %s" % reason if reason else "")
