#!/usr/bin/env python3
"""Scenario schema v0 for the crashbot runner — references, bounds, and the refusals.

Split out of `runner.py` when that file met the tree's 500-line Gate 7 cap, the same reason
`tests/control_socket_flows.py` exists beside the harness. Nothing here touches a process.

The schema (JSON):

    { "id": "arrange/undo-014",          # <scope>/<name>, unique in a run
      "scope": "arrange",                # the run’s own label, recorded in the ledger
      "seed": 4471,                      # provenance: no unseeded case is evidence
      "groups": ["track", "clip", "note"]  # optional: the command groups this case may call
      "notes": "why this case exists",   # optional free text
      "steps": [
        { "cmd": "transport.get_state",  # a dotted id from the live registry
          "args": {},
          "budget_s": 30,                # optional; TIGHTER than the class bound only
          "read_back": "transport.get_state",  # optional: run this, then compare
          "expect": {"punch_armed": true},     # path -> value, compared to read_back’s result
          "expect_len": {"notes": 3},          # path -> list length
          "expect_error": "invalid_args",      # this step must be a typed refusal of that kind
          "bind": {"drums": "track"},          # name this step’s result path for later steps
          "note": "why this step is here" },
        { "cmd": "note.add", "args": {"clip": "@id:drums_clip", "key": 60, "position": 0,
                                      "length": 192, "velocity": 100} }
      ],
      "teardown": "control.quit" }

The references a step may use:
  `@id:<name>`      a value an EARLIER step bound (ids are session-ordinal - measured trk-27,
                    clip-28, ch-42 on this build - so a pack may not invent one);
  `@work:<name>`    a path under this case's own scratch directory (never a repo path);
  `@fixture:<name>` a copy of `<tree>/tools/mcp-zene-control/tests/data/<name>`, copied out
                    of the tree before any command can write to it (plan rule 1).

Bounds (IR-15) live here so the runner cannot drift from them: a render-class command gets
180 s, everything else the harness's tight socket bound, and a scenario may declare a TIGHTER
budget only - a looser one is a schema refusal, not a silently honoured bound.
"""

import hashlib
import json
import os
import re

# (no sys.path work here: this module imports nothing from the tree - it is pure schema)

RENDER_BUDGET_S = 180.0
# The commands whose cost includes a whole engine start, verbatim from the tree's own
# precedent, tests/freeze_bounce_evidence.py:57-61 (which measured why a render bounded by
# the 30 s socket timeout reports a working render as a hang).
RENDER_COMMANDS = ("render.render", "bounce.in_place", "freeze.track", "freeze.region")
MAX_BUDGET_S = 600.0
SOCKET_BOUND_S = 30.0            # = control_socket_harness.SOCKET_TIMEOUT, restated as data

SCENARIO_KEYS = ("id", "scope", "seed", "groups", "steps", "teardown", "notes")
STEP_KEYS = ("cmd", "args", "budget_s", "read_back", "read_back_args", "expect", "expect_len",
             "expect_call", "expect_call_len", "expect_error", "bind", "note")
OUTCOMES = ("ok", "refusal", "hang", "crash", "mismatch", "schema_refused", "not_run",
            "cap_exceeded")
REF_RE = re.compile(r"^@(id|work|fixture):([^@]+)$")
PATH_REF_RE = re.compile(r"@id:([A-Za-z0-9_]+)")   # a reference INSIDE a path key
SEGMENT_RE = re.compile(r"^([A-Za-z0-9_]+)(?:\[([^\]]*)\])?$")


class SchemaError(Exception):
    """A scenario the schema refuses: the run exits 2 with every reason named."""


# ---------------------------------------------------------------------------
# references and path lookups
# ---------------------------------------------------------------------------


def parse_path(path):
    """`channels[id=ch-1].sends[to=ch-1].amount` -> [(key, selector), ...].

    A selector is an integer index or `key=value` matched against element dicts, so a check
    can name an element by a value this run created without knowing the id in advance.
    """
    segments = []
    for raw in str(path).split("."):
        match = SEGMENT_RE.match(raw)
        if not match:
            raise SchemaError("bad path segment %r in %r" % (raw, path))
        segments.append((match.group(1), match.group(2)))
    return segments


def select_from(node, selector, path):
    if not isinstance(node, list):
        raise KeyError("path %r: %r is not a list" % (path, selector))
    if selector.lstrip("-").isdigit():
        try:
            return node[int(selector)]
        except IndexError as error:
            raise KeyError("path %r: index %s out of range (%d element(s))"
                           % (path, selector, len(node))) from error
    key, _, value = selector.partition("=")
    for element in node:
        if isinstance(element, dict) and str(element.get(key)) == value:
            return element
    raise KeyError("path %r: no element with %s=%s" % (path, key, value))


def lookup_path(node, path):
    for key, selector in parse_path(path):
        if not isinstance(node, dict) or key not in node:
            raise KeyError("path %r: no key %r" % (path, key))
        node = node[key]
        if selector is not None:
            node = select_from(node, selector, path)
    return node


def resolve_ref(value, env):
    """`@id:` -> a bound value; `@work:`/`@fixture:` -> a path under the case's scratch dir."""
    if not isinstance(value, str):
        return value
    match = REF_RE.match(value)
    if not match:
        return value
    kind, name = match.group(1), match.group(2)
    if kind == "id":
        if name not in env["bindings"]:
            raise SchemaError("@id:%s is not bound by any earlier step" % name)
        return env["bindings"][name]
    if kind == "work":
        return os.path.join(env["work_dir"], name)
    source = os.path.join(env["fixture_dir"], name)
    if not os.path.exists(source):
        raise SchemaError("@fixture:%s: no such fixture under %s" % (name, env["fixture_dir"]))
    copied = os.path.join(env["work_dir"], name)
    os.makedirs(env["work_dir"], exist_ok=True)
    if not os.path.exists(copied):
        with open(source, "rb") as src, open(copied, "wb") as dst:
            dst.write(src.read())
    with open(source, "rb") as handle:
        digest = hashlib.sha256(handle.read()).hexdigest()
    env["fixtures_used"][name] = {"source_sha256": digest, "copy": copied}
    return copied


def resolve_value(value, env):
    if isinstance(value, dict):
        return {key: resolve_value(item, env) for key, item in value.items()}
    if isinstance(value, list):
        return [resolve_value(item, env) for item in value]
    return resolve_ref(value, env)


def resolve_path(path, env):
    """`channels[id=@id:cha].sends[to=@id:chb]` -> the same path with the bound ids in it.

    A path KEY carries references too (an element found by a value this run created), so the
    substitution happens on the path text before the lookup: `@id:` reaches the paths the same
    way it reaches arg values.
    """
    def replace(match):
        name = match.group(1)
        if name not in env["bindings"]:
            raise SchemaError("@id:%s is not bound by any earlier step" % name)
        return str(env["bindings"][name])

    return PATH_REF_RE.sub(replace, str(path))


def refs_in(value):
    if isinstance(value, dict):
        return [ref for item in value.values() for ref in refs_in(item)]
    if isinstance(value, list):
        return [ref for item in value for ref in refs_in(item)]
    match = REF_RE.match(value) if isinstance(value, str) else None
    return [(match.group(1), match.group(2))] if match else []


# ---------------------------------------------------------------------------
# expectations and bindings (the runner applies these; the rules live with the schema)
# ---------------------------------------------------------------------------


def expectation_problems(step, result, env, prefix="expect"):
    """Every declared expectation that does not hold, with what was actually seen.

    `expect`/`expect_len` judge the READ-BACK's result when the step declares one (the
    read-back is the point of the assertion); `expect_call`/`expect_call_len` always judge the
    step's own reply - a setter's echo, which exists nowhere else.
    """
    problems = []
    for path, want in (step.get(prefix) or {}).items():
        try:
            got = lookup_path(result, resolve_path(path, env))
        except (KeyError, SchemaError) as error:
            problems.append(str(error))
            continue
        wanted = resolve_value(want, env)
        if got != wanted:
            problems.append("%s %s: got %r, want %r" % (prefix, path, got, wanted))
    for path, want in (step.get(prefix + "_len") or {}).items():
        try:
            got = lookup_path(result, resolve_path(path, env))
        except (KeyError, SchemaError) as error:
            problems.append(str(error))
            continue
        if not isinstance(got, (list, str, dict)) or len(got) != want:
            problems.append("%s_len %s: got %r (%s), want length %r"
                            % (prefix, path, got, type(got).__name__, want))
    return problems


def bindings_for(step, result, env):
    """Apply a step's `bind` map to the environment; returns the problems it could not bind."""
    problems = []
    for name, path in (step.get("bind") or {}).items():
        try:
            env["bindings"][name] = lookup_path(result, path)
        except KeyError as error:
            problems.append("bind %s: %s" % (name, error))
    return problems


# ---------------------------------------------------------------------------
# bounds
# ---------------------------------------------------------------------------


def effective_budget(step):
    """Render class -> declared or RENDER_BUDGET_S; everything else -> declared or None.

    None means "the socket client's own default", i.e. the harness's tight bound - the
    caller must not read None as "unbounded".
    """
    declared = step.get("budget_s")
    if step["cmd"] in RENDER_COMMANDS:
        return float(declared) if declared is not None else RENDER_BUDGET_S
    return float(declared) if declared is not None else None


def budget_check(step):
    """The declared-budget rule as a refusal reason (IR-15), in one place."""
    declared = step.get("budget_s")
    if declared is None:
        return None
    if isinstance(declared, bool) or not isinstance(declared, (int, float)) or declared <= 0:
        return "budget_s must be a positive number (got %r)" % (declared,)
    if declared > MAX_BUDGET_S:
        return "budget_s %r is above the schema cap of %.0fs" % (declared, MAX_BUDGET_S)
    if step["cmd"] not in RENDER_COMMANDS and declared > SOCKET_BOUND_S:
        return ("%s is not a render command: it keeps the tight socket bound of %.0fs, so "
                "budget_s %r is refused (a looser bound reports a hang as a slow call)"
                % (step["cmd"], SOCKET_BOUND_S, declared))
    return None


# ---------------------------------------------------------------------------
# validation
# ---------------------------------------------------------------------------


def validate_step(step, index, bound, groups, fixture_dir, problems):
    where = "step %d" % index
    if not isinstance(step, dict):
        problems.append("%s is not an object" % where)
        return
    unknown = sorted(set(step) - set(STEP_KEYS))
    if unknown:
        problems.append("%s carries keys the schema does not define: %s" % (where, unknown))
    cmd = step.get("cmd")
    if not isinstance(cmd, str) or "." not in cmd:
        problems.append("%s has no dotted cmd (got %r)" % (where, cmd))
        return
    if groups and cmd.split(".")[0] not in groups:
        problems.append("%s: %s is outside this scenario's groups %s" % (where, cmd, groups))
    validate_step_types(step, where, problems)
    problem = budget_check(step)
    if problem:
        problems.append("%s: %s" % (where, problem))
    validate_step_refs(step, where, bound, fixture_dir, problems)


STEP_TYPES = (("read_back", "read_back", str), ("expect", "expect", dict),
              ("expect_len", "expect_len", dict), ("expect_call", "expect_call", dict),
              ("expect_call_len", "expect_call_len", dict),
              ("expect_error", "expect_error", str), ("args", "args", dict),
              ("read_back_args", "read_back_args", dict), ("bind", "bind", dict))


def validate_step_types(step, where, problems):
    """The declared-shape checks, in one table so a new key cannot skip them."""
    for label, key, kinds in STEP_TYPES:
        value = step.get(key)
        if value is not None and not isinstance(value, kinds):
            problems.append("%s: %s must be %s (got %r)" % (where, label, kinds.__name__, value))
    if step.get("read_back_args") is not None and not step.get("read_back"):
        problems.append("%s: read_back_args without a read_back" % where)
    for name in (step.get("bind") or {}):
        if not isinstance(step["bind"][name], str):
            problems.append("%s: bind %s must name a result path" % (where, name))


def validate_step_refs(step, where, bound, fixture_dir, problems):
    """Every `@id:`/`@fixture:` reference must resolve against what earlier steps bound."""
    for name in (step.get("bind") or {}):
        bound.add(name)
    for source in (step.get("args"), step.get("expect"), step.get("expect_call")):
        for kind, name in refs_in(source):
            if kind == "id" and name not in bound:
                problems.append("%s references @id:%s before any step bound it" % (where, name))
            if kind == "fixture" and not os.path.exists(os.path.join(fixture_dir, name)):
                problems.append("%s references @fixture:%s which does not exist under %s"
                                % (where, name, fixture_dir))
    validate_path_refs(step, where, bound, problems)


def validate_path_refs(step, where, bound, problems):
    """The `@id:` references inside an expectation's PATH (an element found by a bound value)."""
    for key in ("expect", "expect_len", "expect_call", "expect_call_len"):
        for path in (step.get(key) or {}):
            for name in PATH_REF_RE.findall(str(path)):
                if name not in bound:
                    problems.append("%s: %s path %r references @id:%s before any step bound it"
                                    % (where, key, path, name))


def top_level_problems(doc):
    """The scenario-level checks: id/scope/seed shape, groups, steps, teardown."""
    problems = []
    unknown = sorted(set(doc) - set(SCENARIO_KEYS))
    if unknown:
        problems.append("top-level keys the schema does not define: %s" % unknown)
    tests = (
        (isinstance(doc.get("id"), str) and "/" in str(doc.get("id")),
         "id must be a string of the form <scope>/<name> (got %r)" % doc.get("id")),
        (isinstance(doc.get("scope"), str), "scope must be a string"),
        (isinstance(doc.get("seed"), int) and not isinstance(doc.get("seed"), bool),
         "seed must be an integer (no unseeded case is admissible evidence)"),
        (doc.get("groups") is None or isinstance(doc.get("groups"), list),
         "groups must be a list of command-group names"),
        (isinstance(doc.get("steps"), list) and bool(doc.get("steps")),
         "steps must be a non-empty list"),
        (doc.get("teardown", "control.quit") == "control.quit",
         "teardown %r is not supported in v0 (only control.quit)" % doc.get("teardown")),
    )
    problems.extend(text for held, text in tests if not held)
    return problems


def validate_scenario(doc, path, fixture_dir):
    if not isinstance(doc, dict):
        raise SchemaError("%s: the scenario is not a JSON object" % path)
    problems = top_level_problems(doc)
    declared = doc.get("steps")
    steps = declared if isinstance(declared, list) else []
    bound = set()
    for index, step in enumerate(steps, start=1):
        validate_step(step, index, bound, doc.get("groups"), fixture_dir, problems)
    if problems:
        raise SchemaError("%s: %d schema problem(s):\n  - %s"
                          % (path, len(problems), "\n  - ".join(problems)))
    return doc


def load_scenario(path, fixture_dir):
    with open(path, "r") as handle:
        return validate_scenario(json.load(handle), path, fixture_dir)


def collect_scenarios(paths, fixture_dir):
    files = []
    for path in paths:
        if os.path.isdir(path):
            files.extend(sorted(os.path.join(path, name) for name in os.listdir(path)
                                if name.endswith(".json")))
        else:
            files.append(path)
    if not files:
        raise SchemaError("no scenario files under %s" % ", ".join(paths))
    return [dict(load_scenario(path, fixture_dir), _path=path) for path in files]
