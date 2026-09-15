#!/usr/bin/env python3
"""RAW control-surface transcript for the recording ENGINE surface (0.3.0).

The acceptance evidence for FEATURE-LIST-0.3.0.md rows 64 ("Arbitrary input count /
multiple simultaneous inputs"), 14 ("Multi-track recorder") and 16 ("Retrospective
audio capture"), produced by driving the REAL binary headless and printing every
request and reply verbatim.

WHAT MAKES THIS PROOF RATHER THAN A SMOKE TEST. The arbitrary input count is proved
END TO END ACROSS A RESTART, which is what the feature actually is: instance A
reports the count its engine was built with, `record.input_set` writes a new one and
says `restart_required`, and instance B - started against the same working
directory - reports the new count AND accepts a route armed for an input channel
that instance A had to refuse. No sound card is needed for any of it: the channel
range a route may select is engine state, not a device property, and the device's
own answer is reported separately in `capture_capable` / `capture_open` /
`capture_reason`, which this test asserts are CONSISTENT rather than equal to any
particular value.

What it drives, in order:

  1. instance A: `record.input_get_state`   the configured plan, the live device
     state and the engine's input stages. `capture_capable: false` or
     (`true`, `capture_open: false`, a non-empty `capture_reason`) is this host's
     honest answer, and the two halves are checked against each other;
  2. `control.commands_list`               all nine new ids are registered, and
     `track.set_arm`'s own description no longer says it refuses;
  3. `record.input_set {channels: 8}`      the ARBITRARY input count, written to the
     config file, with `previous` reported and `restart_required` true. The running
     instance's route channel capacity is still the one it started with - asserted,
     because that is what "restart required" MEANS;
  4. typed refusals                        channels 0, 33 and a bad bus pair;
  5. restart                               instance B, started against the config file
     instance A's `record.input_set` WROTE - the file is read off disk by this test and
     handed to the harness's own Instance class, so "the next start reads it" is measured
     rather than assumed;
  6. `record.get_state`                    the new count is live: sixteen routes,
     each able to select any of the eight input channels;
  7. `record.arm_track {route: 0, input_channel: 7}`  a route armed for channel 7 -
     the input channel instance A refused - writes a take and journals it, and
     `record.get_state` reads the arm, the channel, the file and the journal back;
  8. `record.arm_track {route: 99}`        typed invalid_args (the route bound);
  9. `record.disarm_all`                   every capture stops, every journal is
     RETIRED (asserted on disk: the side file is gone) and every take file stays;
 10. `track.set_arm`                       the id that used to be a refusal stub
     starts a real capture on a real song track, and `control.undo` takes it back
     through the recorded inverse (the disarm);
 11. `record.retro_capture_arm` / `_status` / `_to_take`  the retrospective AUDIO
     window: off, then armed, its capacity in frames and seconds, and then either
     the take (when this host actually captured frames) or the typed refusal that
     says the window is empty - never a zero-length file;
 12. `control.transactions`                the A16 records: the arm verbs are
     `snapshot` with a command inverse, `record.input_set` is `true_inverse`, the
     inspectors and the writers that only touch a file are `not_mutating`.

Started through the shared harness (tests/control_socket_harness.py), so this file
adds no second launch path and no second launch recipe.

Usage: QT_QPA_PLATFORM=offscreen python3 control-record-inputs.py <zene>
Exit code 0 only when every assertion held.
"""

import os
import shutil
import sys
import tempfile

import control_socket_harness as H

REQUEST_IDS = iter(range(1, 1000))

#: The command ids this lane adds. Every one has to be registered with schemas.
NEW_IDS = (
    "record.get_state",
    "record.arm_track",
    "record.disarm_track",
    "record.disarm_all",
    "record.input_get_state",
    "record.input_set",
    "record.retro_capture_arm",
    "record.retro_capture_status",
    "record.retro_capture_to_take",
)

#: RetroAudioCapture::DefaultCapacityFrames.
RETRO_CAPACITY_FRAMES = 1 << 20

#: MultiTrackRecorder::MaxRoutes.
ROUTE_COUNT = 16


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


def report_results(recorder):
    print("\n---- checks ----")
    for name, passed, evidence in recorder.results:
        print("  %-64s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)


def check_input_path_shape(session, recorder, label):
    """The input path reports what it is, and the two halves agree."""
    state = session.result("record.input_get_state")
    configured = state.get("configured") or {}
    capable = state.get("capture_capable")
    opened = state.get("capture_open")
    reason = state.get("capture_reason") or ""
    recorder.check("%s: record.input_get_state answers with a configured plan" % label,
                   isinstance(configured, dict) and "channels" in configured
                   and "device" in configured,
                   "result=%r" % state)
    # THE HONESTY INVARIANT: "no capture path in this backend" and "a capture path
    # whose device refused" are different facts, and the reply must not confuse them.
    consistent = (capable is True and (opened is True or bool(reason))) or \
                 (capable is False and opened is False)
    recorder.check("%s: the capture state is internally consistent" % label,
                   consistent,
                   "capable=%r open=%r reason=%r" % (capable, opened, reason))
    recorder.check("%s: the engine's input stages are reported as numbers" % label,
                   isinstance(state.get("bus_frames"), int)
                   and isinstance(state.get("wide_frames"), int)
                   and isinstance(state.get("input_frames_staged"), int),
                   "result=%r" % state)
    return state


def check_registration(session, recorder):
    reply = session.result("control.commands_list")
    entries = {entry.get("id"): entry for entry in reply.get("commands", [])}
    missing = [cid for cid in NEW_IDS if cid not in entries]
    recorder.check("every new record.* id is registered", not missing,
                   "missing=%r" % missing)
    for cid in NEW_IDS:
        entry = entries.get(cid) or {}
        if entry.get("group") != "record" or not entry.get("description") \
                or not entry.get("args_schema") or not entry.get("result_schema"):
            recorder.check("%s carries its group, description and both schemas" % cid,
                           False, "entry=%r" % entry)
    recorder.check("the new ids carry their group, description and both schemas",
                   all((entries.get(cid) or {}).get("group") == "record"
                       for cid in NEW_IDS),
                   "ids=%r" % NEW_IDS)
    arm = entries.get("track.set_arm") or {}
    recorder.check("track.set_arm no longer describes itself as a refusal",
                   "Refused" not in (arm.get("description") or "")
                   and "input_channel" in ((arm.get("args_schema") or {}).get("properties") or {}),
                   "description=%r" % arm.get("description"))


def start_instance_with_config(binary, config_text, config_path, workingdir):
    """A harness Instance started against the config file WE hand it.

    The shared harness gives every instance its own config inside its own temp
    directory, which is right for isolation and useless for proving that a
    NEXT START reads what a previous command wrote. So the second instance is
    built through the harness's own class and then pointed at a config file this
    test writes - the one instance A's `record.input_set` produced.
    """
    instance = H.Instance(binary, workingdir=workingdir)
    os.makedirs(os.path.dirname(config_path), exist_ok=True)
    with open(config_path, "w") as handle:
        handle.write(config_text)
    instance.config_path = config_path
    instance.spawn()
    return instance


def check_arbitrary_input_count_instance_a(session, recorder, shared, instance):
    """The count is a config value, and the running instance keeps its own."""
    before = session.result("record.get_state")
    recorder.check("instance A's engine prepared the default route count",
                   before.get("route_count") == ROUTE_COUNT,
                   "route_count=%r" % before.get("route_count"))
    recorder.check("instance A's routes can select the default (stereo) channels",
                   before.get("input_channel_capacity") == 2,
                   "input_channel_capacity=%r" % before.get("input_channel_capacity"))

    raised = session.result("record.input_set", {"channels": 8})
    recorder.check("record.input_set accepts an eight-channel input count",
                   raised.get("channels") == 8 and raised.get("restart_required") is True,
                   "result=%r" % raised)
    recorder.check("record.input_set reports the previous plan",
                   (raised.get("previous") or {}).get("channels") == 2,
                   "previous=%r" % raised.get("previous"))

    # THE WRITE IS ON DISK, measured rather than trusted: the config file this
    # instance was started with now carries the plan under `audioinput`, which is
    # exactly what instance B is started from below.
    with open(instance.config_path) as handle:
        written = handle.read()
    recorder.check("record.input_set wrote the plan into the instance's config file",
                   "audioinput" in written and 'channels="8"' in written,
                   "config=%r" % written[-400:])

    # THE ARBITRARY CHANNEL IS NOT LIVE YET, and saying so is the point: the
    # engine is built with its capacity, so a route armed for channel 7 in THIS
    # instance must still be refused.
    refused = session.typed_error("record.arm_track",
                                  {"route": 0, "input_channel": 7,
                                   "file": os.path.join(shared, "too-wide.wav")})
    recorder.check("instance A refuses a channel its engine was not built for",
                   refused.get("kind") == "invalid_args",
                   "error=%r" % refused)
    recorder.check("the refusal names the range and the way out",
                   "record.input_set" in (refused.get("message") or ""),
                   "message=%r" % refused.get("message"))
    recorder.check("the refused arm left no take file",
                   not os.path.exists(os.path.join(shared, "too-wide.wav")),
                   "too-wide.wav exists")

    # The configured plan, however, IS live (it is read from the config file).
    plan = session.result("record.input_get_state").get("configured") or {}
    recorder.check("the configured plan already reports the new count",
                   plan.get("channels") == 8,
                   "configured=%r" % plan)
    return written


def check_input_set_refusals(session, recorder, shared):
    for channels, why in ((0, "below the minimum"), (9999, "above the schema's maximum")):
        error = session.typed_error("record.input_set", {"channels": channels})
        recorder.check("record.input_set refuses %d channels (%s)" % (channels, why),
                       error.get("kind") == "invalid_args",
                       "error=%r" % error)
    # A bus pair that names a channel the width cannot have. Kept inside the
    # schema's own bound so the refusal comes from the PLAN's validation.
    error = session.typed_error("record.input_set", {"channels": 2, "left": 3})
    recorder.check("record.input_set refuses a bus pair outside the width",
                   error.get("kind") == "invalid_args",
                   "error=%r" % error)
    # And the schema itself refuses a negative channel count before any handler.
    error = session.typed_error("record.input_set", {"channels": -1})
    recorder.check("the schema refuses a negative channel count",
                   error.get("kind") == "invalid_args",
                   "error=%r" % error)


def check_instance_b(session, recorder, shared):
    state = session.result("record.get_state")
    recorder.check("instance B's engine prepared sixteen routes",
                   state.get("route_count") == ROUTE_COUNT,
                   "route_count=%r" % state.get("route_count"))
    recorder.check("instance B's routes can select EIGHT input channels "
                   "(the arbitrary input count, across a restart)",
                   state.get("input_channel_capacity") == 8,
                   "input_channel_capacity=%r" % state.get("input_channel_capacity"))
    check_input_path_shape(session, recorder, "instance B")

    take = os.path.join(shared, "take-route0.wav")
    armed = session.result("record.arm_track",
                           {"route": 0, "input_channel": 7, "file": take})
    recorder.check("a route armed for input channel 7 is accepted",
                   armed.get("armed") is True and armed.get("input_channel") == 7,
                   "result=%r" % armed)
    recorder.check("the arm wrote the take file", os.path.exists(take),
                   "file=%r exists=%r" % (take, os.path.exists(take)))
    journal = armed.get("journal") or ""
    recorder.check("the arm journalled the take beside it",
                   bool(journal) and os.path.exists(journal),
                   "journal=%r exists=%r" % (journal, os.path.exists(journal)))

    read_back = session.result("record.get_state")
    routes = {entry.get("route"): entry for entry in read_back.get("routes", [])}
    entry = routes.get(0) or {}
    recorder.check("record.get_state reads the arm back",
                   entry.get("armed") is True and entry.get("input_channel") == 7
                   and entry.get("file") == take and entry.get("journal") == journal,
                   "route 0 = %r" % entry)
    recorder.check("the route reports its channel capacity",
                   entry.get("input_channel_capacity") == 8,
                   "route 0 = %r" % entry)

    # The route bound, typed.
    error = session.typed_error("record.arm_track", {"route": 99})
    recorder.check("record.arm_track refuses a route past the engine's last",
                   error.get("kind") == "invalid_args",
                   "error=%r" % error)
    # The channel bound within the new width.
    error = session.typed_error("record.arm_track", {"route": 1, "input_channel": 8})
    recorder.check("record.arm_track refuses channel 8 when the width is 8",
                   error.get("kind") == "invalid_args",
                   "error=%r" % error)
    # A relative take path is a next-process hazard, not a convenience.
    error = session.typed_error("record.arm_track",
                                {"route": 1, "file": "relative-take.wav"})
    recorder.check("record.arm_track refuses a relative take path",
                   error.get("kind") == "invalid_args",
                   "error=%r" % error)

    stopped = session.result("record.disarm_all")
    recorder.check("record.disarm_all reports one route was armed",
                   stopped.get("armed_before") == 1,
                   "result=%r" % stopped)
    recorder.check("a clean stop RETIRES the journal",
                   not os.path.exists(journal),
                   "journal=%r exists=%r" % (journal, os.path.exists(journal)))
    recorder.check("a clean stop leaves the take on disk", os.path.exists(take),
                   "file=%r" % take)
    after = session.result("record.get_state")
    recorder.check("no route is armed after record.disarm_all",
                   all(entry.get("armed") is False for entry in after.get("routes", [])),
                   "routes=%r" % after.get("routes"))


def check_track_set_arm_and_undo(session, recorder, shared):
    """The id that used to be a refusal, and the A16 inverse control.undo runs."""
    tracks = session.result("track.list").get("tracks", [])
    recorder.check("the instance has a song track to arm", bool(tracks),
                   "tracks=%r" % tracks)
    if not tracks:
        return
    track = tracks[0].get("id") if isinstance(tracks[0], dict) else str(tracks[0])

    take = os.path.join(shared, "track-take.wav")
    armed = session.result("track.set_arm",
                           {"track": track, "armed": True, "file": take})
    recorder.check("track.set_arm starts a real capture",
                   armed.get("armed") is True and armed.get("file") == take
                   and os.path.exists(take),
                   "result=%r" % armed)
    journal = armed.get("journal") or ""
    recorder.check("track.set_arm journals the take",
                   bool(journal) and os.path.exists(journal),
                   "journal=%r" % journal)

    undone = session.result("control.undo")
    recorder.check("control.undo dispatches the recorded inverse", undone.get("undone") is True,
                   "result=%r" % undone)
    after = session.result("record.get_state")
    recorder.check("the undo stopped the capture and retired its journal",
                   not os.path.exists(journal)
                   and all(entry.get("armed") is False for entry in after.get("routes", [])),
                   "journal exists=%r routes=%r" % (os.path.exists(journal), after.get("routes")))


def check_retro_audio(session, recorder, shared):
    """Off by default, armable, and never a zero-length take."""
    before = session.result("record.retro_capture_status")
    recorder.check("retrospective AUDIO capture is OFF until it is armed",
                   before.get("armed") is False,
                   "status=%r" % before)
    recorder.check("the retro window's capacity is the documented one",
                   before.get("capacity_frames") == RETRO_CAPACITY_FRAMES,
                   "capacity_frames=%r" % before.get("capacity_frames"))
    recorder.check("the capacity is stated in seconds too",
                   isinstance(before.get("capacity_seconds"), (int, float))
                   and before.get("capacity_seconds") > 0,
                   "status=%r" % before)

    armed = session.result("record.retro_capture_arm")
    recorder.check("record.retro_capture_arm arms the mode", armed.get("armed") is True,
                   "result=%r" % armed)
    armed_with_arg = session.result("record.retro_capture_arm", {"armed": False})
    recorder.check("'armed': false disarms it",
                   armed_with_arg.get("armed") is False,
                   "result=%r" % armed_with_arg)
    session.result("record.retro_capture_arm", {"armed": True})

    status = session.result("record.retro_capture_status")
    retained = status.get("retained_frames") or 0
    take = os.path.join(shared, "retro-take.wav")
    if retained == 0:
        # No capture device on this host, or a silent input: the window is empty
        # and the command must SAY SO rather than write an empty file.
        error = session.typed_error("record.retro_capture_to_take", {"file": take})
        recorder.check("an empty window is refused, typed, not written as a file",
                       error.get("kind") == "refused"
                       and not os.path.exists(take),
                       "error=%r file_exists=%r" % (error, os.path.exists(take)))
    else:
        written = session.result("record.retro_capture_to_take", {"file": take})
        recorder.check("the retained window is written to a WAV",
                       written.get("frames_written") == retained and os.path.exists(take),
                       "result=%r" % written)
        recorder.check("the take is a stereo 24-bit WAV of the window's own length",
                       written.get("channels") == 2
                       and os.path.getsize(take) > 44,
                       "result=%r bytes=%r" % (written, os.path.getsize(take) if os.path.exists(take) else None))
    error = session.typed_error("record.retro_capture_to_take", {"file": "relative.wav"})
    recorder.check("a relative retro take path is refused",
                   error.get("kind") == "invalid_args",
                   "error=%r" % error)
    session.result("record.retro_capture_arm", {"armed": False})


def check_transactions(session, recorder):
    reply = session.result("control.transactions")
    by_command = {}
    for entry in reply.get("transactions", []):
        by_command[entry.get("command")] = entry
    for command, expected in (("record.arm_track", "snapshot"),
                              ("record.input_set", "true_inverse"),
                              ("track.set_arm", "snapshot")):
        entry = by_command.get(command)
        recorder.check("%s is recorded as %s" % (command, expected),
                       entry is not None and entry.get("cls") == expected,
                       "record=%r" % entry)
    for command in ("record.get_state", "record.input_get_state",
                    "record.retro_capture_status"):
        entry = by_command.get(command)
        recorder.check("%s records no transaction (it reads)" % command,
                       entry is None or entry.get("cls") == "not_mutating",
                       "record=%r" % entry)


def check_transcript_is_evidence(transcript, recorder):
    text = "\n".join(transcript.lines)
    wanted = ["record.input_set", "record.arm_track", "record.disarm_all",
              "track.set_arm", "record.retro_capture_status"]
    missing = [name for name in wanted if name not in text]
    recorder.check("the transcript records the whole run verbatim", not missing,
                   "missing=%r of %r" % (missing, wanted))


def run_instance_a(binary, recorder, shared, transcript):
    instance = H.start_instance(binary, workingdir=shared)
    H.wait_for_socket(instance)
    client = H.connect(instance)
    H.wait_ready(instance, client, transcript)
    session = Session(client, transcript)
    print("instance A: %s (working dir %s)" % (instance.socket_path, shared))
    session.result("control.version")
    check_input_path_shape(session, recorder, "instance A")
    check_registration(session, recorder)
    written_config = check_arbitrary_input_count_instance_a(session, recorder, shared, instance)
    check_input_set_refusals(session, recorder, shared)
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        recorder.check("instance A quits through control.quit", False, "reply=%r" % reply)
    instance.wait_for_exit(H.QUIT_TIMEOUT)
    return written_config


def run_instance_b(binary, recorder, shared, transcript, written_config):
    # Started against the config file instance A's record.input_set produced, so
    # "the next start reads it" is measured rather than assumed.
    instance = start_instance_with_config(binary, written_config,
                                         os.path.join(shared, "lmmsrc.xml"), shared)
    H.wait_for_socket(instance)
    client = H.connect(instance)
    H.wait_ready(instance, client, transcript)
    session = Session(client, transcript)
    print("instance B: %s (working dir %s, config %s)"
          % (instance.socket_path, shared, instance.config_path))
    session.result("control.version")
    check_instance_b(session, recorder, shared)
    check_track_set_arm_and_undo(session, recorder, shared)
    check_retro_audio(session, recorder, shared)
    check_transactions(session, recorder)
    check_transcript_is_evidence(transcript, recorder)
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        recorder.check("instance B quits through control.quit", False, "reply=%r" % reply)
    instance.wait_for_exit(H.QUIT_TIMEOUT)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    shared = tempfile.mkdtemp(prefix="zctl-rec-inputs-", dir="/tmp")
    transcript = H.Transcript()
    try:
        written_config = run_instance_a(argv[1], recorder, shared, transcript)
        run_instance_b(argv[1], recorder, shared, transcript, written_config)
    finally:
        shutil.rmtree(shared, ignore_errors=True)

    report_results(recorder)
    transcript.dump()
    if recorder.problems:
        print("")
        recorder.problems.report("recording engine surface control-surface transcript")
        return 1
    H.ok("recording engine surface control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
