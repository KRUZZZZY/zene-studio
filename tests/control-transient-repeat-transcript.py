#!/usr/bin/env python3
"""RAW control-surface transcript for the transient Repeat / Gate warp modes (Q050-162).

Drives the REAL `zene` binary headless through the shared harness
(tests/control_socket_harness.py), prints every request and reply verbatim,
and asserts what an agent sees:

  1. `track.add` + `sample.generate`   a 1-bar tone clip made through the
                                        commands an agent has;
  2. `warp.stretch` repeat             REFUSED, typed, while the clip renders
                                        linearly (no warp to fill);
  3. `warp.set`                         a half-speed warp (the clip spans 2 bars);
  4. `warp.stretch` repeat 1/8          accepted: mode, rate, the interval at the
                                        project tempo (250 ms at 120 BPM) and the
                                        render algorithm in the reply, and in
                                        `warp.list`; a rate given twice is
                                        invalid_args;
  5. `render.render`                    the warped span rendered (hash A);
  6. `warp.stretch` gate 300 ms         the free rate and the other mode; the
                                        render differs (hash B != A);
  7. `control.transactions`             a true_inverse whose inverse names the
                                        previous mode AND rate;
  8. `control.undo`                     back to repeat 1/8, and the render is
                                        hash A again;
  9. `project.save` / `project.open`    the mode and rate survive, the file says
                                        stretch="repeat" repeat_rate="1/8", and
                                        the reopened project renders hash A.

Usage: QT_QPA_PLATFORM=offscreen python3 control-transient-repeat-transcript.py <zene>
Exit code 0 only when every assertion held.
"""

import os
import sys

import control_socket_harness as H

REQUEST_IDS = iter(range(1, 1000))
WARP_MARKERS = [{"source_frame": 0, "offset_ticks": 0}, {"source_frame": 88200, "offset_ticks": 384}]
SPAN = {"start_ticks": 0, "end_ticks": 384}


class Session:
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
    session.result("transport.set_tempo", {"bpm": 120})
    track = session.result("track.add", {"type": "sample", "name": "Transient Target"})
    clip = session.result("sample.generate", {"track": track.get("track"), "position": 0,
                                              "length": 192, "kind": "tone", "frequency": 1000,
                                              "amplitude": 0.8})
    if not clip.get("clip"):
        H.fail("sample.generate returned no clip id (%r)" % clip, instance, transcript)
    return clip.get("clip")


def render(session, outdir, name):
    rendered = session.result("render.render", dict(SPAN, out=os.path.join(outdir, name)))
    return rendered.get("sha256")


def check_refusals(session, clip, recorder):
    error = session.typed_error("warp.stretch", {"clip": clip, "mode": "repeat"})
    recorder.check("repeat on a clip that renders linearly is REFUSED, typed",
                   error.get("kind") == "refused", "kind=%r" % error.get("kind"))
    session.result("warp.set", {"clip": clip, "markers": WARP_MARKERS})
    error = session.typed_error("warp.stretch", {"clip": clip, "mode": "repeat",
                                                 "rate_division": "1/8", "rate_ms": 100})
    recorder.check("a rate given both ways is invalid_args",
                   error.get("kind") == "invalid_args", "kind=%r" % error.get("kind"))


def check_repeat(session, clip, recorder):
    switched = session.result("warp.stretch", {"clip": clip, "mode": "repeat", "rate_division": "1/8"})
    rate = switched.get("repeat_rate") or {}
    recorder.check("warp.stretch selects repeat at a synced 1/8",
                   switched.get("stretch") == "repeat" and switched.get("changed") is True
                   and switched.get("stretch_algorithm") == "transients"
                   and rate.get("name") == "1/8" and rate.get("synced") is True
                   and abs((rate.get("interval_ms") or 0) - 250.0) < 0.01,
                   "stretch=%r algorithm=%r rate=%r" % (switched.get("stretch"),
                                                        switched.get("stretch_algorithm"), rate))
    listed = session.result("warp.list", {"clip": clip})
    recorder.check("warp.list reads repeat 1/8 back",
                   listed.get("stretch") == "repeat"
                   and (listed.get("repeat_rate") or {}).get("name") == "1/8",
                   "stretch=%r rate=%r" % (listed.get("stretch"), listed.get("repeat_rate")))


def check_gate(session, clip, recorder):
    switched = session.result("warp.stretch", {"clip": clip, "mode": "gate", "rate_ms": 300})
    rate = switched.get("repeat_rate") or {}
    recorder.check("warp.stretch selects gate at a free 300 ms",
                   switched.get("stretch") == "gate" and switched.get("previous_mode") == "repeat"
                   and switched.get("previous_repeat_rate") == "1/8"
                   and rate.get("name") == "300ms" and rate.get("synced") is False,
                   "stretch=%r previous=%r rate=%r" % (switched.get("stretch"),
                                                       switched.get("previous_repeat_rate"), rate))
    check_gate_record(session, recorder)


def check_gate_record(session, recorder):
    records = session.result("control.transactions").get("transactions") or []
    # The newest warp.stretch record (the list runs oldest first).
    latest = next((r for r in reversed(records) if r.get("command") == "warp.stretch"), None) or {}
    inverse = latest.get("inverse") or {}
    args = inverse.get("args") or {}
    recorder.check("the A16 record is a true inverse naming the previous mode and rate",
                   latest.get("class") == "true_inverse" and inverse.get("op") == "warp.stretch"
                   and args.get("mode") == "repeat" and args.get("rate_division") == "1/8",
                   "record=%r" % latest)


def check_renders(session, clip, recorder, outdir):
    hashes = {}
    session.result("warp.stretch", {"clip": clip, "mode": "repeat", "rate_division": "1/8"})
    hashes["repeat"] = render(session, outdir, "repeat.wav")
    check_gate(session, clip, recorder)
    hashes["gate"] = render(session, outdir, "gate.wav")
    recorder.check("repeat and gate render differently",
                   hashes["repeat"] and hashes["gate"] and hashes["repeat"] != hashes["gate"],
                   "repeat=%r gate=%r" % (hashes["repeat"], hashes["gate"]))
    undone = session.result("control.undo")
    listed = session.result("warp.list", {"clip": clip})
    hashes["undone"] = render(session, outdir, "undone.wav")
    recorder.check("one control.undo restores repeat 1/8 and its render",
                   undone.get("undone") is True and listed.get("stretch") == "repeat"
                   and (listed.get("repeat_rate") or {}).get("name") == "1/8"
                   and hashes["undone"] == hashes["repeat"],
                   "stretch=%r rate=%r hash=%r" % (listed.get("stretch"), listed.get("repeat_rate"),
                                                   hashes["undone"]))
    return hashes["repeat"]


def check_round_trip(session, clip, recorder, outdir, expected):
    path = os.path.join(outdir, "transient-repeat.mmp")
    session.result("project.save", {"path": path})
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    recorder.check("the saved file carries the mode and the rate",
                   'stretch="repeat"' in text and 'repeat_rate="1/8"' in text,
                   "repeat=%r rate=%r" % ('stretch="repeat"' in text, 'repeat_rate="1/8"' in text))
    session.result("warp.stretch", {"clip": clip, "mode": "gate", "rate_ms": 50})
    session.result("project.open", {"path": path})
    listed = session.result("warp.list", {"clip": clip})
    reopened = render(session, outdir, "reopened.wav")
    recorder.check("project.open brings repeat 1/8 back and renders the same audio",
                   listed.get("stretch") == "repeat"
                   and (listed.get("repeat_rate") or {}).get("name") == "1/8"
                   and reopened == expected,
                   "stretch=%r rate=%r hash=%r" % (listed.get("stretch"), listed.get("repeat_rate"), reopened))


def check_quit(session, instance, recorder):
    session.result("control.quit")
    exited, code, elapsed = instance.wait_for_exit(15.0)
    recorder.check("the instance shuts down cleanly", exited and code == 0,
                   "exited=%r exit_code=%r after %.1fs" % (exited, code, elapsed))


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
        session.result("control.version")
        clip = make_clip(session, instance, transcript)
        check_refusals(session, clip, recorder)
        check_repeat(session, clip, recorder)
        expected = check_renders(session, clip, recorder, instance.tmp)
        check_round_trip(session, clip, recorder, instance.tmp, expected)
        check_quit(session, instance, recorder)

    for name, passed, evidence in recorder.results:
        print("%-4s %-62s %s" % ("PASS" if passed else "FAIL", name, evidence))
    transcript.dump()
    if recorder.problems:
        print("")
        recorder.problems.report("transient repeat control-surface transcript")
        return 1
    H.ok("transient repeat control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
