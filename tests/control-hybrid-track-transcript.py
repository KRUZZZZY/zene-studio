#!/usr/bin/env python3
"""Live agent-surface evidence for the neutral track (Q050-283.1, stage 1 of 3).

Drives the REAL binary over the control socket through the shared harness and
prints the raw request/response transcript, in order:

  A. `track.add` type=neutral after an instrument row: the reply carries the
     stable trk-<n> id, the arrangement position and type "neutral"; `track.list`
     reads the same row back.
  B. `track.move`: the row keeps its id and changes its position.
  C. `control.undo` removes the row, `control.redo` brings a neutral row back.
  D. `project.save`, `project.new`, `project.open`: the row is restored as a
     neutral row with the id, name and position it had.
  E. `clip.add` on the neutral row is a typed refusal (it holds no clips).

Usage: QT_QPA_PLATFORM=offscreen python3 control-hybrid-track-transcript.py <lmms-binary>
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
    # A fresh instance opens with the default template's tracks; every position below is relative to them.
    base = len(test.rows())
    print("\n---- A. track.add type=neutral (identity and position) ----")
    test.call("track.add", {"type": "instrument", "name": "Synth"})
    added = test.call("track.add", {"type": "neutral", "name": "Vocal"})
    neutral = added.get("track")
    test.require(isinstance(neutral, str) and neutral.startswith("trk-"), "no stable id in %r" % (added,))
    test.require(added.get("type") == "neutral", "type is %r" % added.get("type"))
    test.require(added.get("index") == base + 1, "index is %r, expected %d" % (added.get("index"), base + 1))
    test.require(added.get("clip_count") == 0, "clip_count is %r" % added.get("clip_count"))
    test.require(test.rows()[-1] == (neutral, "neutral", "Vocal", base + 1), "track.list disagrees: %r" % test.rows())

    print("\n---- B. track.move (same id, new position) ----")
    test.call("track.add", {"type": "sample", "name": "Loops"})
    test.call("track.move", {"track": neutral, "index": base + 2})
    test.require(test.rows()[base + 2] == (neutral, "neutral", "Vocal", base + 2), "the moved row is %r" % test.rows())
    test.call("track.move", {"track": neutral, "index": base + 1})

    print("\n---- C. control.undo / control.redo ----")
    other = test.call("track.add", {"type": "neutral", "name": "Second"}).get("track")
    count = len(test.rows())
    test.call("control.undo")
    test.require(len(test.rows()) == count - 1 and all(row[0] != other for row in test.rows()),
        "undo did not remove the added neutral row: %r" % test.rows())
    test.call("control.redo")
    test.require(len(test.rows()) == count and test.rows()[-1][1:3] == ("neutral", "Second"),
        "redo did not bring the neutral row back: %r" % test.rows())

    print("\n---- D. save, new project, open (the round trip) ----")
    before = test.rows()
    path = os.path.join(tmp, "neutral.mmp")
    test.call("project.save", {"path": path})
    test.call("project.new")
    test.require(all(row[1] != "neutral" for row in test.rows()),
        "project.new kept a neutral row: %r" % test.rows())
    test.call("project.open", {"path": path})
    after = test.rows()
    test.require([row for row in after if row[1] == "neutral"] == [row for row in before if row[1] == "neutral"],
        "the neutral rows changed over save/open: %r -> %r" % (before, after))
    test.require(after == before, "the arrangement changed over save/open: %r -> %r" % (before, after))

    print("\n---- E. clip.add on a neutral row is refused ----")
    test.call_error("clip.add", {"track": neutral, "position": 0, "length": 192}, "refused")


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
    tmp = tempfile.mkdtemp(prefix="zneutral-", dir="/tmp")
    try:
        return run_claims(binary, tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
