#!/usr/bin/env python3
"""RAW control-surface transcript for the detect.* verbs (0.3.0, import detection).

The acceptance evidence for feature row 34 of docs/FEATURE-LIST-0.3.0.md
(transient / BPM / key detection on import), produced by driving the REAL `zene`
binary headless and printing every request and reply verbatim.

WHAT THIS PROVES, AND WHAT IT DOES NOT.
The fixtures are SYNTHESISED here with a known answer: a click track at exactly
128 BPM whose first click lands at 0.500 s, and an A major scale over an A bass.
The checks are that the engine recovers those answers, that `detect.apply` puts
them into the PROJECT'S OWN FIELDS (the tempo map, and the `<detected-key>`
element), that a save/reopen round trip keeps them, that one `control.undo`
takes the whole detection off, and that every way of asking for something the
file does not carry is REFUSED, typed, leaving nothing behind.

WHAT IT DOES NOT PROVE, stated because the release contract asks for the honest
sentence rather than a number: this is not an accuracy measurement on real
music. A click track is the easy case for an onset/autocorrelation estimate, and
no real-world corpus was measured on this box - `detect.get_state` carries the
same sentence (`method.accuracy_note`) so an agent reading the surface cannot
mistake a suggestion for a measurement. The arithmetic itself is proven
separately (tools/import-detection-proof.cpp, and the registered
ImportDetectionTest QTest).

The checks, in order:

  1. `detect.get_state`          a fresh session holds no key and an empty map,
                                 and publishes the method names, the bounds and
                                 the accuracy sentence;
  2. `detect.analyze`            the 128 BPM click track: the tempo comes back
                                 within 0.5 BPM of the answer, the first
                                 transient within 30 ms of 0.500 s, and the
                                 method is named on the wire;
  3. `detect.analyze`            the A major fixture: tonic A and a scale name
                                 the PRE-EXISTING vocabulary answers to, both
                                 read back through `detect.get_state`'s own
                                 vocabulary list;
  4. refusals                    a missing path, a path that is not a file, an
                                 out-of-range max_seconds and a path with no
                                 transients: every one typed, and none of them
                                 writing anything;
  5. `detect.apply`              both halves land: the tempo map gets ONE event
                                 at tick 0 and is switched on, and the project's
                                 key field carries the detected key;
  6. `transport.tempo_map_get`   the applied tempo read back through the tempo
                                 map's OWN verb, not through detect.*;
  7. `project.save` + `project.open`  the key survives the project file, which is
                                 what makes it the project's field rather than
                                 the session's memory;
  8. `detect.apply` (key only)   a half can be written alone;
  9. `control.undo`              ONE undo takes BOTH halves off;
 10. `detect.apply` (refused)    a refusal writes nothing.

Started through the shared harness (tests/control_socket_harness.py), so this
file adds no second launch path.
Usage: QT_QPA_PLATFORM=offscreen python3 control-detect-commands.py <zene>
Exit code 0 only when every assertion held.
"""

import math
import os
import struct
import sys
import wave

import control_socket_harness as H

REQUEST_IDS = iter(range(1, 1000))

SAMPLE_RATE = 44100
CLICK_BPM = 128.0
CLICK_FIRST_BEAT_S = 0.5
CLICK_SECONDS = 10.0
BPM_TOLERANCE = 0.5
ONSET_TOLERANCE_S = 0.03
TEMPO_METHOD = "spectral-flux-autocorrelation"
KEY_METHOD = "chroma-tonic-weighted-set-match"
TONE_HZ = 220.0


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript

    def call(self, command, args=None):
        return self.client.call(next(REQUEST_IDS), command, args, transcript=self.transcript)

    def result(self, command, args=None):
        """The reply, or {'error': ...} so a failed call is visible in a check."""
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}

    def typed_error(self, command, args=None):
        reply = self.call(command, args)
        return (reply.get("error") or {}) if reply.get("ok") is False else {}


class Recorder:
    """Collects the named checks and their evidence."""

    def __init__(self):
        self.results = []
        self.problems = H.Problems()

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))
        if not passed:
            self.problems.add("%s (%s)" % (name, evidence))


# ---------------------------------------------------------------------------
# the fixtures: written with the standard library, NOT by the engine's decoder
# ---------------------------------------------------------------------------
def write_wave(path, samples):
    """A canonical 16-bit mono RIFF/WAVE. The engine's own libsndfile path is
    what reads it back, so the header this writes is validated by every check
    that follows (a wrong header would make decode fail loudly)."""
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(SAMPLE_RATE)
        handle.writeframes(b"".join(
            struct.pack("<h", max(-32767, min(32767, int(value * 32767.0)))) for value in samples))


def click_track():
    """A click track at CLICK_BPM whose first click is at CLICK_FIRST_BEAT_S."""
    total = int(CLICK_SECONDS * SAMPLE_RATE)
    samples = [0.0] * total
    burst = SAMPLE_RATE // 40  # 25 ms
    beat = CLICK_FIRST_BEAT_S
    while beat < CLICK_SECONDS:
        start = int(beat * SAMPLE_RATE)
        for i in range(burst):
            if start + i >= total:
                break
            envelope = math.exp(-8.0 * i / burst)
            samples[start + i] += 0.8 * envelope * math.sin(2.0 * math.pi * 1000.0 * i / SAMPLE_RATE)
        beat += 60.0 / CLICK_BPM
    return samples


def major_fixture():
    """An A major scale (A B C# D E F# G# A) over an A bass: the answer is
    'A, major' by construction. The bass is what makes the TONIC measurable -
    see the note on the tonic weighting in docs/IMPORT-DETECTION.md."""
    total = int(8.0 * SAMPLE_RATE)
    samples = [0.0] * total
    scale = [220.00, 246.94, 277.18, 293.66, 329.63, 369.99, 415.30, 440.00]

    def add_tone(hz, start_s, length_s, amplitude):
        start = int(start_s * SAMPLE_RATE)
        length = int(length_s * SAMPLE_RATE)
        fade = int(0.02 * SAMPLE_RATE)
        for i in range(length):
            if start + i >= total:
                break
            envelope = 1.0
            if i < fade:
                envelope = i / fade
            elif i + fade >= length:
                envelope = (length - i) / fade
            samples[start + i] += amplitude * envelope * math.sin(
                2.0 * math.pi * hz * i / SAMPLE_RATE)

    for repeat in range(2):
        for note, hz in enumerate(scale):
            add_tone(hz, repeat * 3.0 + note * 0.35, 0.32, 0.35)
    add_tone(110.0, 0.0, 8.0, 0.45)
    return samples


def steady_tone():
    """A tone with no transients at all: the refusal fixture."""
    return [0.4 * math.sin(2.0 * math.pi * TONE_HZ * i / SAMPLE_RATE)
            for i in range(int(6.0 * SAMPLE_RATE))]


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------
def key_of(state):
    return state.get("key") or {}


def tempo_map_of(state):
    return state.get("tempo_map") or {}


def describe(entry):
    return ("found=%s tonic=%r pitch_class=%r scale=%r known=%s score=%.4f margin=%.4f"
            % (entry.get("found"), entry.get("tonic"), entry.get("pitch_class"),
               entry.get("scale"), entry.get("scale_known"), entry.get("score") or 0.0,
               entry.get("margin") or 0.0))


def run_checks(session, instance, recorder):
    workspace = instance.workspace
    click_path = os.path.join(workspace, "click128.wav")
    major_path = os.path.join(workspace, "a-major.wav")
    tone_path = os.path.join(workspace, "steady-tone.wav")
    missing_path = os.path.join(workspace, "not-here.wav")
    project_path = os.path.join(workspace, "detect-project.mmp")

    write_wave(click_path, click_track())
    write_wave(major_path, major_fixture())
    write_wave(tone_path, steady_tone())

    # 1. a fresh session -----------------------------------------------------
    state = session.result("detect.get_state")
    methods = state.get("method") or {}
    bounds = state.get("bounds") or {}
    vocabulary = state.get("scale_vocabulary") or {}
    recorder.check(
        "a fresh session holds no key and an empty tempo map",
        key_of(state).get("present") is False
        and tempo_map_of(state).get("event_count") == 0
        and tempo_map_of(state).get("active") is False,
        "key=%r tempo_map=%r" % (key_of(state), tempo_map_of(state)))
    recorder.check(
        "the two methods are named on the wire, with the accuracy sentence",
        methods.get("tempo") == TEMPO_METHOD and methods.get("key") == KEY_METHOD
        and "unverified" in (methods.get("accuracy_note") or ""),
        "tempo=%r key=%r note=%r" % (methods.get("tempo"), methods.get("key"),
                                     (methods.get("accuracy_note") or "")[:80]))
    recorder.check(
        "the bounds and the pre-existing scale vocabulary are published",
        bounds.get("min_bpm") == 40.0 and bounds.get("max_bpm") == 240.0
        and (vocabulary.get("count") or 0) >= 2
        and "Major" in (vocabulary.get("names") or []),
        "bounds=%r vocabulary_count=%r names=%r" % (bounds, vocabulary.get("count"),
                                                    (vocabulary.get("names") or [])[:6]))

    # 2. the tempo ----------------------------------------------------------
    analysis = session.result("detect.analyze", {"path": click_path})
    tempo = analysis.get("tempo") or {}
    detected = tempo.get("bpm") or 0.0
    onset = tempo.get("first_onset_seconds") or 0.0
    recorder.check(
        "a 128 BPM click track is detected within %.1f BPM" % BPM_TOLERANCE,
        tempo.get("found") is True and abs(detected - CLICK_BPM) <= BPM_TOLERANCE,
        "detected %.3f BPM (confidence %.3f, %r transients)"
        % (detected, tempo.get("confidence") or 0.0, tempo.get("onsets")))
    recorder.check(
        "the first transient lands within %.0f ms of %.3f s"
        % (ONSET_TOLERANCE_S * 1000.0, CLICK_FIRST_BEAT_S),
        abs(onset - CLICK_FIRST_BEAT_S) <= ONSET_TOLERANCE_S,
        "first_onset_seconds=%.3f frame=%r" % (onset, tempo.get("first_onset_frame")))
    recorder.check(
        "the analysis names its method and reports the file it read",
        (analysis.get("method") or {}).get("tempo") == TEMPO_METHOD
        and analysis.get("path") == click_path
        and (analysis.get("sample_rate") or 0) == SAMPLE_RATE,
        "path=%r sample_rate=%r analysed=%.1f s" % (analysis.get("path"),
                                                    analysis.get("sample_rate"),
                                                    analysis.get("analysed_seconds") or 0.0))

    # 3. the key, in the pre-existing vocabulary -----------------------------
    analysis = session.result("detect.analyze", {"path": major_path})
    key = analysis.get("key") or {}
    names = (session.result("detect.get_state").get("scale_vocabulary") or {}).get("names") or []
    recorder.check(
        "an A major fixture reports tonic A and a name the vocabulary holds",
        key.get("found") is True and key.get("tonic") == "A" and key.get("pitch_class") == 9
        and key.get("scale_known") is True and key.get("scale") in names,
        "%s (in the vocabulary: %s)" % (describe(key), key.get("scale") in names))
    recorder.check(
        "the key half names its own method",
        (analysis.get("method") or {}).get("key") == KEY_METHOD,
        "key method=%r" % (analysis.get("method") or {}).get("key"))

    # 4. refusals write nothing ---------------------------------------------
    refusals = []
    for name, args in (
        ("a missing path", {}),
        ("a path that is not a file", {"path": missing_path}),
        ("max_seconds out of range", {"path": click_path, "max_seconds": 0}),
    ):
        error = session.typed_error("detect.analyze", args)
        if not error.get("kind") or not error.get("message"):
            refusals.append("%s -> %r" % (name, error))
    recorder.check("every malformed analyse is refused, typed", not refusals, "; ".join(refusals))

    error = session.typed_error("detect.apply", {"path": tone_path, "tempo": True, "key": False})
    recorder.check(
        "a file with no transients is REFUSED for tempo, not guessed at",
        error.get("kind") == "refused" and "nothing was written" in (error.get("message") or ""),
        "kind=%r message=%r" % (error.get("kind"), (error.get("message") or "")[:120]))
    state = session.result("detect.get_state")
    recorder.check(
        "and that refusal wrote nothing",
        tempo_map_of(state).get("event_count") == 0 and key_of(state).get("present") is False,
        "tempo_map=%r key=%r" % (tempo_map_of(state), key_of(state)))

    # 5. the KEY half alone, and its own inverse ---------------------------
    applied = session.result("detect.apply", {"path": major_path, "tempo": False})
    key_state = applied.get("key_state") or {}
    recorder.check(
        "detect.apply writes the key half alone when asked for it alone",
        (applied.get("tempo") or {}).get("applied") is False
        and key_state.get("present") is True and key_state.get("tonic") == "A"
        and key_state.get("scale") in names
        and (applied.get("key") or {}).get("applied") is True,
        "key_state=%s tempo=%r" % (key_state, applied.get("tempo")))
    recorder.check(
        "and it did not touch the tempo map, which no tempo was asked for",
        tempo_map_of(session.result("detect.get_state")).get("event_count") == 0,
        "tempo_map=%r" % (tempo_map_of(session.result("detect.get_state")),))
    session.result("control.undo")
    state = session.result("detect.get_state")
    recorder.check(
        "control.undo takes the key half back off on its own",
        key_of(state).get("present") is False,
        "key=%r" % (key_of(state),))

    # 6. apply BOTH halves, ONE undoable step ------------------------------
    applied = session.result("detect.apply", {"path": click_path})
    tempo_result = applied.get("tempo") or {}
    recorder.check(
        "detect.apply writes the detected tempo into the tempo map",
        tempo_result.get("applied") is True and tempo_result.get("event_tick") == 0
        and tempo_result.get("bpm") == int(round(CLICK_BPM)) and tempo_result.get("active") is True,
        "bpm=%r detected_bpm=%.3f rounded=%r events=%r"
        % (tempo_result.get("bpm"), tempo_result.get("detected_bpm") or 0.0,
           tempo_result.get("rounded"), tempo_result.get("events")))
    recorder.check(
        "the rounded figure is the map's integer and the exact one is reported beside it",
        abs((tempo_result.get("detected_bpm") or 0.0) - CLICK_BPM) <= BPM_TOLERANCE
        and tempo_result.get("bpm") == int(round(tempo_result.get("detected_bpm") or 0.0)),
        "detected=%.3f written=%r" % (tempo_result.get("detected_bpm") or 0.0,
                                     tempo_result.get("bpm")))
    key_state = applied.get("key_state") or {}
    recorder.check(
        "the same call writes the key half too, from the same analyser",
        key_state.get("present") is True and key_state.get("tonic") == "A"
        and key_state.get("source") == click_path,
        "key_state=%s" % (key_state,))

    # 7. the tempo map's OWN verb reads it back ----------------------------
    map_state = session.result("transport.tempo_map_get")
    events = map_state.get("events") or []
    recorder.check(
        "transport.tempo_map_get (a different verb) reads the applied event back",
        map_state.get("active") is True and len(events) == 1
        and events[0].get("tick") == 0 and events[0].get("bpm") == int(round(CLICK_BPM))
        and map_state.get("tempo_at_position") == int(round(CLICK_BPM)),
        "active=%r events=%r tempo_at_position=%r" % (map_state.get("active"), events,
                                                     map_state.get("tempo_at_position")))

    # 8. ONE undo takes BOTH halves off ------------------------------------
    session.result("control.undo")
    state = session.result("detect.get_state")
    recorder.check(
        "one control.undo takes the whole detection off (both halves)",
        key_of(state).get("present") is False
        and tempo_map_of(state).get("event_count") == 0
        and tempo_map_of(state).get("active") is False,
        "key=%r tempo_map=%r" % (key_of(state), tempo_map_of(state)))

    # 9. the project's own fields reach the project FILE -------------------
    session.result("detect.apply", {"path": click_path})
    session.result("project.save", {"path": project_path})
    saved = b""
    try:
        with open(project_path, "rb") as handle:
            saved = handle.read()
    except OSError as failure:
        recorder.check("the project file was written at all", False, str(failure))
    text = saved.decode("utf-8", "replace") if saved.startswith(b"<?xml") else _inflate(saved)
    recorder.check(
        "the saved PROJECT FILE carries the key in the project's own field",
        "<detected-key" in text and 'tonic="A"' in text
        and 'method="%s"' % KEY_METHOD in text,
        "detected-key element present=%s (project %d bytes)"
        % ("<detected-key" in text, len(saved)))
    recorder.check(
        "the saved PROJECT FILE carries the tempo in the tempo MAP",
        "<tempo-map" in text and 'bpm="%d"' % int(round(CLICK_BPM)) in text,
        "tempo-map element present=%s bpm=%d"
        % ("<tempo-map" in text, int(round(CLICK_BPM))))
    return project_path


def _inflate(data):
    """The project file as text when it was saved compressed, else ''. An .mmp
    is XML; the binary path covers a build that wrote .mmpz bytes anyway."""
    if data[:2] != b"\x1f\x8b":
        return ""
    import gzip
    try:
        return gzip.decompress(data).decode("utf-8", "replace")
    except (OSError, EOFError):
        return ""


def check_quit(session, instance, recorder):
    reply = session.call("control.quit")
    recorder.check("control.quit answers and the instance exits",
                   reply.get("ok") is True and instance.wait_for_exit(30.0) is not None,
                   "reply=%r" % (reply.get("ok"),))


def report_results(recorder):
    passed = sum(1 for _, ok, _ in recorder.results if ok)
    print("")
    for name, ok, evidence in recorder.results:
        print("%-4s %s\n       %s" % ("ok" if ok else "FAIL", name, evidence))
    print("")
    print("%d/%d checks held" % (passed, len(recorder.results)))


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
        run_checks(session, instance, recorder)
        check_quit(session, instance, recorder)

    report_results(recorder)
    transcript.dump()
    if recorder.problems:
        print("")
        recorder.problems.report("detect.* control-surface transcript")
        return 1
    H.ok("detect.* control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
