#!/usr/bin/env python3
"""Shared vocabulary for the `session.*` per-id proof table.

SPLIT OUT of `control-session-api-proof.py` on 2026-09-16 by the 0.3.0 fix-up
pass: the one file was 889 lines with five functions over CCN 10 (Gate 7 and
Gate 4, fork scope). Nothing about the proof changed - the socket client, the
row table, the read-back helpers, the two refusal drivers, the reporting and
the argv/tee plumbing below are the code the one-file version carried, called
in the same order with the same arguments, printing the same strings. The
driver half lives in `session_api_proof_rows.py`; the entry point is still
`control-session-api-proof.py`.

THE CONSTANTS LIVE HERE, not in the entry point, because both other modules
read them and the entry point imports this module (never the reverse). An
import of this module defines things and allocates one request-id counter; it
runs nothing else.
"""

import hashlib
import json
import sys
import time

import control_socket_harness as H

REQUEST_IDS = iter(range(1, 3000))

#: The group's whole surface, in the order the table prints.
IDS = (
    "session.get_state",
    "session.set_grid",
    "session.set_quantisation",
    "session.set_scene",
    "session.set_slot",
    "session.clear",
    "session.clear_slot",
    "session.launch_slot",
    "session.launch_scene",
    "session.stop_slot",
    "session.stop_all",
)

#: The six the A16 table classifies `true_inverse` and the five it classifies
#: `not_mutating`. Measured against the live instance, not assumed: the class
#: in the table is the one the RUNNING registry reported.
MUTATING = ("session.set_grid", "session.set_quantisation", "session.set_scene",
            "session.set_slot", "session.clear_slot", "session.clear")
NON_MUTATING = ("session.get_state", "session.launch_slot", "session.launch_scene",
                "session.stop_slot", "session.stop_all")

#: The play head is parked a quarter of a bar in, so the line a launch is
#: scheduled for is three quarters of a bar away and no launch races a boundary.
SEEK_FRACTION = 0.25
LAUNCH_TIMEOUT = 30.0
DRAIN_TIMEOUT = 20.0
RESET_TIMEOUT = 20.0
GRID_SCENES = 2
#: (track, scene) cells this run populates, plus one in the track-less last
#: column so the "no song track" refusal has a clip to find.
POPULATED = [(0, 0), (1, 0), (2, 0), (0, 1), (1, 1)]
EMPTY_CELL = (2, 1)


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript

    def call(self, command, args=None):
        return self.client.call(next(REQUEST_IDS), command, args, transcript=self.transcript)

    def result(self, command, args=None):
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}

    def error(self, command, args=None):
        reply = self.call(command, args)
        if reply.get("ok") is False:
            return reply.get("error") or {}
        return {}


class Rows:
    """One row per id, and the problems found building them.

    A row is (id, registered, command_sent, reply_ok, observed, a16, verdict).
    Every field is filled from a live call: `verdict` is the only field this
    file decides, and it is derived - STUB when the id never answered ok, and
    MEASURED when it answered, changed what it claims to change and (for the
    mutating six) recorded the class its A16 row declares.
    """

    def __init__(self):
        self.rows = {}
        self.problems = H.Problems()
        self.refusals = []
        #: Set by main(): the binary under test and the live registry's ids.
        self.binary = ""
        self.live_ids = set()

    def row(self, command):
        return self.rows.setdefault(command, {
            "command_sent": "", "reply_ok": None, "observed": "",
            "a16": "", "verdict": "UNMEASURED",
        })

    def check(self, command, passed, evidence):
        if not passed:
            self.problems.add("%s: %s" % (command, evidence))
        return passed

    def refusal(self, command, args, error, expectation):
        """A measured refusal: the id refused the ILLEGAL call as documented."""
        message = error.get("message") or ""
        passed = error.get("kind") == "invalid_args" and expectation in message
        if not passed:
            self.problems.add("%s refusal (%s): %r" % (command, args, error))
        self.refusals.append((command, json.dumps(args, separators=(",", ":")),
                              error.get("kind", "<ok>"), passed))
        return passed


def send(session, rows, command, args=None):
    """Issue one id and record the request verbatim on its row."""
    sent = command + (" " + json.dumps(args, separators=(",", ":"), sort_keys=True) if args else "")
    rows.row(command)["command_sent"] = sent
    return session.result(command, args)


def transaction_for(session, command):
    """The A16 record the live registry holds for `command`, or None."""
    records = session.result("control.transactions").get("transactions") or []
    for record in records:
        if record.get("command") == command:
            return record
    return None


def state_of(session):
    return session.result("session.get_state")


def launch(state):
    return state.get("launch", {})


def grid(state):
    return state.get("grid", {})


def wait_until(session, ready, timeout):
    started = time.time()
    state = state_of(session)
    while time.time() - started < timeout:
        if ready(state):
            return state, time.time() - started, True
        time.sleep(0.1)
        state = state_of(session)
    return state, time.time() - started, False


def hash_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


# ---------------------------------------------------------------------------
# the refusals - a stub cannot refuse the illegal AND perform the legal call
# ---------------------------------------------------------------------------


def drive_schema_refusals(session, rows):
    """The SCHEMA layer, measured first because it needs no grid.

    Measured on run 1 and the reason this block exists separately: a value
    outside the declared argument bound is refused by the REGISTRY, before any
    handler runs, and its message names the bound ("track: 900 is above the
    maximum 255"). A stub-detector that only ever sent such a value would be
    measuring the schema and calling it the handler, so both layers are here.
    """
    rows.refusal("session.set_grid", {"tracks": 9000, "scenes": 2},
                 session.error("session.set_grid", {"tracks": 9000, "scenes": 2}), "tracks")
    rows.refusal("session.set_quantisation", {"quantisation": "whenever"},
                 session.error("session.set_quantisation", {"quantisation": "whenever"}),
                 "quantisation")
    rows.refusal("session.set_scene", {"scene": 900},
                 session.error("session.set_scene", {"scene": 900}), "above the maximum")
    rows.refusal("session.set_slot", {"track": 900, "scene": 0, "type": "empty"},
                 session.error("session.set_slot", {"track": 900, "scene": 0, "type": "empty"}),
                 "above the maximum")
    rows.refusal("session.launch_slot", {"track": 900, "scene": 0},
                 session.error("session.launch_slot", {"track": 900, "scene": 0}), "above the maximum")
    rows.refusal("session.launch_scene", {"scene": 900},
                 session.error("session.launch_scene", {"scene": 900}), "above the maximum")
    rows.refusal("session.clear_slot", {"track": 900, "scene": 0},
                 session.error("session.clear_slot", {"track": 900, "scene": 0}), "above the maximum")
    rows.refusal("session.stop_slot", {"track": 900, "scene": 0},
                 session.error("session.stop_slot", {"track": 900, "scene": 0}), "above the maximum")
    rows.refusal("session.clear", {"tracks": 1},
                 session.error("session.clear", {"tracks": 1}), "")


def drive_grid_refusals(session, rows, song_tracks):
    """The HANDLER layer: a value inside the schema bound but outside the grid.

    This is the half a refusal stub would also pass, which is why it is here
    BESIDE the eleven measured rows and not instead of them: the property a stub
    cannot have is refusing the illegal call AND performing the legal one.
    """
    # The rows above emptied the grid, so rebuild one. This runs LAST on
    # purpose: the eleven rows have already been read, so rebuilding cannot
    # launder any of them.
    columns = song_tracks + 1
    session.result("session.set_grid", {"tracks": columns, "scenes": GRID_SCENES})
    session.result("session.set_slot", {"track": 0, "scene": 1, "type": "midi", "pattern": 1,
                                        "name": "refusal-clip", "quantisation": "global"})
    session.result("session.set_slot", {"track": song_tracks, "scene": 1, "type": "midi",
                                        "pattern": 2, "name": "trackless-clip",
                                        "quantisation": "global"})
    live = grid(state_of(session))
    rows.check("session.set_grid", int(live.get("tracks", -1)) == columns,
               "the refusal grid was not rebuilt: %r" % live)

    rows.refusal("session.set_scene", {"scene": GRID_SCENES},
                 session.error("session.set_scene", {"scene": GRID_SCENES}), "is outside the grid")
    rows.refusal("session.set_slot", {"track": columns, "scene": 0, "type": "empty"},
                 session.error("session.set_slot", {"track": columns, "scene": 0, "type": "empty"}),
                 "is outside the grid")
    rows.refusal("session.set_slot", {"track": 0, "scene": 0, "type": "midi"},
                 session.error("session.set_slot", {"track": 0, "scene": 0, "type": "midi"}),
                 "needs 'pattern'")
    rows.refusal("session.clear_slot", {"track": columns, "scene": 0},
                 session.error("session.clear_slot", {"track": columns, "scene": 0}),
                 "is outside the grid")
    rows.refusal("session.stop_slot", {"track": columns, "scene": 0},
                 session.error("session.stop_slot", {"track": columns, "scene": 0}),
                 "is outside the grid")
    rows.refusal("session.launch_slot", {"track": 0, "scene": 0},
                 session.error("session.launch_slot", {"track": 0, "scene": 0}), "is empty")
    rows.refusal("session.launch_slot", {"track": song_tracks, "scene": 1},
                 session.error("session.launch_slot", {"track": song_tracks, "scene": 1}),
                 "has no song track")
    rows.refusal("session.launch_scene", {"scene": GRID_SCENES},
                 session.error("session.launch_scene", {"scene": GRID_SCENES}), "is outside the grid")


# ---------------------------------------------------------------------------
# reporting
# ---------------------------------------------------------------------------


def print_table(rows, binary_hash, instance, ticks_per_bar, drive_order):
    print("")
    print("==== session.* per-id proof table ====")
    print("binary:    %s" % rows.binary)
    print("sha256:    %s" % binary_hash)
    print("socket:    %s" % instance.socket_path)
    print("ticks/bar: %d" % ticks_per_bar)
    print("drive order: %s" % " -> ".join(drive_order))
    print("")
    header = ("%-24s %-10s %-6s %s" % ("id", "registered", "reply", "measured behaviour / A16 (live)"))
    print(header)
    print("-" * len(header))
    for command in IDS:
        row = rows.rows.get(command) or {}
        registered = "yes" if command in rows.live_ids else "NO"
        reply = {True: "ok", False: "error", None: "-"}[row.get("reply_ok")]
        print("%-24s %-10s %-6s %s" % (command, registered, reply, row.get("observed", "<no row built>")))
        print("%-24s %-10s %-6s   A16 %s" % ("", "", "", row.get("a16", "")))
        print("%-24s %-10s %-6s   VERDICT: %s" % ("", "", "", row.get("verdict", "UNMEASURED")))
    print("")
    print("==== measured refusals (each id refuses the ILLEGAL call) ====")
    for command, args, kind, passed in rows.refusals:
        print("  %-24s %-34s kind=%-14s %s" % (command, args, kind, "ok" if passed else "FAILED"))


def print_transcript(transcript, elide_below=2000):
    """The raw transcript, with the one payload that would drown it elided.

    `control.commands_list` answers with every registered command's schemas -
    measured at ~410 KB of a ~460 KB run, because the surface has 173 ids. It is
    elided HERE and only here, with the count and the reason printed in its
    place, because every one of its entries is a different id's registered
    reference and the table above is this file's answer for the eleven that
    matter. Nothing else is elided: the per-id requests, their replies, the
    refusal replies and the A16 transaction records are printed in full, because
    they ARE the evidence.
    """
    print("\n---- raw request/response transcript ----")
    for line in transcript.lines:
        if line.startswith("-< ") or line.startswith("<- "):
            if "commands_list" not in line and len(line) > elide_below:
                print("%s [%d bytes, printed in full below]" % (line[:120], len(line)))
                print("...%s" % line[-elide_below:])
                continue
        if "control.commands_list" in line and line.startswith("-> "):
            print(line)
            continue
        if '"commands":[' in line and line.startswith("<- "):
            print('<- {"id":%s,"ok":true,"result":{"commands":[<%d entries elided: every '
                  'registered command\'s schemas and requires declaration; the eleven this '
                  'file audits have their own rows in the table above>],"count":%d,"proto":1}}'
                  % (line.split('"id":', 1)[-1].split(",", 1)[0],
                     line.count('"group":'), line.count('"group":')))
            continue
        print(line)


def parse_argv(argv):
    """`<binary> [--out PATH]` - the artefact path is the only option."""
    binary = None
    out = None
    index = 1
    while index < len(argv):
        if argv[index] == "--out" and index + 1 < len(argv):
            out = argv[index + 1]
            index += 2
            continue
        binary = binary or argv[index]
        index += 1
    return binary, out


class Tee:
    """Writes the run to stdout and (when asked) to the artefact file."""

    def __init__(self, handle=None, console=None):
        self.handle = handle
        self.console = console if console is not None else sys.stdout

    def write(self, text):
        self.console.write(text)
        if self.handle is not None:
            self.handle.write(text)

    def flush(self):
        self.console.flush()
        if self.handle is not None:
            self.handle.flush()

