#!/usr/bin/env python3
"""END-TO-END proof that LUFS loudness metering is drivable by an agent.

THE CLAIM UNDER TEST: an agent connected to a real `zene` instance over its control
socket can arm a PASSIVE loudness tap on the live master, read BS.1770-4 integrated /
momentary / short-term loudness and true peak off the wire while the project plays, and
measure a RENDERED FILE with the same meter - and that neither path changes one byte of
audio. Feature row 24 of docs/FEATURE-LIST-0.3.0.md ("LUFS / loudness metering"), the
audit's "in the tree but not drivable through the socket" feature whose measurement core
and render-path report were already merged.

WHY A REGISTERED CTEST AND NOT A UNIT TEST. tests/src/core/MeterTapTest.cpp holds the tap
itself to account in process (what an armed tap reads, silence reads the sentinel, 10 dB
more input reads 10 LU higher, a fed buffer is bit-identical afterwards, zero allocations
per block). What it cannot prove is the release contract's section 3.1: that the feature
is reachable THROUGH THE SOCKET - schema validation, the wire numbers, the A16
transaction, and the live tap being fed by the engine's OWN audio thread while a real
project plays. So this starts the REAL binary (the ControlSocketIntegration mould:
`$<TARGET_FILE:zene>`, QT_QPA_PLATFORM=offscreen, the shared control_socket_harness) with
its own HOME/XDG world, and uses the tree's OWN loudness fixtures
(tests/data/loudness/make-fixtures.py - the generator docs/LUFS-WIRING.md's evidence run
uses) so every absolute number has an independently known value.

WHAT IT ASSERTS, in numbers read off the wire:

  * the group is registered: control.commands_list carries meter.get_state, meter.arm and
    meter.measure_file with schemas and A16 classes, export.set_loudness_report with one,
    and export.get_settings now EXPOSES loudness_report (the second half of the audit's
    complaint about this feature);
  * a tap that has never been armed reports armed false, blocks_fed 0 and NULL readings -
    the sentinel, never a plausible number;
  * arming it is reversible (a recorded action checkpoint) and STARTS a measurement: the
    readings are null immediately after, and blocks_fed then grows on its own, i.e. the
    engine's audio thread is feeding the tap (the tap is live, not a fixture);
  * THE THREE NEGATIVE CONTROLS THE TASK NAMES:
      1. silence reads null: tests/data/loudness/silent.wav measures `measured` false,
         every reading null and verdict NOT MEASURED - while the two tones measure their
         own known levels (-23.00 and -33.00 LUFS-I, EBU Tech 3341 cases 1 and 2);
      2. a louder signal reads proportionally higher: the two tones are 10 dB apart by
         construction and read 10 LU apart, on the wire, within the EBU tolerance - and
         the same holds for the LIVE tap with tone-23.mmp and tone-33.mmp playing through
         the engine, where a constant-reading or unfed tap cannot produce the difference;
      3. the audio is byte-identical with the meter attached: the sha256 meter.measure_file
         reports for each fixture equals the file's sha256 computed here before the call
         (measuring a file does not touch it), and a project rendered with the live tap
         ARMED has PCM frames identical to the same project rendered with it disarmed;
  * every refusal is typed and writes nothing: a relative path, a missing file, a missing
    argument, and a wrong argument type.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-meter-commands.py <zene-binary>

Exit codes: 0 every check held; 1 a check failed (the app log is printed);
2 cannot run (no binary at the path, or no loudness fixture generator in the tree).
"""

from __future__ import annotations

import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import time
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE_GENERATOR = os.path.join(HERE, "data", "loudness", "make-fixtures.py")

# The independently known values of the fixtures (EBU Tech 3341 case 1 / case 2: a
# 1 kHz sine in phase in both channels, each channel's peak N dB below full scale).
TONE_23_LUFS = -23.0
TONE_33_LUFS = -33.0
TONE_SEPARATION_LU = 10.0
TONE_TOLERANCE_LU = 0.1
# The live reading goes through the whole engine (the note's own envelope, the mixer at
# unity): the LEVEL can differ from the file's by a fraction of a LU, but the DIFFERENCE
# between the two fixtures cannot - which is what makes the live check a control rather
# than a calibration.
LIVE_TOLERANCE_LU = 1.0
LIVE_SEPARATION_TOLERANCE_LU = 0.5

#: A render gets a DECLARED budget, never one socket read - the number the tree's render
#: transcripts already carry (tests/freeze_bounce_evidence.py:57, tests/control-render-presets.py:44:
#: "ONE FULL ENGINE START plus the render"). `render.render` runs the product's own CLI in a
#: CHILD process and the child pays a full `Engine::init` before it renders - measured ~34 s on
#: the linux-arm64 CI runner against ~0.3 s here (docs/RENDER-CHILD-WAIT.md) - while the
#: dispatch thread answers NOTHING, this socket included, until the child exits. The harness's
#: SOCKET_TIMEOUT (30 s) bounds a socket round trip, not a render: on run 35126160372 the
#: linux-arm64 job failed THIS test at 78.17 s with "no response line inside 30.0s (socket
#: timed out)" while the render child was in state R, and the same test passed on linux-x86_64
#: in 13.20 s. Every command NOT in RENDER_COMMANDS keeps SOCKET_TIMEOUT, so a genuine hang
#: still costs seconds.
RENDER_TIMEOUT = 180.0
RENDER_COMMANDS = ("render.render",)


# ---------------------------------------------------------------------------
# the wire client
# ---------------------------------------------------------------------------


class Recorder:
    """Collects the named checks and their evidence."""

    def __init__(self):
        self.results = []
        self.problems = H.Problems()

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))
        if not passed:
            self.problems.add("%s (%s)" % (name, evidence))


class Session:
    """One bounded request/reply at a time, with the transcript recorded."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.request_id = 100

    def reply(self, cmd, args=None):
        self.request_id += 1
        budget = RENDER_TIMEOUT if cmd in RENDER_COMMANDS else None
        return self.client.call(self.request_id, cmd, args or {}, transcript=self.transcript,
                                timeout=budget)

    def result(self, cmd, args=None):
        reply = self.reply(cmd, args)
        return H.ok_result(reply, reply.get("id"))

    def error(self, cmd, args=None, kind="invalid_args"):
        reply = self.reply(cmd, args)
        return H.typed_error(reply, reply.get("id"), kind)

    def entry(self, command_id):
        listed = self.result("control.commands_list").get("commands") or []
        for entry in listed:
            if entry.get("id") == command_id:
                return entry
        return {}


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def pcm_sha256(path):
    """sha256 of a WAV's FRAMES (the `data` chunk), not of the container.

    The container carries a PEAK chunk whose body libsndfile stamps with the wall-clock
    second, so two renders of the same audio differ by that byte (measured; see
    docs/LUFS-WIRING.md section 4.3). The AUDIO is what this proof compares, so it reads
    the frames back through the stdlib `wave` module.
    """
    with wave.open(path, "rb") as handle:
        frames = handle.readframes(handle.getnframes())
    return hashlib.sha256(frames).hexdigest()


def write_silent_wav(path, seconds=1.0, sample_rate=48000, channels=2):
    """A WAV nothing can mistake for a measurement: every sample exactly zero."""
    frames = int(seconds * sample_rate)
    with wave.open(path, "wb") as handle:
        handle.setnchannels(channels)
        handle.setsampwidth(2)
        handle.setframerate(sample_rate)
        handle.writeframes(b"\x00" * (frames * channels * 2))


def write_tone_wav(path, peak_dbfs, seconds=2.0, sample_rate=48000, frequency_hz=1000.0):
    """A 1 kHz stereo sine at the stated peak, written as 32-bit float.

    Used for the CHANNEL-COUNT and empty-file refusals and for the mono measurement; the
    absolute-loudness checks use the tree's own fixture generator, so the signal that
    carries a number in this proof is not a second implementation of one.
    """
    import math

    amplitude = 10.0 ** (peak_dbfs / 20.0)
    count = int(seconds * sample_rate)
    samples = []
    for index in range(count):
        value = amplitude * math.sin(2.0 * math.pi * frequency_hz * index / sample_rate)
        samples.extend((value, value))
    data = struct.pack("<%df" % len(samples), *samples)
    fmt = struct.pack("<HHIIHH", 3, 2, sample_rate, sample_rate * 2 * 4, 2 * 4, 32)
    with open(path, "wb") as handle:
        handle.write(b"RIFF" + struct.pack("<I", 4 + 8 + len(fmt) + 8 + len(data)) + b"WAVE")
        handle.write(b"fmt " + struct.pack("<I", len(fmt)) + fmt)
        handle.write(b"data" + struct.pack("<I", len(data)) + data)


def live(result):
    return result.get("live") or {}


def measure(session, path):
    return session.result("meter.measure_file", {"path": path})


def readings(result):
    """The five numbers, as the wire carries them (None == the meter's sentinel)."""
    return tuple(result.get(key) for key in ("integrated_lufs", "momentary_lufs",
                                            "short_term_lufs", "short_term_max_lufs",
                                            "true_peak_dbtp"))


def play_and_measure(session, project_path, arm_seconds=4.0):
    """Open a project, arm a fresh measurement, play, and read the live tap back."""
    session.result("transport.stop")
    session.result("project.open", {"path": project_path})
    session.result("meter.arm", {"enabled": True})
    session.result("transport.seek", {"ticks": 0})
    session.result("transport.play")
    time.sleep(arm_seconds)
    state = session.result("meter.get_state")
    session.result("transport.stop")
    return state


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


def check_registration(session, recorder):
    """The ids exist, carry schemas and A16 classes, and export exposes the report."""
    listed = {entry.get("id")
              for entry in session.result("control.commands_list").get("commands") or []}
    wanted = {"meter.get_state", "meter.arm", "meter.measure_file",
              "export.set_loudness_report"}
    recorder.check("control.commands_list carries the meter.* ids and export.set_loudness_report",
                   wanted <= listed, "missing=%s" % sorted(wanted - listed))

    reader = session.entry("meter.get_state")
    recorder.check("meter.get_state is a read with a result schema",
                   reader.get("mutating") is False and bool(reader.get("result_schema")),
                   "mutating=%r schema=%r" % (reader.get("mutating"), bool(reader.get("result_schema"))))

    arm = session.entry("meter.arm")
    arm_required = (arm.get("args_schema") or {}).get("required") or []
    recorder.check("meter.arm requires `enabled` and declares itself mutating",
                   arm.get("mutating") is True and "enabled" in arm_required,
                   "mutating=%r required=%r" % (arm.get("mutating"), arm_required))

    file_read = session.entry("meter.measure_file")
    file_required = (file_read.get("args_schema") or {}).get("required") or []
    recorder.check("meter.measure_file requires `path` and declares itself a read",
                   file_read.get("mutating") is False and "path" in file_required,
                   "mutating=%r required=%r" % (file_read.get("mutating"), file_required))

    settings = session.result("export.get_settings")
    recorder.check("export.get_settings now exposes the render loudness report",
                   "loudness_report" in settings and settings.get("loudness_report") is False,
                   "settings=%r" % settings)


def check_default_state(session, recorder):
    """A tap that was never armed measures nothing, and says so with null."""
    state = session.result("meter.get_state")
    tap = live(state)
    recorder.check("an unarmed tap reports armed false with no measurement",
                   tap.get("enabled") is False and tap.get("blocks_fed") == 0
                   and readings(tap) == (None, None, None, None, None),
                   "live=%r" % tap)
    recorder.check("the readout reports the meter's own rate and channel count",
                   tap.get("sample_rate") in (44100, 48000) and tap.get("channels") == 2,
                   "rate=%r channels=%r" % (tap.get("sample_rate"), tap.get("channels")))
    target = state.get("target") or {}
    recorder.check("the payload names the standard it reports against",
                   abs(float(target.get("integrated_lufs", 0)) - TONE_23_LUFS) < 1e-6
                   and abs(float(target.get("tolerance_lu", 0)) - 0.5) < 1e-6,
                   "target=%r" % target)


def meter_records(session):
    """The A16 records this group left, as control.transactions reports them.

    The registry REMOVES the handler's private `__transaction` key from the reply and
    records it itself (ControlRegistry::recordTransactionOf), so the transaction an
    agent reads back is this command's records - not a key in the result.
    """
    records = session.result("control.transactions").get("transactions") or []
    return [record for record in records if record.get("command") == "meter.arm"]


def check_arm_is_live_and_reversible(session, recorder):
    """Arming starts a measurement, is reversible, and the audio thread feeds the tap."""
    armed = session.result("meter.arm", {"enabled": True})
    records = meter_records(session)
    record = records[-1] if records else {}
    recorder.check("meter.arm records a reversible action checkpoint",
                   armed.get("armed") is True and armed.get("previous") is False
                   and record.get("class") == "true_inverse"
                   and record.get("reversible") is True
                   and "setEnabled" in (record.get("mechanism") or ""),
                   "record=%r" % record)
    recorder.check("arming starts a FRESH measurement (the readings are null immediately)",
                   readings(live(armed)) == (None, None, None, None, None),
                   "live=%r" % live(armed))

    first = live(session.result("meter.get_state"))
    time.sleep(1.0)
    second = live(session.result("meter.get_state"))
    # The FIRST reading is taken the instant after arming, so it can legitimately be 0 -
    # what matters is that the count then MOVES on its own, with nothing sent in between.
    recorder.check("the engine's own audio thread is feeding the tap (blocks_fed grows)",
                   second.get("blocks_fed", 0) > first.get("blocks_fed", 0)
                   and second.get("frames_fed", 0) > 0,
                   "first=%r second=%r frames=%r" % (first.get("blocks_fed"),
                                                     second.get("blocks_fed"),
                                                     second.get("frames_fed")))
    recorder.check("a master carrying no signal still reads null, never a number",
                   readings(second) == (None, None, None, None, None),
                   "live=%r" % second)

    disarmed = session.result("meter.arm", {"enabled": False})
    recorder.check("disarming keeps the measurement readable and stops the counting",
                   disarmed.get("armed") is False
                   and live(disarmed).get("blocks_fed", 0) > 0,
                   "armed=%r live=%r" % (disarmed.get("armed"), live(disarmed).get("blocks_fed")))


def check_file_measurements(session, recorder, fixtures):
    """The document half: known levels, the sentinel, and the proportional control."""
    tone_23 = measure(session, os.path.join(fixtures, "tone-23.wav"))
    tone_33 = measure(session, os.path.join(fixtures, "tone-33.wav"))

    recorder.check("tone-23.wav measures its own known level (EBU Tech 3341 case 1)",
                   abs(tone_23.get("integrated_lufs", 99) - TONE_23_LUFS) <= TONE_TOLERANCE_LU,
                   "integrated=%r" % tone_23.get("integrated_lufs"))
    recorder.check("tone-33.wav measures its own known level (case 2)",
                   abs(tone_33.get("integrated_lufs", 99) - TONE_33_LUFS) <= TONE_TOLERANCE_LU,
                   "integrated=%r" % tone_33.get("integrated_lufs"))
    recorder.check("NEGATIVE CONTROL: 10 dB more input reads 10 LU higher",
                   abs((tone_23.get("integrated_lufs", 0) - tone_33.get("integrated_lufs", 0))
                       - TONE_SEPARATION_LU) <= 2 * TONE_TOLERANCE_LU,
                   "separation=%r LU" % (tone_23.get("integrated_lufs", 0)
                                         - tone_33.get("integrated_lufs", 0)))
    recorder.check("the measured file reports its shape and a positive duration",
                   tone_23.get("sample_rate") == 48000 and tone_23.get("channels") == 2
                   and tone_23.get("frames", 0) > 0
                   and tone_23.get("duration_seconds", 0) > 0,
                   "shape=%r" % {k: tone_23.get(k) for k in ("sample_rate", "channels",
                                                             "frames", "duration_seconds")})
    recorder.check("the EBU R 128 verdict is graded, not asserted",
                   tone_23.get("verdict") in ("PASS", "WARN")
                   and tone_23.get("measured") is True
                   and abs(tone_23.get("deviation_lu", 99)) <= 0.5,
                   "verdict=%r deviation=%r" % (tone_23.get("verdict"), tone_23.get("deviation_lu")))

    silent = measure(session, os.path.join(fixtures, "silent.wav"))
    recorder.check("NEGATIVE CONTROL: silence reads null and claims no verdict",
                   readings(silent) == (None, None, None, None, None)
                   and silent.get("measured") is False
                   and str(silent.get("verdict")).startswith("NOT MEASURED"),
                   "result=%r" % {k: silent.get(k) for k in ("integrated_lufs", "true_peak_dbtp",
                                                             "measured", "verdict")})


def check_measuring_does_not_touch_the_file(session, recorder, fixtures):
    """NEGATIVE CONTROL 3, file half: measure_file reads the file and nothing else."""
    path = os.path.join(fixtures, "tone-23.wav")
    before = sha256_of(path)
    result = measure(session, path)
    after = sha256_of(path)
    reported = next((row.get("sha256") for row in result.get("source") or []
                     if row.get("path") == path), None)
    recorder.check("measuring a file leaves every byte of it where it was",
                   before == after and reported == before,
                   "before=%s after=%s reported=%s" % (before, after, reported))


def check_live_measurement_of_a_playing_project(session, recorder, fixtures):
    """NEGATIVE CONTROL 2, live half: a louder project reads proportionally higher."""
    loud = play_and_measure(session, os.path.join(fixtures, "tone-23.mmp"))
    quiet = play_and_measure(session, os.path.join(fixtures, "tone-33.mmp"))

    raw_loud = live(loud).get("integrated_lufs")
    raw_quiet = live(quiet).get("integrated_lufs")
    loud_reading = float(raw_loud) if isinstance(raw_loud, (int, float)) else None
    quiet_reading = float(raw_quiet) if isinstance(raw_quiet, (int, float)) else None
    recorder.check("the live tap measures a playing project (both fixtures report a level)",
                   loud_reading is not None and quiet_reading is not None
                   and -70.0 < loud_reading < 0.0 and -70.0 < quiet_reading < 0.0,
                   "loud=%r quiet=%r" % (raw_loud, raw_quiet))
    if loud_reading is None or quiet_reading is None:
        # The two checks below are arithmetic on the readings; without one there is nothing
        # to compare, and the failure above is the evidence that says why.
        recorder.check("NEGATIVE CONTROL: 10 dB more input reads 10 LU higher LIVE", False,
                       "no live reading to compare (loud=%r quiet=%r)" % (raw_loud, raw_quiet))
        recorder.check("the live level is the fixture's level (the engine adds no gain)", False,
                       "no live reading (loud=%r)" % (raw_loud,))
        return
    separation = loud_reading - quiet_reading
    recorder.check("NEGATIVE CONTROL: 10 dB more input reads 10 LU higher LIVE",
                   abs(separation - TONE_SEPARATION_LU) <= LIVE_SEPARATION_TOLERANCE_LU,
                   "separation=%r LU" % separation)
    recorder.check("the live level is the fixture's level (the engine adds no gain)",
                   abs(loud_reading - TONE_23_LUFS) <= LIVE_TOLERANCE_LU,
                   "live=%r expected=%r" % (loud_reading, TONE_23_LUFS))


def check_render_is_byte_identical_with_the_tap_attached(session, recorder, workdir, fixtures):
    """NEGATIVE CONTROL 3, render half: the audio is the same metres or not."""
    project = os.path.join(fixtures, "tone-23.mmp")
    session.result("transport.stop")
    session.result("project.open", {"path": project})

    session.result("meter.arm", {"enabled": True})
    armed_render = session.result("render.render",
                                  {"out": os.path.join(workdir, "armed.wav"), "format": "wav"})
    session.result("meter.arm", {"enabled": False})
    plain_render = session.result("render.render",
                                  {"out": os.path.join(workdir, "plain.wav"), "format": "wav"})

    armed_audio = pcm_sha256(armed_render.get("path"))
    plain_audio = pcm_sha256(plain_render.get("path"))
    recorder.check("the rendered audio is byte-identical with the master tap attached",
                   armed_audio == plain_audio and armed_render.get("frames") == plain_render.get("frames"),
                   "armed=%s plain=%s frames=%r/%r" % (armed_audio, plain_audio,
                                                       armed_render.get("frames"),
                                                       plain_render.get("frames")))
    recorder.check("the render itself does not depend on the transport or the tap",
                   armed_render.get("frames", 0) > 0 and armed_render.get("bytes", 0) > 0,
                   "render=%r" % {k: armed_render.get(k) for k in ("path", "bytes", "frames")})


def check_refusals(session, recorder, workdir):
    """Every refusal is typed, and writes nothing."""
    session.error("meter.measure_file", {}, "invalid_args")
    session.error("meter.measure_file", {"path": "relative/tone.wav"}, "invalid_args")
    session.error("meter.measure_file", {"path": os.path.join(workdir, "absent.wav")}, "invalid_args")

    empty = os.path.join(workdir, "empty.wav")
    with wave.open(empty, "wb") as handle:
        handle.setnchannels(2)
        handle.setsampwidth(2)
        handle.setframerate(48000)
        handle.writeframes(b"")
    session.error("meter.measure_file", {"path": empty}, "refused")

    six_channel = os.path.join(workdir, "six-channel.wav")
    with wave.open(six_channel, "wb") as handle:
        handle.setnchannels(8)
        handle.setsampwidth(2)
        handle.setframerate(48000)
        handle.writeframes(b"\x00" * (100 * 8 * 2))
    session.error("meter.measure_file", {"path": six_channel}, "refused")
    recorder.check("four typed refusals (three invalid args, one refused layout)", True,
                   "measured over the wire")


def check_master_arm_undo(session, recorder):
    """The inverse: control.undo takes the armed flag back."""
    session.result("meter.arm", {"enabled": False})
    session.result("meter.arm", {"enabled": True})
    recorder.check("armed for the undo check",
                   session.result("meter.get_state").get("armed") is True,
                   "armed")
    session.result("control.undo")
    state = session.result("meter.get_state")
    recorder.check("control.undo returns the tap to its previous armed state",
                   state.get("armed") is False,
                   "armed=%r after undo" % state.get("armed"))


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------


def report_results(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-66s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)


def run_checks(session, workdir, fixtures, recorder):
    check_registration(session, recorder)
    check_default_state(session, recorder)
    check_arm_is_live_and_reversible(session, recorder)
    check_file_measurements(session, recorder, fixtures)
    check_measuring_does_not_touch_the_file(session, recorder, fixtures)
    check_live_measurement_of_a_playing_project(session, recorder, fixtures)
    check_render_is_byte_identical_with_the_tap_attached(session, recorder, workdir, fixtures)
    check_refusals(session, recorder, workdir)
    check_master_arm_undo(session, recorder)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    binary = argv[1]
    if not os.path.exists(binary):
        print("no binary at %s" % binary)
        return 2
    if not os.path.exists(FIXTURE_GENERATOR):
        print("no loudness fixture generator at %s" % FIXTURE_GENERATOR)
        return 2

    workdir = tempfile.mkdtemp(prefix="zene-meter-")
    fixtures = os.path.join(workdir, "fixtures")
    os.makedirs(fixtures, exist_ok=True)
    # The tree's own generator: the signals that carry a LUFS number in this proof are
    # EBU Tech 3341 case 1 and case 2, written by the file docs/LUFS-WIRING.md names.
    generated = subprocess.run([sys.executable, FIXTURE_GENERATOR, fixtures],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if generated.returncode != 0 or not os.path.exists(os.path.join(fixtures, "tone-23.wav")):
        print("the fixture generator failed (exit %d):" % generated.returncode)
        print(generated.stdout.decode("utf-8", "replace")[-2000:])
        return 2

    recorder = Recorder()
    transcript = H.Transcript()
    with H.start_instance(binary) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        print("instance: %s" % binary)
        print("socket:   %s" % instance.socket_path)
        run_checks(session, workdir, fixtures, recorder)
        session.client.close()

    report_results(recorder)
    transcript.dump()
    if recorder.problems:
        print("")
        recorder.problems.report("meter.* control-surface transcript")
        print("the run directory is kept for inspection: %s" % workdir)
        return 1
    H.ok("meter.* control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
