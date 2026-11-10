#!/usr/bin/env python3
"""Live agent-surface evidence for assigning an instrument to a neutral row (Q050-283.2, stage 2 of 3).

Drives the REAL binary over the control socket through the shared harness and
prints the raw request/response transcript, in order:

  A. `track.set_role` role=instrument instrument=tripleoscillator on a neutral row
     between two other rows: the reply and `track.list` keep the row's trk-<n> id,
     position and name; the type and `role` read "instrument"; the other rows are
     unchanged.
  B. One `control.undo` gives the neutral row back at the same place, one
     `control.redo` the instrument row.
  C. A clip on the instrument row; `track.set_role` role=neutral keeps the row and
     reports `kept_roles` [{role: instrument, instrument: tripleoscillator, automation_links: 0}].
  D. `project.save`, `project.new`, `project.open`: the neutral row and its kept role
     come back; `track.set_role` role=instrument then gives the instrument and its
     clip back at the same place.
  E. Typed refusals: no instrument named on a row with no kept role, and a sample
     track (no role to switch).

Usage: QT_QPA_PLATFORM=offscreen python3 control-hybrid-track-role-transcript.py <lmms-binary>
Exit code 0 only when every assertion passed.
"""

import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from control_socket_harness import (  # noqa: E402
    PING_TIMEOUT, READY_TIMEOUT, Transcript, connect, ok_result,
    start_instance, typed_error, wait_ready,
)

USAGE = __doc__


class TranscriptTest:
    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.next_id = 100
        self.problems = []

    def call(self, command, arguments=None):
        self.next_id += 1
        return ok_result(self.client.call(self.next_id, command, arguments or {},
            timeout=PING_TIMEOUT, transcript=self.transcript), self.next_id)

    def call_error(self, command, arguments, kind):
        self.next_id += 1
        return typed_error(self.client.call(self.next_id, command, arguments,
            timeout=PING_TIMEOUT, transcript=self.transcript), self.next_id, kind)

    def require(self, condition, text):
        if not condition:
            self.problems.append(text)
        return bool(condition)

    def rows(self):
        return [(row["id"], row["type"], row["name"], row["index"])
                for row in self.call("track.list").get("tracks", [])]


def claims(test, tmp):
    base = len(test.rows())
    print("\n---- A. track.set_role role=instrument (identity and position kept) ----")
    test.call("track.add", {"type": "sample", "name": "Loops"})
    row = test.call("track.add", {"type": "neutral", "name": "Idea"}).get("track")
    test.call("track.add", {"type": "sample", "name": "Tail"})
    index = base + 1
    others = [r for r in test.rows() if r[0] != row]
    switched = test.call("track.set_role", {"track": row, "role": "instrument", "instrument": "tripleoscillator"})
    test.require(switched.get("track") == row, "the reply names %r, not %r" % (switched.get("track"), row))
    test.require(switched.get("index") == index, "index is %r, expected %d" % (switched.get("index"), index))
    test.require(switched.get("role") == "instrument", "role is %r" % switched.get("role"))
    test.require(test.rows()[index] == (row, "instrument", "Idea", index), "track.list disagrees: %r" % test.rows())
    test.require([r for r in test.rows() if r[0] != row] == others, "the other rows changed: %r" % test.rows())

    print("\n---- B. control.undo / control.redo ----")
    test.call("control.undo")
    test.require(test.rows()[index] == (row, "neutral", "Idea", index), "undo gave %r" % test.rows())
    test.call("control.redo")
    test.require(test.rows()[index] == (row, "instrument", "Idea", index), "redo gave %r" % test.rows())

    print("\n---- C. role=neutral keeps the instrument role ----")
    test.call("clip.add", {"track": row, "position": 0, "length": 192})
    neutral = test.call("track.set_role", {"track": row, "role": "neutral"})
    test.require(neutral.get("type") == "neutral" and neutral.get("index") == index, "the switch gave %r" % neutral)
    kept = test.call("track.get_state", {"track": row}).get("kept_roles")
    test.require(kept == [{"role": "instrument", "instrument": "tripleoscillator", "automation_links": 0}], "kept_roles is %r" % (kept,))

    print("\n---- D. save, new project, open; then the instrument back ----")
    before = test.rows()
    path = os.path.join(tmp, "kept-role.mmp")
    test.call("project.save", {"path": path})
    test.call("project.new")
    test.call("project.open", {"path": path})
    test.require(test.rows() == before, "the arrangement changed over save/open: %r -> %r" % (before, test.rows()))
    kept = test.call("track.get_state", {"track": row}).get("kept_roles")
    test.require(kept == [{"role": "instrument", "instrument": "tripleoscillator", "automation_links": 0}], "kept_roles after open is %r" % (kept,))
    back = test.call("track.set_role", {"track": row, "role": "instrument"})
    test.require(back.get("track") == row and back.get("index") == index and back.get("clip_count") == 1,
        "the restored row is %r" % back)
    test.require(test.rows()[index] == (row, "instrument", "Idea", index), "track.list after restore: %r" % test.rows())

    print("\n---- E. typed refusals ----")
    fresh = test.call("track.add", {"type": "neutral"}).get("track")
    test.call_error("track.set_role", {"track": fresh, "role": "instrument"}, "invalid_args")
    loops = test.rows()[base][0]
    test.call_error("track.set_role", {"track": loops, "role": "instrument", "instrument": "tripleoscillator"}, "refused")


def run_claims(binary, tmp):
    transcript = Transcript()
    instance = start_instance(binary)
    try:
        client = connect(instance)
        wait_ready(instance, client, transcript, seconds=READY_TIMEOUT, ping_timeout=PING_TIMEOUT)
        test = TranscriptTest(client, transcript)
        claims(test, tmp)
        try:
            ok_result(client.call(999, "control.quit", {"save": False}, timeout=PING_TIMEOUT,
                transcript=transcript), 999)
        except Exception:  # noqa: BLE001 - the reply need not arrive before exit
            pass
        transcript.dump()
        return 0 if not test.problems else report_failure(test.problems)
    except Exception as error:  # noqa: BLE001 - any failed claim is a failure, never a skip
        print("\nFAIL: %s" % error)
        transcript.dump()
        instance.dump_log()
        return 1
    finally:
        instance.close()


def report_failure(problems):
    print("\nFAIL:")
    for item in problems:
        print("  - %s" % item)
    return 1


def main():
    if len(sys.argv) < 2:
        print(USAGE)
        return 2
    binary = os.path.abspath(sys.argv[1])
    if not os.path.exists(binary):
        print(USAGE)
        return 2
    tmp = tempfile.mkdtemp(prefix="zrole-", dir="/tmp")
    try:
        return run_claims(binary, tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
