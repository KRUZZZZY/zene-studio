#!/usr/bin/env python3
"""THE per-id proof table for the `session.*` command group, driven through ONE
live `--control-socket` instance.

WHY THIS FILE EXISTS. The group has two registered proofs already -
`control-session-m1.py` (ctest ControlSessionLaunch: the grid, the scene launch
and the launch read-back) and `control-session-lifecycle-transcript.py` (ctest
ControlSessionLifecycleTranscript: launch_slot / stop_slot / stop_all /
clear_slot / clear). Each drives the ids it needs, in the order the FEATURE
wants. Neither answers the question this file exists to answer, which is the
audit question and not the feature question:

    for EACH of the group's eleven ids, is the thing that ran the WORK,
    or is it a stub that only refuses?

So the unit of this file is the ID, not the feature. Every id gets one row,
the row is built from a live call and its read-back, and the row carries the
measuring command and the measured result - including the A16 classification
read back OUT OF THE RUNNING INSTANCE (`control.transactions`), so the table's
"registered reference" column is a measurement too and not a grep.

THE ELEVEN IDS (the group's whole surface, in the order the table prints):

    session.get_state  session.set_grid  session.set_quantisation
    session.set_scene  session.set_slot  session.clear  session.clear_slot
    session.launch_slot  session.launch_scene  session.stop_slot
    session.stop_all

DRIVE ORDER vs TABLE ORDER. The table prints in the fixed order above; the
driving order is the one that keeps the session legal (a grid must exist before
a cell is addressed; a cell must hold a clip before it is launched; a clear
must come after the reads it would otherwise erase). The drive order is printed
by the run, so a reader can see that no row depends on state a later row built.

WHAT A REFUSAL STUB WOULD LOOK LIKE, and why a row would say so: a handler that
returns only a typed failure. Every row here asserts the id answered `ok:true`
for a LEGAL call AND that the claimed effect is visible in the next read-back,
so a stub cannot pass a row. The refusals the group DOES have (an out-of-grid
address, an empty cell, a column with no song track) are measured too, as the
refusal rows at the bottom, because "refuses the illegal call" is the property
a stub would fail to combine with "performs the legal one".

WHAT IT DOES NOT PROVE. A launch's per-slot PHASE is not readable over the
socket (session.get_state reports the model's cells and the aggregate launch
counters, not SlotLaunchState), so nothing here asserts a stopped slot returned
to Idle; that half is tests/src/core/SessionSchedulerTest.cpp. And a launched
slot renders no audio in this tree (src/core/SessionClip.cpp is serialisation
only) - the limitation docs/KNOWN-LIMITATIONS.md carries - so "started" here
means the engine recorded the start on the scheduled line, which is exactly
what launch.start_line reports and is stated as such rather than as audio.

Started through the shared harness (tests/control_socket_harness.py), so this
file adds no second launch path.

Usage: QT_QPA_PLATFORM=offscreen python3 control-session-api-proof.py <zene>
Exit code 0 only when every row's checks held; 77 (ctest Skipped, never Passed)
when this build has no session.* group at all (WANT_SESSION_VIEW=OFF).

SPLIT, 2026-09-16 (the 0.3.0 fix-up pass): this file was 889 lines with five
functions over CCN 10. The machinery it used to carry is now
`session_api_proof_lib.py` (client, row table, read-back helpers, refusal
drivers, reporting) and `session_api_proof_rows.py` (the eleven row drivers);
this file keeps the group's surface, the drive order and the exit contract.
The proof's behaviour is unchanged - same ids, same order, same rows, same
strings, same exit codes (0 measured / 77 no group / 1 a row failed).
"""

import os
import sys

import control_socket_harness as H

from session_api_proof_lib import (IDS, MUTATING, NON_MUTATING, Rows, Session, hash_of,
                                    parse_argv, print_table, print_transcript, Tee,
                                    drive_schema_refusals, drive_grid_refusals)
from session_api_proof_rows import (drive_clear, drive_clear_slot, drive_get_state,
                                    drive_launch_scene, drive_launch_slot, drive_set_grid,
                                    drive_set_quantisation, drive_set_scene, drive_set_slot,
                                    drive_stop_all, drive_stop_slot, drive_transport)


# ---------------------------------------------------------------------------
# the drive order and the run itself
# ---------------------------------------------------------------------------


def announce(instance, session, rows):
    """Print the run's own header and read the live registry's id set."""
    version = session.result("control.version")
    listing = session.result("control.commands_list")
    rows.live_ids = {entry.get("id") for entry in listing.get("commands", [])}
    print("instance:   %s" % rows.binary)
    print("socket:     %s" % instance.socket_path)
    print("version:    %r" % version)
    print("live ids:   %d" % len(rows.live_ids))


def registration_gap(rows):
    """77 when this build registers NO session.* id at all, else None.

    A partial gap is a problem, not a skip: it means the group is half-built.
    """
    missing = [command for command in IDS if command not in rows.live_ids]
    if not missing:
        return None
    if len(missing) == len(IDS):
        print("this build registers no session.* id (WANT_SESSION_VIEW=OFF): "
              "Skipped, never Passed")
        return 77
    rows.problems.add("registered ids missing from the live registry: %r" % missing)
    return None


def drive_all(session, rows, instance, drive_order):
    """Every measured row, in the legal drive order; returns (ticks_per_bar, song_tracks)."""
    # The schema layer first: it needs no grid, and measuring it here keeps
    # it distinct from the handler layer at the bottom.
    drive_schema_refusals(session, rows)
    drive_order += ["(the schema-layer refusals)"]

    drive_get_state(session, rows)
    columns, song_tracks, _ = drive_set_grid(session, rows)
    drive_order += ["session.get_state", "session.set_grid", "session.set_quantisation",
                    "session.set_scene", "session.set_slot"]
    drive_set_quantisation(session, rows)
    drive_set_scene(session, rows)
    drive_set_slot(session, rows, columns, song_tracks)
    _, ticks_per_bar = drive_transport(session, rows)
    drive_order += ["session.launch_slot", "session.launch_scene", "session.stop_slot",
                    "session.stop_all", "session.clear_slot", "session.clear"]
    drive_launch_slot(session, rows)
    drive_launch_scene(session, rows)
    drive_stop_slot(session, rows)
    drive_stop_all(session, rows)
    drive_clear_slot(session, rows)
    drive_clear(session, rows, instance)
    return ticks_per_bar, song_tracks


def check_a16_classes(session, rows):
    """The five launch/stop verbs record NO transaction - their A16 claim."""
    recorded = {record.get("command")
                for record in (session.result("control.transactions").get("transactions") or [])}
    offending = sorted(command for command in NON_MUTATING if command in recorded)
    if offending:
        rows.problems.add("launch/stop verbs recorded a transaction: %r" % offending)
    for command in MUTATING:
        if command not in recorded:
            rows.problems.add("%s recorded no A16 transaction" % command)


def check_every_row_measured(rows):
    """Every declared row must exist and be MEASURED."""
    for command in IDS:
        row = rows.rows.get(command)
        if row is None:
            rows.problems.add("%s: no row was built" % command)
        elif row["verdict"] != "MEASURED":
            rows.problems.add("%s: verdict is %s" % (command, row["verdict"]))


def quit_instance(session, instance, rows):
    """control.quit must answer ok AND the instance must exit 0."""
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        rows.problems.add("control.quit did not answer ok: %r" % reply)
        return
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    if not (exited and code == 0):
        rows.problems.add("control.quit did not stop the instance: exited=%r code=%r"
                          % (exited, code))


def run(binary, handle):
    rows = Rows()
    rows.binary = binary
    drive_order = []
    transcript = H.Transcript()
    with H.start_instance(binary) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        announce(instance, session, rows)

        skipped = registration_gap(rows)
        if skipped is not None:
            return skipped

        ticks_per_bar, song_tracks = drive_all(session, rows, instance, drive_order)
        check_a16_classes(session, rows)

        drive_grid_refusals(session, rows, song_tracks)
        drive_order += ["(the grid-layer refusals)"]

        check_every_row_measured(rows)
        quit_instance(session, instance, rows)

    print_table(rows, hash_of(binary), instance, ticks_per_bar, drive_order)
    print_transcript(transcript)
    if rows.problems:
        print("")
        rows.problems.report("session.* per-id proof table")
        return 1
    H.ok("session.* per-id proof table (every one of the eleven ids measured)")
    return 0


def main(argv):
    binary, out = parse_argv(argv)
    if binary is None:
        print(__doc__)
        return 2
    console = sys.stdout
    handle = open(out, "w") if out else None
    tee = Tee(handle, console)
    sys.stdout = tee
    try:
        return run(os.path.abspath(binary), handle)
    finally:
        # Restore the REAL stdout and flush the artefact BEFORE closing it:
        # leaving a closed handle as sys.stdout makes the interpreter's own
        # final flush fail, and CPython turns that into exit code 120 on a run
        # whose every check held (measured - the first --out run reported 120
        # beside its own PASS line, which no ctest would have accepted).
        sys.stdout = console
        tee.flush()
        if handle is not None:
            handle.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv))

