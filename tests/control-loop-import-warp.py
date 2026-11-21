#!/usr/bin/env python3
"""RAW control-surface transcript for an imported loop's tempo and key (0.5.0 L05, Q050-093).

Drives the REAL `zene` binary headless, printing every request and reply verbatim. The fixtures are
synthesised here with a known answer - four bars of an A major scale at 100 BPM over an A drone, and the same
at 120 BPM in C (the project's key comes from detect.apply on that one) - the same synthesis as
tests/src/core/LoopFixture.h.

The checks, in order:
  1. the project is put at 140 BPM in C major (transport.set_tempo, detect.apply key only);
  2. clip.import_file loop_mode=follow_project: the reply carries the detected ~100 BPM and tonic A, the tempo
     followed and +3 semitones applied;
  3. clip.source_get_state reads the same back, and what the clip plays: warp tempo mode `source` at 100 BPM,
     transpose +3, four project bars long; warp.list (the warp group's own verb) agrees;
  4. the project's tempo and key did not move;
  5. ONE control.undo takes the clip and its new track off;
  6. settings.set import/loopwarp=keep_unstretched, then an import with no loop_mode keeps its own rate
     (mode_from=preference, zero transpose, its natural length); the preference set to follow_project makes the
     next import follow;
  7. an unknown loop_mode is refused, typed, and changes nothing.
Usage: QT_QPA_PLATFORM=offscreen python3 control-loop-import-warp.py <zene>
Exit code 0 only when every assertion held.
"""

import math
import os
import sys
import tempfile

import control_socket_harness as H
from control_detect_fixtures import SAMPLE_RATE, write_wave

REQUEST_IDS = iter(range(1, 1000))
FOUR_BARS = 4 * 192
PROJECT_TEMPO = 140


def loop_fixture(bpm, tonic_hz, beats=16):
    """One note per beat walking the major scale up from tonic_hz, over a drone an octave below."""
    beat = 60.0 / bpm
    total = int(beats * beat * SAMPLE_RATE)
    samples = [0.0] * total
    ratios = [1, 9 / 8, 5 / 4, 4 / 3, 3 / 2, 5 / 3, 15 / 8, 2]
    for n in range(beats):
        start = int(n * beat * SAMPLE_RATE)
        length = int(beat * 0.9 * SAMPLE_RATE)
        hz = tonic_hz * ratios[n % 8]
        for i in range(min(length, total - start)):
            envelope = math.exp(-3.0 * i / length) * min(1.0, i / (0.002 * SAMPLE_RATE))
            samples[start + i] += 0.35 * envelope * math.sin(2 * math.pi * hz * i / SAMPLE_RATE)
    for i in range(total):
        samples[i] += 0.3 * math.sin(2 * math.pi * (tonic_hz / 2) * i / SAMPLE_RATE)
    return samples


class Session:
    def __init__(self, client, transcript, recorder):
        self.client = client
        self.transcript = transcript
        self.recorder = recorder

    def call(self, command, args=None):
        return self.client.call(next(REQUEST_IDS), command, args, transcript=self.transcript)

    def result(self, command, args=None):
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}

    def check(self, name, passed, evidence):
        self.recorder.append((name, bool(passed), evidence))

    def track_count(self):
        return self.result("track.list").get("count", -1)


def part(reply, name):
    """A nested object of a reply, empty when absent."""
    value = reply.get(name)
    return value if isinstance(value, dict) else {}


def picked(state, *names):
    return {name: state.get(name) for name in names}


def near(value, target, tolerance):
    return isinstance(value, (int, float)) and abs(value - target) <= tolerance


def check_follow(session, loop_path):
    imported = session.result("clip.import_file", {"path": loop_path, "loop_mode": "follow_project"})
    loop = part(imported, "loop")
    tempo = part(loop, "source_tempo")
    key = part(loop, "source_key")
    session.check("follow_project: ~100 BPM and tonic A detected, tempo followed, +3 applied",
                  all([near(tempo.get("bpm"), 100.0, 0.5), key.get("pitch_class") == 9,
                       loop.get("tempo_followed") is True, loop.get("transpose_applied") == 3,
                       loop.get("mode_from") == "argument"]),
                  "tempo=%r key=%r %r" % (tempo, key.get("tonic"),
                                          picked(loop, "loop_mode", "tempo_followed", "transpose_applied")))
    clip = imported.get("clip")
    state = session.result("clip.source_get_state", {"clip": clip})
    session.check("clip.source_get_state: leads at 100 BPM, transposed +3, four project bars",
                  all([state.get("tempo_mode") == "source", state.get("warp_source_tempo") == 100.0,
                       state.get("transpose") == 3, near(state.get("length"), FOUR_BARS, 1),
                       state.get("loop_mode") == "follow_project"]),
                  "%r" % picked(state, "tempo_mode", "warp_source_tempo", "stretch", "transpose", "length"))
    warp = session.result("warp.list", {"clip": clip})
    session.check("warp.list agrees: tempo mode source at 100 BPM",
                  all([warp.get("tempo_mode") == "source", warp.get("source_tempo") == 100.0]),
                  "%r" % picked(warp, "tempo_mode", "source_tempo"))
    return clip


def check_project_untouched(session):
    transport = session.result("transport.get_state")
    detect = session.result("detect.get_state")
    session.check("the project's tempo and key did not move",
                  all([transport.get("tempo") == PROJECT_TEMPO, part(detect, "key").get("pitch_class") == 0,
                       part(detect, "tempo_map").get("event_count") == 0]),
                  "tempo=%r key=%r tempo_map=%r" % (transport.get("tempo"), part(detect, "key").get("tonic"),
                                                    part(detect, "tempo_map")))


def import_with_preference(session, loop_path, preference):
    session.result("settings.set", {"key": "import/loopwarp", "value": preference})
    imported = session.result("clip.import_file", {"path": loop_path})
    return part(imported, "loop"), session.result("clip.source_get_state", {"clip": imported.get("clip")})


def check_preference(session, loop_path):
    kept_loop, kept = import_with_preference(session, loop_path, "keep_unstretched")
    session.check("preference keep_unstretched: no loop_mode keeps its own rate and pitch",
                  all([kept_loop.get("mode_from") == "preference", kept_loop.get("loop_mode") == "keep_unstretched",
                       kept.get("tempo_mode") == "follow", kept.get("transpose") == 0,
                       kept.get("analysed") is True, kept.get("length") != FOUR_BARS]),
                  "%r" % picked(kept, "loop_mode", "tempo_mode", "transpose", "length"))
    followed_loop, followed = import_with_preference(session, loop_path, "follow_project")
    session.check("preference follow_project: the next import follows",
                  all([followed_loop.get("mode_from") == "preference", followed.get("tempo_mode") == "source",
                       followed.get("transpose") == 3]),
                  "%r" % picked(followed, "loop_mode", "tempo_mode", "transpose", "length"))


def run_checks(session, workdir):
    loop_path = os.path.join(workdir, "a-major-100.wav")
    key_path = os.path.join(workdir, "c-major-120.wav")
    write_wave(loop_path, loop_fixture(100.0, 220.0))
    write_wave(key_path, loop_fixture(120.0, 261.63))

    session.result("transport.set_tempo", {"bpm": PROJECT_TEMPO})
    applied = session.result("detect.apply", {"path": key_path, "tempo": False, "key": True})
    session.check("the project is in C (detect.apply, key only)",
                  part(applied, "key_state").get("pitch_class") == 0, "key_state=%r" % applied.get("key_state"))

    tracks = session.track_count()
    clip = check_follow(session, loop_path)
    check_project_untouched(session)
    undo = session.call("control.undo")
    gone = session.call("clip.source_get_state", {"clip": clip})
    session.check("one control.undo takes the clip and its new track off",
                  undo.get("ok") is True and session.track_count() == tracks and gone.get("ok") is False,
                  "tracks %r -> %r, read-back after undo ok=%r" % (tracks, session.track_count(), gone.get("ok")))
    check_preference(session, loop_path)
    before = session.track_count()
    refused = session.call("clip.import_file", {"path": loop_path, "loop_mode": "sideways"})
    session.check("an unknown loop_mode is refused, typed, and changes nothing",
                  all([refused.get("ok") is False, part(refused, "error").get("kind") == "invalid_args",
                       session.track_count() == before]),
                  "error=%r" % (refused.get("error"),))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    results = []
    transcript = H.Transcript()
    with tempfile.TemporaryDirectory() as workdir, H.start_instance(argv[1]) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript, results)
        run_checks(session, workdir)
        reply = session.call("control.quit")
        session.check("control.quit answers and the instance exits",
                      reply.get("ok") is True and instance.wait_for_exit(30.0) is not None, "ok=%r" % reply.get("ok"))
    transcript.dump()
    print("")
    for name, passed, evidence in results:
        print("%-4s %s\n       %s" % ("ok" if passed else "FAIL", name, evidence))
    failed = [name for name, passed, _ in results if not passed]
    print("\n%d/%d checks held" % (len(results) - len(failed), len(results)))
    if failed:
        return 1
    H.ok("loop import warp control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
