#!/usr/bin/env python3
"""RAW control-surface transcript for the warp.* command group (SPEC A16 / #597).

This is the acceptance evidence that warp-marker editing is drivable over the
control socket by an agent, produced by driving the REAL `lmms` binary headless
and printing every request and every reply verbatim - no summarising layer, so a
reader can see exactly what the engine answered.

What it drives, in order:

  1. `track.add` type=sample + `clip.add`  the clip a warp is authored on, made
                                           through the commands an agent has;
  2. `warp.list`                            an unwarped clip, with the defaults
                                           docs/WARP.md section 4 documents;
  3. `warp.add` x2                          two markers, added OUT of order;
  4. `warp.list`                            the engine's own source-frame order;
  5. `warp.move`                            one marker to a new timeline offset;
  6. `warp.set`                             the tempo mode + source tempo;
  7. `warp.set` markers=[]                  the whole map cleared;
  8. `warp.add` + `control.undo`            SPEC A16: the edit comes back off;
  9. refusals                               duplicate frame, malformed and
                                           unknown clip ids - every one typed;
 10. `control.transactions`                 the A16 record the edits left.

The instance is started with the documented headless recipe through the shared
harness (tests/control_socket_harness.py), so this file adds no second launch
path. The transcript this prints is committed as
docs/WARP-COMMANDS-TRANSCRIPT.md.

Usage: QT_QPA_PLATFORM=offscreen python3 control-warp-commands-transcript.py <lmms>
Exit code 0 only when every assertion held.
"""

import json
import sys

import control_socket_harness as H

REQUEST_IDS = iter(range(1, 1000))


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 0

    def call(self, command, args=None):
        self.last_id = next(REQUEST_IDS)
        return self.client.call(self.last_id, command, args, transcript=self.transcript)

    def result(self, command, args=None):
        """The reply, or {'error': ...} so a failed call is visible in a check."""
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}

    def typed_error(self, command, args=None):
        reply = self.call(command, args)
        if reply.get("ok") is False:
            return reply.get("error") or {}
        return {}


class Recorder:
    """Collects the named checks and their evidence."""

    def __init__(self):
        self.results = []
        self.problems = H.Problems()

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))
        if not passed:
            self.problems.add("%s (%s)" % (name, evidence))


def sorted_marker_pairs(listed):
    return [(m.get("source_frame"), m.get("offset_ticks")) for m in listed.get("markers") or []]


def is_follow_defaults(listed):
    return (listed.get("marker_count") == 0 and listed.get("warped") is False
            and listed.get("tempo_mode") == "follow"
            and float(listed.get("source_tempo", -1.0)) == 0.0)


def records_are_true_inverse(records):
    return all(r.get("class") == "true_inverse" and r.get("reversible") is True for r in records)


def make_clip(session, instance, transcript):
    """The fixture: a sample track and one clip on it, through the commands."""
    track = session.result("track.add", {"type": "sample", "name": "Warp Target"})
    clip = session.result("clip.add", {"track": track.get("track"), "position": 0})
    if not clip.get("clip"):
        H.fail("clip.add returned no clip id (%r)" % clip, instance, transcript)
    return clip.get("clip")


def check_defaults(session, clip, recorder):
    listed = session.result("warp.list", {"clip": clip})
    recorder.check("an untouched clip reports an empty map", is_follow_defaults(listed),
                   "marker_count=%r warped=%r tempo_mode=%r source_tempo=%r"
                   % (listed.get("marker_count"), listed.get("warped"),
                      listed.get("tempo_mode"), listed.get("source_tempo")))


def check_add(session, clip, recorder):
    """Two markers, added out of order on purpose; the read-back is the engine's."""
    first = session.result("warp.add", {"clip": clip, "source_frame": 176400, "offset_ticks": 96})
    recorder.check("warp.add returns the new map",
                   first.get("marker_count") == 1
                   and (first.get("added") or {}).get("source_frame") == 176400,
                   "marker_count=%r added=%r" % (first.get("marker_count"), first.get("added")))
    session.result("warp.add", {"clip": clip, "source_frame": 44100, "offset_ticks": 24})
    listed = session.result("warp.list", {"clip": clip})
    recorder.check("the map reads back in source-frame order",
                   sorted_marker_pairs(listed) == [(44100, 24), (176400, 96)],
                   "markers=%s" % json.dumps(listed.get("markers")))


def check_move(session, clip, recorder):
    moved = session.result("warp.move", {"clip": clip, "source_frame": 176400,
                                         "offset_ticks": 60})
    recorder.check("warp.move reports the previous offset",
                   (moved.get("moved") or {}).get("previous_offset_ticks") == 96,
                   "moved=%s" % json.dumps(moved.get("moved")))


def check_set_mode_and_tempo(session, clip, recorder):
    leader = session.result("warp.set", {"clip": clip, "mode": "source",
                                         "source_tempo": 140.5})
    recorder.check("warp.set sets the tempo mode and source tempo",
                   leader.get("tempo_mode") == "source"
                   and float(leader.get("source_tempo", -1.0)) == 140.5
                   and leader.get("marker_count") == 2,
                   "tempo_mode=%r source_tempo=%r marker_count=%r"
                   % (leader.get("tempo_mode"), leader.get("source_tempo"),
                      leader.get("marker_count")))


def check_clear(session, clip, recorder):
    cleared = session.result("warp.set", {"clip": clip, "markers": []})
    recorder.check("an empty marker list clears the map",
                   cleared.get("marker_count") == 0 and cleared.get("warped") is False,
                   "marker_count=%r" % cleared.get("marker_count"))


def check_undo(session, clip, recorder):
    """SPEC A16: the checkpoint before the FIRST marker is a state with no <warp>."""
    session.result("warp.add", {"clip": clip, "source_frame": 44100, "offset_ticks": 24})
    undone = session.result("control.undo")
    after = session.result("warp.list", {"clip": clip})
    recorder.check("control.undo takes the marker back off",
                   undone.get("undone") is True and after.get("marker_count") == 0,
                   "undone=%r marker_count=%r" % (undone.get("undone"), after.get("marker_count")))


def check_refusals(session, clip, recorder):
    session.result("warp.add", {"clip": clip, "source_frame": 44100, "offset_ticks": 96})
    duplicate = session.typed_error("warp.add", {"clip": clip, "source_frame": 44100,
                                                 "offset_ticks": 48})
    malformed = session.typed_error("warp.list", {"clip": "not-a-clip"})
    unknown = session.typed_error("warp.list", {"clip": "clip-9999"})
    recorder.check("a duplicate source frame is refused, typed",
                   "source frame" in duplicate.get("message", ""), duplicate.get("message", ""))
    recorder.check("a malformed clip id is refused, typed",
                   bool(malformed.get("message")), malformed.get("message", ""))
    recorder.check("an unknown clip id is refused, typed",
                   bool(unknown.get("message")), unknown.get("message", ""))


def print_records(records):
    print("")
    print("---- the warp.* records control.transactions reports ----")
    for record in records:
        print("%-12s class=%s reversible=%s" % (record.get("command"), record.get("class"),
                                                record.get("reversible")))
        print("             inverse=%s" % json.dumps(record.get("inverse")))
        print("             mechanism=%s" % record.get("mechanism"))


def check_transactions(session, recorder):
    report = session.result("control.transactions")
    records = [r for r in report.get("transactions", [])
               if str(r.get("command", "")).startswith("warp.")]
    print_records(records)
    recorder.check("every mutating warp.* call left an A16 record",
                   len(records) >= 5 and records_are_true_inverse(records),
                   "%d records" % len(records))
    recorder.check("a warp record's inverse names warp.set",
                   any((r.get("inverse") or {}).get("op") == "warp.set" for r in records),
                   "ops=%s" % [((r.get("inverse") or {}).get("op")) for r in records])


def check_quit(session, instance, recorder):
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        recorder.check("control.quit answered", False, "%r" % reply)
        return
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    recorder.check("control.quit stops the instance", exited and code == 0,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))


def report_results(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-52s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)
            recorder.problems.add("%s (%s)" % (name, evidence))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    with H.start_instance(argv[1]) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        print("instance: %s" % argv[1])
        print("socket:   %s" % instance.socket_path)

        session.result("control.version")
        clip = make_clip(session, instance, transcript)
        check_defaults(session, clip, recorder)
        check_add(session, clip, recorder)
        check_move(session, clip, recorder)
        check_set_mode_and_tempo(session, clip, recorder)
        check_clear(session, clip, recorder)
        check_undo(session, clip, recorder)
        check_refusals(session, clip, recorder)
        check_transactions(session, recorder)
        check_quit(session, instance, recorder)

    report_results(recorder)
    transcript.dump()
    if recorder.problems:
        print("")
        recorder.problems.report("warp.* control-surface transcript")
        return 1
    H.ok("warp.* control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
