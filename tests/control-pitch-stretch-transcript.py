#!/usr/bin/env python3
"""RAW control-surface transcript for the pitch-preserving stretch (feature-list row 30).

This is the acceptance evidence that the stretch mode is drivable over the
control socket by an agent: it drives the REAL `zene` binary headless through
the shared harness (tests/control_socket_harness.py) and prints every request
and every reply verbatim.

What it drives, in order:

  1. `track.add` type=sample + `clip.add`   the clip the mode is set on, made
                                            through the commands an agent has;
  2. `warp.list`                            an unwarped clip: the stretch mode
                                            reads `resample`, the default;
  3. `warp.stretch` preserve_pitch          REFUSED, typed: with no rate change
                                            there is nothing to preserve the
                                            pitch across;
  4. `warp.set`                             two markers (the 2x warp);
  5. `warp.stretch` preserve_pitch          accepted; the reply names the mode,
                                            the previous mode and the algorithm;
  6. `warp.list`                            reads back `preserve_pitch`;
  7. `control.transactions`                 the A16 record the call left;
  8. `control.undo`                         takes it back, and `warp.list`
                                            reads `resample` again.

Usage: QT_QPA_PLATFORM=offscreen python3 control-pitch-stretch-transcript.py <zene>
Exit code 0 only when every assertion held.
"""

import sys

import control_socket_harness as H

REQUEST_IDS = iter(range(1, 1000))


class Session:
    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 0

    def call(self, command, args=None):
        self.last_id = next(REQUEST_IDS)
        return self.client.call(self.last_id, command, args, transcript=self.transcript)

    def result(self, command, args=None):
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
    def __init__(self):
        self.results = []
        self.problems = H.Problems()

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))
        if not passed:
            self.problems.add("%s (%s)" % (name, evidence))


def make_clip(session, instance, transcript):
    track = session.result("track.add", {"type": "sample", "name": "Stretch Target"})
    clip = session.result("clip.add", {"track": track.get("track"), "position": 0})
    if not clip.get("clip"):
        H.fail("clip.add returned no clip id (%r)" % clip, instance, transcript)
    return clip.get("clip")


def check_default(session, clip, recorder):
    listed = session.result("warp.list", {"clip": clip})
    recorder.check("an untouched clip reads the resampling default",
                   listed.get("stretch") == "resample" and listed.get("renders_linearly") is True,
                   "stretch=%r renders_linearly=%r" % (listed.get("stretch"),
                                                       listed.get("renders_linearly")))


def check_refusal(session, clip, recorder):
    error = session.typed_error("warp.stretch", {"clip": clip, "mode": "preserve_pitch"})
    recorder.check("preserve_pitch on a clip with no rate change is REFUSED, typed",
                   error.get("kind") == "refused" and "no rate change" in (error.get("message") or ""),
                   "kind=%r message=%r" % (error.get("kind"), (error.get("message") or "")[:90]))
    error = session.typed_error("warp.stretch", {"clip": clip, "mode": "vocoder"})
    recorder.check("a mode the engine does not know is invalid_args",
                   error.get("kind") == "invalid_args",
                   "kind=%r" % error.get("kind"))


def check_set_and_switch(session, clip, recorder):
    warped = session.result("warp.set", {"clip": clip, "markers": [
        {"source_frame": 0, "offset_ticks": 0},
        {"source_frame": 44100, "offset_ticks": 48}]})
    recorder.check("the 2x warp is authored", warped.get("marker_count") == 2,
                   "marker_count=%r" % warped.get("marker_count"))
    switched = session.result("warp.stretch", {"clip": clip, "mode": "preserve_pitch"})
    recorder.check("warp.stretch switches the mode and names the previous one",
                   switched.get("stretch") == "preserve_pitch"
                   and switched.get("previous_mode") == "resample"
                   and switched.get("changed") is True
                   and switched.get("stretch_algorithm") == "wsola"
                   and switched.get("renders_linearly") is False,
                   "stretch=%r previous=%r changed=%r algorithm=%r"
                   % (switched.get("stretch"), switched.get("previous_mode"),
                      switched.get("changed"), switched.get("stretch_algorithm")))
    listed = session.result("warp.list", {"clip": clip})
    recorder.check("warp.list reads the mode back", listed.get("stretch") == "preserve_pitch",
                   "stretch=%r" % listed.get("stretch"))


def check_transaction(session, recorder):
    # No arguments: control.transactions takes none (its schema refuses extras),
    # and the record of the call above is what this reads.
    records = session.result("control.transactions")
    entries = records.get("transactions") or []
    latest = next((r for r in entries if r.get("command") == "warp.stretch"), None)
    recorder.check("the A16 record classifies warp.stretch as a true inverse",
                   latest is not None and latest.get("class") == "true_inverse"
                   and latest.get("reversible") is True
                   and "Clip checkpoint" in (latest.get("mechanism") or ""),
                   "record=%r" % (latest or {}))
    recorder.check("the recorded inverse names the command and the previous mode",
                   latest is not None
                   and (latest.get("inverse") or {}).get("op") == "warp.stretch"
                   and ((latest.get("inverse") or {}).get("args") or {}).get("mode") == "resample",
                   "inverse=%r" % ((latest or {}).get("inverse")))


def check_undo(session, clip, recorder):
    undone = session.result("control.undo")
    listed = session.result("warp.list", {"clip": clip})
    recorder.check("control.undo restores the previous mode",
                   undone.get("undone") is True
                   and undone.get("undone_command") == "warp.stretch"
                   and listed.get("stretch") == "resample",
                   "undone=%r undone_command=%r stretch=%r"
                   % (undone.get("undone"), undone.get("undone_command"), listed.get("stretch")))


def check_quit(session, instance, recorder):
    session.result("control.quit")
    exited, code, elapsed = instance.wait_for_exit(15.0)
    recorder.check("the instance shuts down cleanly", exited and code == 0,
                   "exited=%r exit_code=%r after %.1fs" % (exited, code, elapsed))


def report(recorder):
    for name, passed, evidence in recorder.results:
        print("%-4s %-58s %s" % ("PASS" if passed else "FAIL", name, evidence))


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
        check_default(session, clip, recorder)
        check_refusal(session, clip, recorder)
        check_set_and_switch(session, clip, recorder)
        check_transaction(session, recorder)
        check_undo(session, clip, recorder)
        report(recorder)
        check_quit(session, instance, recorder)
        report(recorder)

    transcript.dump()
    if recorder.problems:
        print("")
        recorder.problems.report("pitch-stretch control-surface transcript")
        return 1
    H.ok("pitch-stretch control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
