#!/usr/bin/env python3
"""agent_surface_lib.py - the decision core of the agent_surface gate (SPEC A15).

Kept separate from tests/agent-surface-gate.py so that both files stay inside
the fork-scoped gates that measure them (file-length <= 500 lines, CCN <= 10):
this module holds the file formats, the pure decision logic and the arguments
synthesised from a command's own schema; the gate holds the socket client, the
documented headless launch recipe and the report.

Nothing here starts a process or opens a socket, which is what makes the whole
verdict testable in-process - see self_test(), run by
`tests/agent-surface-gate.py --self-test`.
"""

import json
import os
import time

#: error.kind values the wire contract allows (AGENT-TOOLING.md section 4).
TERMINAL_KINDS = {"not_found", "requires", "invalid_args", "busy", "refused"}


# --------------------------------------------------------------------------
# file formats
# --------------------------------------------------------------------------

def load_reasons(path):
    """`<key><TAB><reason>` lines; `#` comments and blanks skipped.

    Returns {key: reason}. A line with no reason raises: an unjustified
    exemption is how a gate stops being a gate.
    """
    entries = {}
    if not path or not os.path.exists(path):
        return entries
    with open(path, encoding="utf-8") as handle:
        for number, raw in enumerate(handle, 1):
            line = raw.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            key, tab, reason = line.partition("\t")
            if not tab or not reason.strip():
                raise ValueError("%s:%d: no reason given for '%s'" % (path, number, key))
            entries[key.strip()] = reason.strip()
    return entries


def load_baseline(path):
    """One action key per line; `#` comments and blanks skipped. Order is irrelevant."""
    keys = []
    if not path or not __import__("os").path.exists(path):
        return keys
    with open(path, encoding="utf-8") as handle:
        for raw in handle:
            line = raw.strip()
            if line and not line.startswith("#"):
                keys.append(line)
    return keys


def write_baseline(path, keys, reason):
    """The deliberate re-anchor. Never called by a plain gate run."""
    import time
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("# agent-surface baseline - menu/toolbar actions with no registered\n")
        handle.write("# command id (SPEC-zene-studio.md A15). DO NOT ADD LINES BY HAND:\n")
        handle.write("# the ratchet only moves one way. A line is deleted when its action\n")
        handle.write("# gets a command id; the whole file is rewritten only by a deliberate\n")
        handle.write('#   tests/agent-surface-gate.py ... --reanchor "reason"\n')
        handle.write("#\n# re-anchored %s: %s\n" % (time.strftime("%Y-%m-%d"), reason))
        for key in sorted(keys):
            handle.write("%s\n" % key)


# --------------------------------------------------------------------------
# the shape of one reflected surface item
# --------------------------------------------------------------------------

def action_key(item):
    """A stable, human-readable identity for one menu/toolbar item.

    Built from the surface, the container and the label - never from an ordinal
    - so it survives a rebuild and reads like the menu item it grandfathers,
    e.g. `menu:File/Open...` or `toolbar:mainToolbar/Metronome`.
    """
    return "%s:%s/%s" % (item["surface"], item["container_path"], item["text"])


def is_exempt(item, exempt):
    """A matcher from tests/agent-surface-exempt.txt, or None.

    Exemptions are narrow by construction: `class:<Name>` exempts one QMenu
    subclass, `path:<Menu>` one menu. An unregistered action anywhere else still
    fails the gate, and an exempt item is still counted and reported.
    """
    for matcher in exempt:
        kind, _, value = matcher.partition(":")
        if kind == "class":
            if item["container_class"].rsplit("::", 1)[-1] == value:
                return matcher
        elif kind == "path":
            if item["container_path"] == value or item["container_path"].startswith(value + "/"):
                return matcher
        else:
            raise ValueError("exempt matcher %r: use class:<Name> or path:<Menu>" % matcher)
    return None


def classify_action(item, key, baseline):
    """(problem-or-None, tier) for one non-exempt surface item.

    tier is one of REGISTERED / UNKNOWN / GRANDFATHERED / NEW.
    """
    if item.get("declared_unknown"):
        return ("HARD: '%s' declares command '%s', which is not in the registry - a "
                "declaration that resolves to nothing cannot be grandfathered"
                % (key, item.get("command")), "UNKNOWN")
    if item.get("command"):
        return (None, "REGISTERED")
    if key in baseline:
        return (None, "GRANDFATHERED")
    return ("NEW action without a registered command: '%s' (surface=%s container=%s)"
            % (key, item["surface"], item["container_path"]), "NEW")


def check_reflection(surface, baseline, exempt):
    """Every action in every (non-exempt) menu/toolbar must resolve to a command."""
    problems, stats = [], {"reflected": 0, "exempt": 0, "registered": 0,
                           "unregistered": 0, "grandfathered": 0}
    unregistered, seen = set(), set()
    for item in surface:
        stats["reflected"] += 1
        if is_exempt(item, exempt) is not None:
            stats["exempt"] += 1
            continue
        key = action_key(item)
        if key in seen:
            problems.append("two different surface items share the key '%s'; the baseline "
                            "cannot tell them apart" % key)
            continue
        seen.add(key)
        problem, tier = classify_action(item, key, baseline)
        if problem:
            problems.append(problem)
        if tier == "REGISTERED":
            stats["registered"] += 1
            continue
        stats["unregistered"] += 1
        unregistered.add(key)
        if tier == "GRANDFATHERED":
            stats["grandfathered"] += 1
    return problems, stats, unregistered, seen


def check_ratchet(baseline, unregistered, seen):
    """The baseline may only shrink: an entry that is no longer needed is stale."""
    problems = []
    for key in sorted(set(baseline) - unregistered):
        if key in seen:
            problems.append("STALE baseline entry: '%s' resolves to a command now - delete "
                            'that line from the baseline, or re-anchor with --reanchor "reason"'
                            % key)
        else:
            problems.append("STALE baseline entry: '%s' is no longer in the surface at all "
                            '(the action was renamed or removed) - delete that line from the '
                            'baseline, or re-anchor with --reanchor "reason"' % key)
    return problems, len(set(baseline) - unregistered)


def check_allowlist(commands, allowlist):
    """An allowlist entry must name a live command AND be justified by `requires`."""
    problems, allowed = [], 0
    for command, reason in sorted(allowlist.items()):
        if command not in commands:
            problems.append("allowlist names '%s', which the registry does not declare "
                            "(stale entry: %s)" % (command, reason))
            continue
        if not commands[command].get("requires"):
            problems.append("allowlist entry '%s' is not justified: the command declares no "
                            "requires (display|device|human), so it has to be swept "
                            "(reason given: %s)" % (command, reason))
            continue
        allowed += 1
    return problems, allowed


def sweep_problem(command, outcome, timeout):
    """None when the sweep outcome is a typed result, else the reason it is not."""
    if outcome is None:
        return ("command '%s' is neither allowlisted nor swept - the reverse completeness "
                "check cannot account for it" % command)
    status = outcome.get("status")
    if status in ("ok", "typed_error"):
        return None
    if status == "timeout":
        return "HANG: '%s' produced no reply within %.0f s" % (command, timeout)
    if status in ("dead", "malformed"):
        return "CRASH/PROTOCOL: '%s' -> %s (%s)" % (command, status, outcome.get("detail", ""))
    return "command '%s' failed the sweep: %r" % (command, outcome)


def check_sweep(commands, allowlist, sweep, timeout):
    """Reverse completeness: every declared command is swept or allowlisted."""
    problems, swept = [], 0
    for command in sorted(commands):
        if command in allowlist:
            continue
        problem = sweep_problem(command, sweep.get(command), timeout)
        if problem:
            problems.append(problem)
        else:
            swept += 1
    return problems, swept


def evaluate(surface, commands, baseline, allowlist, exempt, sweep, timeout):
    """Return (problems, stats). The single place a verdict is decided."""
    reflect_problems, stats, unregistered, seen = check_reflection(surface, baseline, exempt)
    ratchet_problems, stale = check_ratchet(baseline, unregistered, seen)
    allow_problems, allowlisted = check_allowlist(commands, allowlist)
    sweep_problems, swept = check_sweep(commands, allowlist, sweep, timeout)
    stats.update({"stale": stale, "allowlisted": allowlisted, "swept": swept,
                  "commands": len(commands), "baseline": len(baseline)})
    return reflect_problems + ratchet_problems + allow_problems + sweep_problems, stats


# --------------------------------------------------------------------------
# arguments synthesised from the command's own schema
# --------------------------------------------------------------------------

def value_for_spec(spec):
    if isinstance(spec.get("enum"), list) and spec["enum"]:
        return spec["enum"][0]
    kind = spec.get("type")
    if kind == "integer":
        return int(spec.get("minimum", 0) or 0)
    if kind == "number":
        return float(spec.get("minimum", 0) or 0)
    if kind == "boolean":
        return False
    if kind == "array":
        return []
    if kind == "object":
        return {}
    return ""


def derive_args(schema, command, overrides):
    """The smallest argument object the command's own schema accepts.

    Driving this from the live schema means a command added tomorrow is swept
    without anyone editing the gate. `overrides` supplies real values where an
    empty placeholder would hang on a modal or test nothing.
    """
    if command in overrides:
        return overrides[command]
    properties = schema.get("properties", {})
    return {name: value_for_spec(properties.get(name, {}))
            for name in schema.get("required", [])}


def typed(reply, expected_id):
    """None when the reply is a well-formed typed result, else why it is not."""
    if not isinstance(reply, dict):
        return "reply is not a JSON object"
    if reply.get("id") != expected_id:
        return "reply id %r != %r" % (reply.get("id"), expected_id)
    if reply.get("ok") is True:
        return None if isinstance(reply.get("result"), dict) else "ok=true without a result object"
    if reply.get("ok") is False:
        error = reply.get("error") or {}
        if error.get("kind") not in TERMINAL_KINDS:
            return "error.kind %r is not in the closed set" % error.get("kind")
        if not error.get("message"):
            return "typed error carries no message"
        return None
    return "reply has no boolean 'ok'"


def reanchor_blockers(problems, initialising):
    """The problems a re-anchor may not overlook.

    On a tree that has never been accounted for, the ordinary "unregistered
    action" problems ARE the baseline's content, so they are not blockers.
    Everything else - a hang, a crash, a declaration that resolves to nothing -
    blocks, always.
    """
    ordinary = "NEW action without a registered command"
    hard = [p for p in problems if not p.startswith(ordinary)]
    if hard or (problems and not initialising):
        return hard or problems
    return []


def reanchor(baseline_path, reason, surface, exempt, problems, print_line=print):
    """The documented valve: rewrite the baseline from a surface that is sane.

    Returns 0 when the baseline was written, 1 when the valve refused. See
    reanchor_blockers() for what it refuses to overlook.
    """
    initialising = not os.path.exists(baseline_path)
    blockers = reanchor_blockers(problems, initialising)
    if blockers:
        print_line("FAIL: --reanchor refused: %d problem(s) are not ordinary unregistered "
                   "actions, and a re-anchor records a surface that is otherwise accounted "
                   "for" % len(blockers))
        return 1
    unregistered = sorted({action_key(i) for i in surface
                           if is_exempt(i, exempt) is None
                           and not i.get("command") and not i.get("declared_unknown")})
    write_baseline(baseline_path, unregistered, reason)
    print_line("%s: baseline written from the live surface (%d entries)"
               % ("baseline initialised" if initialising else "RE-ANCHORED",
                  len(unregistered)))
    print_line("reason: %s" % reason)
    return 0


def file_sha256(path):
    """Used to prove the gate did not write to its own fixture."""
    import hashlib
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def emit_report(stats, sweep, problems, elapsed, budget, path, print_line=print):
    """Print the human summary, optionally write the machine-readable one."""
    report = {
        "elapsed": round(elapsed, 2),
        "budget": budget,
        "reflected": stats["reflected"],
        "registered": stats["registered"],
        "unregistered": stats["unregistered"],
        "grandfathered": stats["grandfathered"],
        "exempt_generated": stats["exempt"],
        "baseline_entries": stats["baseline"],
        "stale": stats["stale"],
        "commands": stats["commands"],
        "swept": stats["swept"],
        "allowlisted": stats["allowlisted"],
        "sweep": {k: {kk: vv for kk, vv in v.items() if kk != "args"}
                  for k, v in sorted(sweep.items())},
        "problems": problems,
    }
    print_line("")
    print_line("agent_surface: %d reflected, %d exempt (generated menus)"
               % (stats["reflected"], stats["exempt"]))
    print_line("  reflection : %d registered, %d unregistered (%d grandfathered, %d new)"
               % (stats["registered"], stats["unregistered"], stats["grandfathered"],
                  stats["unregistered"] - stats["grandfathered"]))
    print_line("  ratchet    : baseline %d entries, %d stale"
               % (stats["baseline"], stats["stale"]))
    print_line("  reverse    : %d commands, %d swept, %d allowlisted"
               % (stats["commands"], stats["swept"], stats["allowlisted"]))
    print_line("  budget     : %.1f s of %.0f s" % (elapsed, budget))
    if path:
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=1, sort_keys=True)
    return report


# --------------------------------------------------------------------------
# self-test: prove every rule bites, without starting the app
# --------------------------------------------------------------------------

def _item(container, text, command="", declared_unknown=False, surface="menu",
          container_class="QMenu"):
    return {"surface": surface, "container_path": container, "container_class": container_class,
            "text": text, "command": command, "declared_unknown": declared_unknown,
            "object_name": ""}


SELF_TEST_CASES = [
    # (name, expected substring in a problem, or None for "must be clean")
    ("new unregistered action is named", "NEW action without a registered command"),
    ("grandfathered action passes", None),
    ("registered baseline entry is stale", "STALE baseline entry"),
    ("vanished action is stale", "no longer in the surface"),
    ("unknown declared command is hard", "not in the registry"),
    ("unaccounted command", "neither allowlisted nor swept"),
    ("unjustified allowlist entry", "not justified"),
    ("allowlist names unknown command", "does not declare"),
    ("justified allowlist entry passes", None),
    ("hang is a failure", "HANG"),
    ("crash is a failure", "CRASH/PROTOCOL"),
    ("generated menu is exempt", None),
    ("exemption is not a blanket", "NEW action without a registered command"),
    ("duplicate keys are refused", "share the key"),
    ("clean surface passes", None),
]


def _self_test_inputs(case_name):
    good = {"transport.play": {"requires": [], "args_schema": {}},
            "control.quit": {"requires": [], "args_schema": {}}}
    ok_sweep = {"transport.play": {"status": "ok"}, "control.quit": {"status": "ok"}}
    extra = dict(good)
    extra["mixer.add_channel"] = {"requires": [], "args_schema": {}}
    table = {
        "new unregistered action is named": (
            [_item("File", "Open..."), _item("File", "Negative control")],
            good, [], {}, {}, ok_sweep),
        "grandfathered action passes": (
            [_item("File", "Negative control")], good,
            ["menu:File/Negative control"], {}, {}, ok_sweep),
        "registered baseline entry is stale": (
            [_item("File", "Open...", command="transport.play")], good,
            ["menu:File/Open..."], {}, {}, ok_sweep),
        "vanished action is stale": ([], good, ["menu:File/Gone"], {}, {}, ok_sweep),
        "unknown declared command is hard": (
            [_item("File", "Open...", command="nope.nope", declared_unknown=True)],
            good, [], {}, {}, ok_sweep),
        "unaccounted command": ([], extra, [], {}, {}, ok_sweep),
        "unjustified allowlist entry": (
            [], good, [], {"transport.play": "seems awkward"}, {}, ok_sweep),
        "allowlist names unknown command": (
            [], good, [], {"ghost.command": "needs a display"}, {}, ok_sweep),
        "justified allowlist entry passes": (
            [], {"transport.play": {"requires": ["display"], "args_schema": {}},
                 "control.quit": {"requires": [], "args_schema": {}}},
            [], {"transport.play": "opens a window"}, {},
            {"control.quit": {"status": "ok"}}),
        "hang is a failure": (
            [], good, [], {}, {}, {"transport.play": {"status": "timeout"},
                                   "control.quit": {"status": "ok"}}),
        "crash is a failure": (
            [], good, [], {}, {}, {"transport.play": {"status": "dead"},
                                   "control.quit": {"status": "ok"}}),
        "generated menu is exempt": (
            [_item("RecentProjectsMenu", "song.mmp",
                   container_class="lmms::gui::RecentProjectsMenu")],
            good, [], {}, {"class:RecentProjectsMenu": "user data"}, ok_sweep),
        "exemption is not a blanket": (
            [_item("RecentProjectsMenu", "song.mmp",
                   container_class="lmms::gui::RecentProjectsMenu"),
             _item("File", "Sneaky")],
            good, [], {}, {"class:RecentProjectsMenu": "user data"}, ok_sweep),
        "duplicate keys are refused": (
            [_item("File", "Save"), _item("File", "Save")], good, [], {}, {}, ok_sweep),
        "clean surface passes": (
            [_item("File", "Open...", command="transport.play")], good, [], {}, {}, ok_sweep),
    }
    return table[case_name]


def self_test(print_line=print, timeout=45.0):
    """Drive evaluate() with synthetic inputs; every rule must bite. Returns 0 or 1."""
    failures = 0
    width = max(len(name) for name, _ in SELF_TEST_CASES)
    for name, expect in SELF_TEST_CASES:
        surface, commands, baseline, allowlist, exempt, sweep = _self_test_inputs(name)
        problems, _stats = evaluate(surface, commands, baseline, allowlist, exempt, sweep, timeout)
        hit = (not problems) if expect is None else any(expect in p for p in problems)
        if not hit:
            failures += 1
        print_line("  %-*s %s%s" % (width, name, "ok" if hit else "MISSED",
                                    "" if hit else "  problems=%r" % problems))
    print_line("self-test: %d/%d rules bite" % (len(SELF_TEST_CASES) - failures,
                                                len(SELF_TEST_CASES)))
    return 0 if failures == 0 else 1
