#!/usr/bin/env python3
"""The MCP tooling spine's freshness ratchet: the committed snapshot vs the binary.

THE CLAIM UNDER TEST: the offline command list the MCP bridge serves when no
instance is running - `tools/mcp-zene-control/zene_control/commands_snapshot.json` -
describes the SAME command surface as the binary this tree just built.

WHY IT IS A REGISTERED CTEST AND NOT A UNIT TEST. `registry.py` generates the
bridge's tool list from a live instance's `control.commands_list` at request
time, so a new command group is automatically an MCP tool - that is the spine.
The mode with no live instance (and no cache either) is served from the committed
snapshot, and NOTHING enforced that the snapshot was fresh: the unit test
asserted only that the file existed and held five sample ids. It had drifted to
70 commands while the registry declared 108 - 38 missing, every one of them a
0.3.0 feature - so tooling for the new work looked absent in exactly the mode a
reader tries first. A check that cannot see drift is not a check of this file.

So this one starts the REAL binary (the `ControlSessionLaunch` mould:
`$<TARGET_FILE:zene>`, `QT_QPA_PLATFORM=offscreen`, the shared
`control_socket_harness`), asks it for `control.commands_list`, loads the
committed snapshot through the BRIDGE'S OWN loader
(`zene_control.registry.load_bundle` - so the snapshot is parsed the way the
bridge parses it, not the way this script would prefer it to be), and compares
the two id sets in BOTH directions:

  * an id the binary declares and the snapshot lacks  -> FAIL ("missing")
  * an id the snapshot carries and the binary does not -> FAIL ("extra")
  * no drift at all                                    -> PASS

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
it configured the sandbox in.

SKIP, NOT PASS. Exit 77 (ctest reports "Skipped" through SKIP_RETURN_CODE) when
the bridge package cannot be imported at all: that is the one condition under
which this test cannot look at anything, and a test that cannot look must never
be registered as one that passed. There is no `WANT_*` option that gates the
bridge itself - it is Python under `tools/`, not a build target - so `python3`
being absent (which stops the test being registered by `tests/CMakeLists.txt`,
with a message(STATUS)) is the other configuration that does not run it.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-commands-snapshot.py <zene-binary> \
        [--compiled-out PREFIX]... [--compiled-in PREFIX]...

Exit codes: 0 the snapshot matches; 1 drift, or the snapshot is unusable;
2 cannot run (no binary at the path); 77 skipped (the bridge is not importable).
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
BRIDGE_DIR = REPO / "tools" / "mcp-zene-control"
SNAPSHOT = BRIDGE_DIR / "zene_control" / "commands_snapshot.json"

#: ctest's "did not run" convention; kept in step with SKIP_RETURN_CODE in
#: tests/CMakeLists.txt by the module constant below being the only source.
SKIP_CODE = 77

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
    """The bridge's own registry module as (module, ""), or (None, why not)."""
    if str(BRIDGE_DIR) not in sys.path:
        sys.path.insert(0, str(BRIDGE_DIR))
    try:
        from zene_control import registry
    except ImportError as error:
        return None, "the MCP bridge package is not importable from %s (%s)" % (BRIDGE_DIR, error)
    return registry, ""


def committed_ids(registry):
    """The snapshot's id set through the bridge's own loader, as (ids, "")."""
    bundle = registry.load_bundle(str(SNAPSHOT))
    if bundle is None:
        return None, ("the committed snapshot %s is missing, unreadable, or is not a %r "
                      "bundle" % (SNAPSHOT, registry.BUNDLE_KIND))
    ids = {str(entry.get("id") or "") for entry in bundle["commands"]}
    ids.discard("")
    print("snapshot          %s" % SNAPSHOT)
    print("snapshot ids      %d command(s), captured %s, proto %s"
          % (len(ids), bundle.get("captured_at"), bundle.get("proto")))
    print("snapshot origin   %s" % json.dumps(bundle.get("instance") or {}, sort_keys=True))
    return ids, ""


def live_ids(binary):
    """Start the real binary, ask it for its command list, then shut it down.

    A bounded harness, exactly like the other control-surface tests: every wait
    in `control_socket_harness` has a deadline, and an expired deadline is a
    failure, never a wait.
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
        client.call(2, "control.quit")
        client.close()
        instance.wait_for_exit(H.QUIT_TIMEOUT)
        return [str(entry.get("id") or "") for entry in entries]
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


def main(argv):
    options = parse_args(argv)
    if not os.path.exists(options.binary):
        print("cannot run: no binary at %s" % options.binary)
        return 2
    registry, why = load_bridge()
    if registry is None:
        print("SKIP: %s" % why)
        print("      the drift ratchet cannot look at anything, so it reports Skipped (exit "
              "%d) rather than Passed: %s" % (SKIP_CODE, FIX))
        return SKIP_CODE
    committed, why = committed_ids(registry)
    if committed is None:
        print("FAIL: %s" % why)
        print("      It is what makes `zene_commands` answer before any instance exists.")
        print("      Regenerate it with the bridge's own generator:")
        print("        %s" % FIX)
        return 1
    print("binary            %s" % os.path.abspath(options.binary))
    live = live_ids(options.binary)
    problems, absent, extra, excused, admitted = compare(
        live, committed, options.compiled_out, options.compiled_in)
    print_diff(absent, extra, excused, admitted)
    if problems:
        for item in problems:
            print("  - %s" % item)
        print_failure(len(problems), len(absent) + len(extra))
        return 1
    print("PASS: the committed offline snapshot and this binary register the SAME %d command "
          "id(s)%s" % (len(committed) - len(excused),
                       "" if not (excused or admitted)
                       else " (%d excused by --compiled-out, %d admitted by --compiled-in)"
                            % (len(excused), len(admitted))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
