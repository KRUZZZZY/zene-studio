#!/usr/bin/env python3
"""The MCP tooling spine's ratchet: the bridge's exposed tools vs the binary.

THE CLAIM UNDER TEST, in one sentence: every command id this tree's binary
registers is reachable as an MCP tool through `tools/mcp-zene-control`, whether or
not an instance is running.

WHY IT IS A REGISTERED CTEST AND NOT A UNIT TEST. `registry.py` generates the
bridge's tool list from a live instance's `control.commands_list` at request
time, so a new command group is automatically an MCP tool - that is the spine.
The modes with no live instance are served from the offline copies next to the
bridge, and nothing enforced that those described the same surface as the binary
this tree just built: the unit test asserted only that the snapshot file existed
and held five sample ids. It had drifted to 70 commands while the registry
declared 108 - 38 missing, every one of them a 0.3.0 feature - so tooling for the
new work looked absent in exactly the mode a reader tries first. A check that
cannot see drift is not a check of this file.

So this one starts the REAL binary (the `ControlSessionLaunch` mould:
`$<TARGET_FILE:zene>`, `QT_QPA_PLATFORM=offscreen`, the shared
`control_socket_harness`), asks it for `control.commands_list`, and then compares
THREE surfaces in BOTH directions, naming the ids on any disagreement:

  1. the committed snapshot vs the binary. The snapshot is loaded through the
     BRIDGE'S OWN loader (`zene_control.registry.load_bundle` - so it is parsed
     the way the bridge parses it, not the way this script would prefer it to be):
       * an id the binary declares and the snapshot lacks   -> FAIL ("missing")
       * an id the snapshot carries and the binary does not -> FAIL ("extra")
  2. the TOOLS the bridge exposes while the instance answers, against the ids
     that same instance just declared. `server.py` adds exactly two bridge-owned
     tools (`zene_commands`, `zene_status`) on top of one generated tool per
     `commands_list` id, so this is the list an MCP client's `tools/list` returns.
  3. the TOOLS the bridge exposes with nothing answering - twice: with an empty
     state directory (the committed snapshot is the whole surface) and with a
     copy of the OLD cache planted in it. That second one is the failure this
     check exists for: the bridge used to serve the offline copies in a fixed
     order (cache first), so a cache captured from an older instance shadowed a
     newer committed snapshot for good. Measured on this tree, a 70-id
     0.1.0-alpha cache made 74 current command ids unreachable while the snapshot
     itself was current - the whole of COVERAGE-MATRIX-2026-09-13.md §4.4. The
     freshest readable copy must win instead, and the check asserts the bridge
     says so (`source` is the snapshot, `skipped_offline` names the cache).

THE FIX IS PRINTED WITH THE FAILURE: the exact
`python3 tools/mcp-zene-control/snapshot_commands.py --socket <sock>` invocation,
against a live instance, then commit the regenerated file.

CONFIGURATION. The snapshot is captured from the RELEASE configuration (both
command-group options ON). Two CMake options remove a whole group at compile
time - `WANT_SESSION_VIEW=OFF` (the `session.*` group) and `ZENE_TELEMETRY=OFF`
(the `telemetry.*` group) - and a build that chose either has a shorter live list
for a reason that is not drift. `tests/CMakeLists.txt` passes each one in as
`--compiled-out <prefix>`; those ids are excused from the comparison, and the
flag is checked in BOTH directions, so a flag naming a group the binary DOES
declare is a failure rather than a silent blindfold.

There is ONE option in the other direction, and it needs both directions checked
for the same reason: `WANT_WASM=ON` with the wasmtime C API on the find path
compiles the `wasm.*` group in (item #614, src/core/ControlCommandsWasm.cpp), so
such a build has a LONGER live list than the release snapshot - six ids the
offline list cannot carry, because the snapshot is one file and a configuration
is not. `--compiled-in <prefix>` declares that, is checked in both directions
too (declared but the binary registers none of them -> failure; declared and the
snapshot carries them -> failure, because then the flag is not describing a
difference), and `tests/CMakeLists.txt` passes `--compiled-in wasm.` exactly when
it configured the sandbox in. The two offline comparisons are made against the
snapshot's own id set, so a configuration difference cannot make them red.

SKIP, NOT PASS. Exit 77 (ctest reports "Skipped" through SKIP_RETURN_CODE) when
the bridge package cannot be imported at all: that is the one condition under
which this test cannot look at anything, and a test that cannot look must never
be registered as one that passed. There is no `WANT_*` option that gates the
bridge itself - it is Python under `tools/`, not a build target - so `python3`
being absent (which stops the test being registered by `tests/CMakeLists.txt`,
with a message(STATUS)) is the other configuration that does not run it. The
bridge's stdio server needs the `mcp` distribution; when that is not importable
the tool-list half reports the generated-tool ids from `registry.py` instead of
`tools/list`, rather than going red for a missing optional import.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-commands-snapshot.py <zene-binary> \
        [--compiled-out PREFIX]... [--compiled-in PREFIX]...

Exit codes: 0 every surface matches; 1 drift or a missing id, or an unusable
snapshot; 2 cannot run (no binary at the path); 77 skipped (bridge not importable).
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
BRIDGE_DIR = REPO / "tools" / "mcp-zene-control"
SNAPSHOT = BRIDGE_DIR / "zene_control" / "commands_snapshot.json"

#: ctest's "did not run" convention; kept in step with SKIP_RETURN_CODE in
#: tests/CMakeLists.txt by the module constant below being the only source.
SKIP_CODE = 77

#: The stamp planted on the synthetic old cache. Fixed and far in the past, so
#: the planted copy can never be the freshest one by accident.
STALE_STAMP = "2000-01-01T00:00:00Z"

#: How many of the snapshot's own ids the synthetic old cache carries. Three is
#: enough to be a strict subset and keeps the transcript readable.
STALE_IDS = 3

sys.path.insert(0, str(HERE))

import control_socket_harness as H  # noqa: E402  (path set above)

FIX = ("python3 tools/mcp-zene-control/snapshot_commands.py --socket <sock>"
       "    # <sock> = a live instance's control socket")


def parse_args(argv):
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("binary", help="the built zene binary ($<TARGET_FILE:zene>)")
    parser.add_argument("--compiled-out", action="append", default=[], metavar="PREFIX",
                        help="a command-id prefix this build's configuration removed at "
                             "compile time (session. for -DWANT_SESSION_VIEW=OFF, "
                             "telemetry. for -DZENE_TELEMETRY=OFF); repeatable")
    parser.add_argument("--compiled-in", action="append", default=[], metavar="PREFIX",
                        help="a command-id prefix this build's configuration ADDS to the "
                             "surface, which the release snapshot therefore cannot carry "
                             "(wasm. for -DWANT_WASM=ON with the wasmtime C API present); "
                             "repeatable")
    return parser.parse_args(argv[1:])


def load_bridge():
    """The bridge's own modules as a namespace, or (None, why it cannot be loaded).

    `registry`, `config` and `tools` are the halves the bridge's command surface
    is built from and need nothing but the standard library. `server` (the stdio
    server) imports the `mcp` distribution, so it is optional here: the generated
    tool list is `registry`+`tools` work, and an absent optional import must not
    turn this test red.
    """
    if str(BRIDGE_DIR) not in sys.path:
        sys.path.insert(0, str(BRIDGE_DIR))
    try:
        import zene_control.config as config
        import zene_control.registry as registry
        import zene_control.tools as tools
    except ImportError as error:
        return None, "the MCP bridge package is not importable from %s (%s)" % (BRIDGE_DIR, error)
    try:
        import zene_control.server as server
    except ImportError:
        server = None
    return SimpleNamespace(config=config, registry=registry, tools=tools, server=server), ""


def committed_ids(modules):
    """The snapshot's id set through the bridge's own loader, as (ids, "")."""
    bundle = modules.registry.load_bundle(str(SNAPSHOT))
    if bundle is None:
        return None, ("the committed snapshot %s is missing, unreadable, or is not a %r "
                      "bundle" % (SNAPSHOT, modules.registry.BUNDLE_KIND))
    ids = {str(entry.get("id") or "") for entry in bundle["commands"]}
    ids.discard("")
    print("snapshot          %s" % SNAPSHOT)
    print("snapshot ids      %d command(s), captured %s, proto %s"
          % (len(ids), bundle.get("captured_at"), bundle.get("proto")))
    print("snapshot origin   %s" % json.dumps(bundle.get("instance") or {}, sort_keys=True))
    return ids, ""


def exposed_ids(modules, socket_path, state_dir, offline):
    """What the bridge would offer, as (ids, source, skipped, bridge_tools).

    `bridge_tools` is the `tools/list` size when the stdio server is importable,
    and None when it is not (see load_bridge).
    """
    config = modules.config
    cfg = config.Config(socket_path=socket_path, workdir=str(BRIDGE_DIR),
                        state_dir=state_dir, offline=offline)
    bridge = modules.tools.Bridge(cfg)
    source = bridge.resolve()
    tools = (len(modules.server.BridgeServer(cfg).tool_payloads())
             if modules.server is not None else None)
    # `skipped` (the copies ranked staler and passed over) is reported by the
    # bridge this check ships with. getattr keeps an older bridge's CommandSource
    # from crashing the check: the assertion that matters is the id set below.
    return [spec.id for spec in source.specs], source.source, getattr(source, "skipped", ()), tools


def plant_stale_cache(modules, state_dir, ids):
    """Write the synthetic old cache: a strict subset of the snapshot, dated 2000.

    This is the shape that caused the measured 74-id gap - an offline copy from
    an older instance, left in the state directory the bridge reads - reduced to
    its essentials so the check is deterministic and needs no old binary.
    """
    bundle = {"kind": modules.registry.BUNDLE_KIND, "schema": modules.registry.BUNDLE_SCHEMA,
              "captured_at": STALE_STAMP, "proto": 1, "count": len(ids),
              "instance": {"version": "planted-stale-cache"},
              "commands": [{"id": item, "group": item.split(".")[0]} for item in sorted(ids)]}
    path = os.path.join(state_dir, "commands.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(bundle, handle, indent=2, sort_keys=True)
    return path


def compare_tool_ids(mode, exposed, expected, expected_label):
    """(problems) for one mode: the bridge's ids must equal `expected` exactly."""
    missing = sorted(set(expected) - set(exposed))
    extra = sorted(set(exposed) - set(expected))
    print("                  vs %s: %d missing, %d extra" % (expected_label, len(missing),
                                                             len(extra)))
    problems = []
    if missing:
        print("MISSING from the %s tool list  %d - the DAW declares these and the bridge "
              "exposes no tool for them" % (mode, len(missing)))
        for entry in missing:
            print("                    %s" % entry)
        problems.append("the %s tool list is missing %d id(s) the DAW declares"
                        % (mode, len(missing)))
    if extra:
        print("EXTRA in the %s tool list      %d - the bridge exposes these and the DAW "
              "declares none of them" % (mode, len(extra)))
        for entry in extra:
            print("                    %s" % entry)
        problems.append("the %s tool list carries %d id(s) the DAW does not declare"
                        % (mode, len(extra)))
    return problems


def check_bridge_coverage(modules, socket_path, live, committed):
    """Every registered id is reachable as an MCP tool, with and without an instance.

    Three modes, because the bridge answers from three places and only one of them
    is the live instance. The stale-cache mode is the regression guard for the
    defect this whole check was added for: see the module docstring.
    """
    print("bridge            %s" % (BRIDGE_DIR / "zene_control" / "registry.py"))
    problems = []
    live_state = tempfile.mkdtemp(prefix="zene-snapshot-live-", dir="/tmp")
    ids, source, _skipped, tools = exposed_ids(modules, socket_path, live_state, offline=False)
    print("bridge live       %d generated tool(s)%s, source %s"
          % (len(ids), "" if tools is None else " (%d with the bridge tools)" % tools, source))
    if source != "live":
        problems.append("an instance was answering at %s and the bridge still served its "
                        "command list from the %s" % (socket_path, source))
    problems += compare_tool_ids("live", ids, live, "the binary it just asked")

    clean = tempfile.mkdtemp(prefix="zene-snapshot-clean-", dir="/tmp")
    ids, source, _skipped, tools = exposed_ids(modules, "/tmp/zene-snapshot-absent.sock",
                                               clean, offline=True)
    print("bridge offline    %d generated tool(s)%s, source %s"
          % (len(ids), "" if tools is None else " (%d with the bridge tools)" % tools, source))
    if source != "snapshot":
        problems.append("with an empty state directory and no instance the bridge served %s, "
                        "not the committed snapshot" % source)
    problems += compare_tool_ids("offline", ids, committed, "the committed snapshot")

    stale = tempfile.mkdtemp(prefix="zene-snapshot-stale-", dir="/tmp")
    planted = sorted(committed)[:STALE_IDS]
    plant_stale_cache(modules, stale, planted)
    ids, source, skipped, tools = exposed_ids(modules, "/tmp/zene-snapshot-absent.sock",
                                              stale, offline=True)
    print("bridge stale-cache %d generated tool(s)%s, source %s, passed over %s"
          % (len(ids), "" if tools is None else " (%d with the bridge tools)" % tools,
             source, json.dumps([item.get("source") for item in skipped])))
    print("                  planted: %d id(s) captured %s" % (len(planted), STALE_STAMP))
    if source != "snapshot":
        problems.append("a cache captured %s shadowed the committed snapshot: the bridge "
                        "served %s instead (%d id(s) unreachable that the snapshot carries)"
                        % (STALE_STAMP, source, len(set(committed) - set(ids))))
    problems += compare_tool_ids("stale-cache", ids, committed, "the committed snapshot")
    return problems


def live_ids(binary, probe=None):
    """Start the real binary, ask it for its command list, then shut it down.

    A bounded harness, exactly like the other control-surface tests: every wait
    in `control_socket_harness` has a deadline, and an expired deadline is a
    failure, never a wait. `probe(entries, socket_path)` runs while the instance
    is still answering - the bridge cannot be asked what it would expose for a
    surface that has already exited - and its return value is handed back.
    """
    instance = H.start_instance(binary)
    try:
        client = H.connect(instance)
        H.wait_ready(instance, client, None)
        reply = client.call(1, "control.commands_list")
        result = H.ok_result(reply, 1)
        entries = result.get("commands")
        if not isinstance(entries, list) or not entries:
            raise AssertionError("control.commands_list returned no commands array: %r" % (reply,))
        print("live ids          %d command(s), proto %s" % (len(entries), result.get("proto")))
        findings = probe(entries, instance.socket_path) if probe is not None else None
        client.call(2, "control.quit")
        client.close()
        instance.wait_for_exit(H.QUIT_TIMEOUT)
        return [str(entry.get("id") or "") for entry in entries], findings
    except (H.Timeout, AssertionError) as error:
        # A bounded-wait expiry or a malformed reply is a FAILURE with the app log
        # attached, never a traceback and never a wait (control_socket_harness's rule).
        H.fail("the binary did not answer control.commands_list: %s" % error, instance)
    finally:
        instance.close()


def excused_ids(live, committed, prefixes):
    """(ids a --compiled-out flag excuses, problems with the flag itself)."""
    problems = []
    live_set = set(live)
    excused = set()
    for prefix in prefixes:
        declared = {entry for entry in committed if entry.startswith(prefix)}
        if not declared:
            problems.append("--compiled-out %s names no id the snapshot carries, so the flag "
                            "does not describe this tree" % prefix)
            continue
        present = sorted(entry for entry in live_set if entry.startswith(prefix))
        if present:
            problems.append("--compiled-out %s was declared, but the binary DOES register %d of "
                            "its %d id(s) (e.g. %s): the flag is wrong for this build, and "
                            "excusing them would hide real drift"
                            % (prefix, len(present), len(declared), ", ".join(present[:3])))
        excused |= declared
    return excused, problems


def admitted_ids(live, committed, prefixes):
    """(ids a --compiled-in flag excuses, problems with the flag itself).

    The mirror of excused_ids. These are ids this configuration ADDS, which a
    snapshot captured from the release configuration cannot carry: the snapshot
    is one file and a configuration is not. Checked in both directions for the
    same reason excused_ids is - a prefix the binary does not register describes
    nothing, and a prefix the snapshot already carries would excuse real drift.
    """
    problems = []
    admitted = set()
    for prefix in prefixes:
        present = {entry for entry in live if entry.startswith(prefix)}
        if not present:
            problems.append("--compiled-in %s names no id this binary registers, so the flag "
                            "does not describe this build" % prefix)
            continue
        if any(entry.startswith(prefix) for entry in committed):
            problems.append("--compiled-in %s was declared, but the snapshot ALREADY carries "
                            "ids with that prefix: the flag would excuse real drift" % prefix)
            continue
        admitted |= present
    return admitted, problems


def compare(live, committed, compiled_out, compiled_in):
    """Every way the two id sets can disagree.

    Returns (problems, absent, extra, excused, admitted):
      * `absent` - ids this binary registers and the snapshot does not offer: the
        snapshot is stale, or a new command group was never regenerated into it.
      * `extra`  - ids the snapshot offers and this binary does not register: a group
        was removed or renamed and the offline list still advertises it.
    """
    live_set = set(live)
    excused, problems = excused_ids(live, committed, compiled_out)
    admitted, admitted_problems = admitted_ids(live, committed, compiled_in)
    problems += admitted_problems
    absent = sorted(live_set - committed - admitted)
    extra = sorted(committed - excused - live_set)
    if absent:
        problems.append("%d command id(s) this binary registers are NOT in the snapshot"
                        % len(absent))
    if extra:
        problems.append("%d command id(s) the snapshot carries are NOT registered by this "
                        "binary" % len(extra))
    return problems, absent, extra, excused, admitted


def print_diff(absent, extra, excused, admitted):
    if excused or admitted:
        print("excused by flags  %d id(s) this configuration removed at compile time, %d it adds"
              % (len(excused), len(admitted)))
    for label, ids, why in (
            ("MISSING from the snapshot", absent,
             "the binary registers these; the offline list does not offer them"),
            ("EXTRA in the snapshot", extra,
             "the offline list offers these; this binary does not register them")):
        if not ids:
            continue
        print("%s  %d - %s" % (label, len(ids), why))
        for entry in ids:
            print("                    %s" % entry)


def print_failure(findings, drifted):
    print("FAIL: the committed snapshot and the built binary do not agree (%d finding(s), "
          "%d drifted command id(s))." % (findings, drifted))
    print("      The bridge serves the snapshot whenever no instance is running (and no cache "
          "exists),")
    print("      so this IS the tool list an agent sees before it opens the DAW.")
    print("      Regenerate it with the bridge's own generator, against a live instance, never "
          "by hand,")
    print("      then commit the regenerated file:")
    print("        %s" % FIX)


def print_coverage_failure(findings):
    print("FAIL: the MCP bridge does not expose every command id this binary registers "
          "(%d finding(s))." % findings)
    print("      %s generates one tool per id it reads from a live instance, so a gap here is "
          "not" % (BRIDGE_DIR / "zene_control" / "registry.py"))
    print("      a per-feature bridge defect: the bridge served a stale offline copy. The "
          "freshest")
    print("      readable copy must win (registry.rank_offline_bundles), and the offline list "
          "is the")
    print("      committed snapshot - regenerate and commit it with:")
    print("        %s" % FIX)


def coverage_probe(modules, committed):
    """The probe the live phase runs while the instance is still answering."""
    def probe(entries, socket_path):
        return check_bridge_coverage(modules, socket_path,
                                     [str(entry.get("id") or "") for entry in entries], committed)
    return probe


def skip_report(why):
    print("SKIP: %s" % why)
    print("      the drift ratchet cannot look at anything, so it reports Skipped (exit "
          "%d) rather than Passed: %s" % (SKIP_CODE, FIX))
    return SKIP_CODE


def snapshot_missing_report(why):
    print("FAIL: %s" % why)
    print("      It is what makes `zene_commands` answer before any instance exists.")
    print("      Regenerate it with the bridge's own generator:")
    print("        %s" % FIX)
    return 1


def verdict(problems, absent, extra, excused, admitted, coverage_problems, committed):
    """Print both halves' findings and return the exit code."""
    for item in problems:
        print("  - %s" % item)
    if problems:
        print_failure(len(problems), len(absent) + len(extra))
    for item in coverage_problems or []:
        print("  - %s" % item)
    if coverage_problems:
        print_coverage_failure(len(coverage_problems))
    if problems or coverage_problems:
        return 1
    print("PASS: the committed offline snapshot and this binary register the SAME %d command "
          "id(s)%s" % (len(committed) - len(excused),
                       "" if not (excused or admitted)
                       else " (%d excused by --compiled-out, %d admitted by --compiled-in)"
                            % (len(excused), len(admitted))))
    print("PASS: the bridge exposes a tool for EVERY one of them, live, offline, and with a "
          "stale cache planted")
    return 0


def main(argv):
    options = parse_args(argv)
    if not os.path.exists(options.binary):
        print("cannot run: no binary at %s" % options.binary)
        return 2
    modules, why = load_bridge()
    if modules is None:
        return skip_report(why)
    committed, why = committed_ids(modules)
    if committed is None:
        return snapshot_missing_report(why)
    print("binary            %s" % os.path.abspath(options.binary))
    live, coverage_problems = live_ids(options.binary, coverage_probe(modules, committed))
    problems, absent, extra, excused, admitted = compare(
        live, committed, options.compiled_out, options.compiled_in)
    print_diff(absent, extra, excused, admitted)
    return verdict(problems, absent, extra, excused, admitted, coverage_problems, committed)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
