#!/usr/bin/env python3
"""ControlRetroCapture - the registered proof for retrospective MIDI capture.

Owner item 14: docs/MIDI-RETRO-CAPTURE.md is the design, docs/MIDI-RETRO-CAPTURE-BOUNDS.md
states the bound, and this file MEASURES both through `--control-socket`.

The claim it exists to check is the one the feature was built for: MIDI that was
PLAYED can be recovered afterwards - the "I should have hit record" case. So it
plays REAL MIDI into the running engine from the host's ALSA sequencer (the
`aplaymidi` client: an external process, exactly like a keyboard) and then pulls
the window back out over the socket. Nothing in this process can reach the
capture path directly, so the events enter the engine the way a player's do.

Each assertion is a real effect rather than a success report:

  * DEFAULT OFF - the same file is played BEFORE arming and the window is still
    empty afterwards; the SAME file is then played armed and the window holds
    exactly the events that were played. Each reading is the measurement of the
    other;
  * the notes' OWN positions, lengths, keys and velocities, read back from the
    engine through roll.get_state - not from the command's own counters;
  * the clip id appears in arrangement.get_state and is gone after exactly ONE
    control.undo (SPEC A16: the recorded inverse is clip.delete);
  * THE BOUND - more events are played than the ring can hold, and the engine's
    own numbers have to match the documented bound exactly: retained == the
    documented capacity, and retained + overwritten + paused_dropped == played.
    The capacity the build reports must also equal the figure the bounds
    document states, so the document and the binary cannot drift apart.

It reports ctest *Skipped* (exit 77) - never *Passed* - when the host cannot
supply real MIDI input (no `aconnect`/`aplaymidi`, or the engine came up on the
dummy MIDI client), because a test that cannot make its measurement must not
report that it did.

Usage: QT_QPA_PLATFORM=offscreen python3 control-retro-capture.py <zene-binary>
"""

import os
import re
import struct
import subprocess
import sys
import time

from control_socket_harness import (Instance, Problems, Transcript, connect,
                                    finish, ok_result, wait_ready)

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE = os.path.join(HERE, "data", "agent-control-fixture.mmp")
BOUNDS_DOC = os.path.join(os.path.dirname(HERE), "docs", "MIDI-RETRO-CAPTURE-BOUNDS.md")

# The engine's name for the ALSA-sequencer client (include/MidiAlsaSeq.h). Matched
# by prefix: it is built from two adjacent string literals there.
SEQUENCER = "ALSA-Sequencer"
ARM = "midi.retro_capture_arm"
STATUS = "midi.retro_capture_status"
TO_CLIP = "midi.retro_capture_to_clip"

# The file played for the recovery half: (tick, key, velocity, length) in absolute
# sequencer ticks. Every field differs between the two notes, so a swapped or
# rescaled one cannot pass.
SMALL_NOTES = ((0, 60, 100, 240), (240, 64, 64, 240))
SMALL_WINDOW_END = 480
CLIP_LENGTH = 960
# The bound half is played as BURST_RUNS repetitions of a BURST_PAIRS-pair file,
# paced by BURST_GAP_SECONDS. The pacing is not tidiness: the engine's ALSA client
# has a bounded INPUT POOL, so a single 20000-event burst overflows it IN THE
# KERNEL and the capture never sees most of the events - measured on this box, one
# 18000-event burst delivered 614 events and the rest were dropped before the ring,
# which is a fact about the pool and not about the window under test. 200 events
# per run is comfortably inside what one burst was measured to deliver, and pacing
# lets the MIDI thread drain between runs so the accounting below is exact.
# BURST_RUNS x BURST_PAIRS x 2 = 20000 events, 2.4x the window.
BURST_PAIRS = 100
BURST_RUNS = 100
BURST_GAP_SECONDS = 0.15
SETTLE_SECONDS = 2.0
BOUND_SECONDS = 180.0


# ---------------------------------------------------------------------------
# the MIDI source: a Standard MIDI File played by the host's own ALSA client
# ---------------------------------------------------------------------------


def vlq(value):
    """One MIDI variable-length quantity."""
    out = bytearray([value & 0x7F])
    value >>= 7
    while value:
        out.insert(0, (value & 0x7F) | 0x80)
        value >>= 7
    return bytes(out)


def write_smf(path, notes):
    """A format-0 file whose notes sit at the ABSOLUTE ticks given.

    aplaymidi keeps the file's own tick as the sequencer event's timestamp, which
    is what makes this test deterministic: the ticks written here are the ticks
    the engine measures the notes at. The probe that established it is recorded in
    docs/MIDI-RETRO-CAPTURE-BOUNDS.md.
    """
    body = bytearray()
    cursor = 0
    for tick, key, velocity, length in notes:
        body += vlq(tick - cursor) + bytes((0x90, key, velocity))
        body += vlq(length) + bytes((0x80, key, 0))
        cursor = tick + length
    body += b"\x00\xff\x2f\x00"
    header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480)
    with open(path, "wb") as handle:
        handle.write(header + b"MTrk" + struct.pack(">I", len(body)) + bytes(body))


def host_tools():
    """The two alsa-utils tools this test measures with, or (None, None)."""
    found = []
    for tool in ("aconnect", "aplaymidi"):
        which = subprocess.run(["sh", "-c", "command -v %s" % tool],
                               capture_output=True, text=True)
        found.append(which.stdout.strip() or None)
    return found[0], found[1]


def sequencer_port(aconnect, pid):
    """(client, port) of the ALSA-sequencer client opened by `pid`, or (None, None).

    Matching on the PID is what keeps this test away from the other Zene Studio
    clients on a shared box: the client NAME is not unique, the pid is.
    """
    listing = subprocess.run([aconnect, "-l"], capture_output=True, text=True)
    if listing.returncode != 0:
        return None, None
    client = None
    for line in listing.stdout.splitlines():
        head = re.match(r"^client\s+(\d+):\s+'.*'\s+\[type=user(?:,pid=(\d+))?\]", line)
        if head:
            client = int(head.group(1)) if head.group(2) == str(pid) else None
            continue
        if client is None:
            continue
        port = re.match(r"^\s+(\d+)\s+'", line)
        if port:
            return client, int(port.group(1))
    return None, None


def play_notes(aplaymidi, port, path, problems):
    """Play a file into the engine. Bounded, and its exit code is reported."""
    if port is None or not os.path.exists(aplaymidi):
        problems.add("cannot play %s: no aplaymidi at %r" % (path, aplaymidi))
        return False
    done = subprocess.run([aplaymidi, "-p", "%d:%d" % port, path],
                          capture_output=True, text=True, timeout=BOUND_SECONDS)
    problems.require(done.returncode == 0,
                     "aplaymidi exited %d playing %s: %s"
                     % (done.returncode, os.path.basename(path), done.stderr.strip()))
    return done.returncode == 0


def documented_capacity():
    """The retained-event figure docs/MIDI-RETRO-CAPTURE-BOUNDS.md states."""
    with open(BOUNDS_DOC) as handle:
        text = handle.read()
    match = re.search(r"\*\*([\d,]+) events\*\*", text)
    return int(match.group(1).replace(",", "")) if match else None


# ---------------------------------------------------------------------------
# the socket side
# ---------------------------------------------------------------------------


class Session:
    """A control client that numbers its own requests and keeps a transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last = 0

    def call(self, cmd, args=None):
        self.last += 1
        return self.client.call(self.last, cmd, args or {}, transcript=self.transcript)

    def ok(self, cmd, args=None):
        return ok_result(self.call(cmd, args), self.last)


def clip_ids(session):
    """Every clip id arrangement.get_state reports, as a set."""
    state = session.ok("arrangement.get_state")
    return set(clip["id"] for clip in state["clips"])


def wait_buffered(session, expected, seconds):
    """Poll midi.retro_capture_status until the window holds `expected` events."""
    deadline = time.time() + seconds
    status = session.ok(STATUS)
    while status["events_buffered"] != expected and time.time() < deadline:
        time.sleep(0.1)
        status = session.ok(STATUS)
    return status


def expected_notes():
    return sorted((tick, length, key, velocity)
                  for tick, key, velocity, length in SMALL_NOTES)


def roll_notes(session, clip):
    """(position, length, key, velocity) per note, straight from the engine."""
    state = session.ok("roll.get_state", {"clip": clip})
    return sorted((int(n["position"]), int(n["length"]), int(n["key"]),
                   int(n["velocity"])) for n in state["notes"])


# ---------------------------------------------------------------------------
# the measurements. Each takes the shared run context and a Problems to fill.
# ---------------------------------------------------------------------------


def check_default_off(context, problems):
    """The same file, played disarmed, must leave the window empty."""
    session = context["session"]
    before = session.ok(STATUS)
    problems.require(before["armed"] is False,
                     "the capture was already armed at start: %r" % before["armed"])
    problems.require(before["events_buffered"] == 0,
                     "the window already held %r events before anything was played"
                     % before["events_buffered"])
    play_notes(context["aplaymidi"], context["port"], context["small_file"], problems)
    time.sleep(SETTLE_SECONDS)
    after = session.ok(STATUS)
    problems.require(after["events_buffered"] == 0,
                     "a DISARMED capture recorded %r events: the default is not off"
                     % after["events_buffered"])


def check_recovery(context, problems):
    """Arm, play the same file, recover the notes' own positions and velocities."""
    session = context["session"]
    armed = session.ok(ARM)
    problems.require(armed["armed"] is True, "arming reported armed=%r" % armed["armed"])
    problems.require(armed["changed"] is True,
                     "arming an unarmed capture reported changed=%r" % armed["changed"])

    play_notes(context["aplaymidi"], context["port"], context["small_file"], problems)
    played = 2 * len(SMALL_NOTES)
    status = wait_buffered(session, played, SETTLE_SECONDS * 4)
    problems.require(status["events_buffered"] == played,
                     "the window holds %r events, %d were played"
                     % (status["events_buffered"], played))
    problems.require(status["paused_dropped"] == 0,
                     "%r events were dropped by the snapshot handshake"
                     % status["paused_dropped"])

    context["before"] = clip_ids(session)
    written = session.ok(TO_CLIP, {"length": CLIP_LENGTH})
    context["written"] = written
    problems.require(written["events"] == played,
                     "to_clip consumed %r events, %d were played"
                     % (written["events"], played))
    problems.require(written["window_start"] == SMALL_NOTES[0][0]
                     and written["window_end"] == SMALL_WINDOW_END,
                     "the window is [%r, %r], the file's notes sit in [%d, %d]"
                     % (written["window_start"], written["window_end"],
                        SMALL_NOTES[0][0], SMALL_WINDOW_END))
    problems.require(written["events_written"] == len(SMALL_NOTES),
                     "to_clip wrote %r notes, %d were played"
                     % (written["events_written"], len(SMALL_NOTES)))
    problems.require(written["unmatched_ons"] == 0 and written["unmatched_offs"] == 0,
                     "the matcher reported %r unmatched note-ons and %r unmatched "
                     "releases for a file of complete pairs"
                     % (written["unmatched_ons"], written["unmatched_offs"]))

    measured = roll_notes(session, written["clip"])
    problems.require(measured == expected_notes(),
                     "roll.get_state reports %r; the file that was played holds %r "
                     "(position, length, key, velocity)"
                     % (measured, expected_notes()))
    problems.require(written["clip"] in clip_ids(session),
                     "the new clip %r is not in the arrangement"
                     % written["clip"])


def check_undo(context, problems):
    """ONE control.undo must take the captured clip back off (SPEC A16)."""
    session = context["session"]
    written = context["written"]
    before = context["before"]
    problems.require(clip_ids(session) == before | {written["clip"]},
                     "after to_clip the arrangement lists %r, expected %r"
                     % (sorted(clip_ids(session)), sorted(before | {written["clip"]})))
    undone = session.ok("control.undo")
    problems.require(undone["undone"] is True,
                     "control.undo reported undone=%r" % undone["undone"])
    problems.require(undone.get("undone_command") == TO_CLIP,
                     "control.undo unwound %r from the journal, not the capture"
                     % undone.get("undone_command"))
    after = clip_ids(session)
    problems.require(after == before,
                     "after ONE control.undo the arrangement lists %r, expected the "
                     "pre-capture %r" % (sorted(after), sorted(before)))


def play_bursts(aplaymidi, port, path, problems):
    """Play the same file BURST_RUNS times, paced, so nothing is lost to the pool."""
    for _ in range(BURST_RUNS):
        if not play_notes(aplaymidi, port, path, problems):
            return
        time.sleep(BURST_GAP_SECONDS)


def check_bound(context, problems):
    """Play more than the ring can hold and assert the DOCUMENTED behaviour.

    Nothing here asserts a crash or a refusal, because the documented behaviour is
    drop-OLDEST with the loss counted: the window keeps the newest events, and the
    three numbers the engine reports have to add up to what was played.
    """
    session = context["session"]
    capacity = session.ok(STATUS)["capacity_events"]
    documented = documented_capacity()
    problems.require(documented is not None,
                     "%s states no retained-event figure" % BOUNDS_DOC)
    problems.require(capacity == documented,
                     "the build retains %r events, %s documents %r"
                     % (capacity, os.path.basename(BOUNDS_DOC), documented))

    play_bursts(context["aplaymidi"], context["port"], context["burst_file"], problems)
    played = context["played"] + 2 * BURST_PAIRS * BURST_RUNS
    status = wait_buffered(session, capacity, BOUND_SECONDS)
    problems.require(status["events_buffered"] == capacity,
                     "the window holds %r events after %d were played; the documented "
                     "bound is %r" % (status["events_buffered"], played, capacity))
    accounted = (status["events_buffered"] + int(status["overwritten"])
                 + int(status["paused_dropped"]))
    problems.require(accounted == played,
                     "the engine accounts for %r events (%r retained + %r overwritten "
                     "+ %r paused) but %d were played: an event was lost WITHOUT being "
                     "counted" % (accounted, status["events_buffered"],
                                  status["overwritten"], status["paused_dropped"], played))
    problems.require(status["overwritten"] > 0,
                     "nothing was reported overwritten although %d events were played "
                     "into a %r-event window" % (played, capacity))
    problems.require(status["armed"] is True,
                     "the capture disarmed itself while the bound was measured")


def check_alive(context, problems):
    """The bound is a policy, not a crash: the instance answers afterwards."""
    session = context["session"]
    instance = context["instance"]
    problems.require(instance.alive(),
                     "the instance exited (code %r) during the measurement"
                     % instance.process.returncode)
    ping = session.ok("control.ping")
    problems.require(ping.get("pong") is True,
                     "control.ping answered %r after the bound was measured" % ping)


# ---------------------------------------------------------------------------
# setup and main
# ---------------------------------------------------------------------------


def readable_fixture(instance):
    """The committed fixture, with its MIDI ports made READABLE.

    A project's instrument track only opens an ALSA-sequencer input port when its
    <midiport> is readable (src/core/midi/MidiPort.cpp: loadSettings ->
    updateMidiPortMode -> applyPortMode), so this is what gives the engine
    somewhere for a player's events to arrive. It is written into the instance's
    own temp world and opened over the socket, like any other project.
    """
    with open(FIXTURE) as handle:
        text = handle.read()
    path = os.path.join(instance.tmp, "retro-capture.mmp")
    with open(path, "w") as handle:
        handle.write(text.replace('readable="0"', 'readable="1"'))
    return path


def skipped(reason, *instances):
    for instance in instances:
        instance.close()
    print("SKIPPED: %s" % reason)
    sys.exit(77)


def step(name, function, context):
    """Run one measurement, print its problems, and return its (name, ok, problems)."""
    problems = Problems()
    function(context, problems)
    print("")
    print("%-58s %s"
          % (name, "ok" if not problems else "FAILED (%d)" % len(problems.items)))
    for item in problems.items:
        print("      %s" % item)
    # control_socket_harness.finish() iterates the third element, so it is the
    # list of problem strings, not the Problems object.
    return (name, not problems, problems.items)


def prepare(session, instance, aconnect):
    """Everything the measurements need, or a 77 exit naming what is missing."""
    opened = session.ok("project.open", {"path": readable_fixture(instance)})
    if opened.get("loaded_with_errors"):
        skipped("the fixture did not load cleanly (%d error(s))"
                % opened.get("error_count", 0), instance)
    running = session.ok(STATUS)
    if not str(running["client"]).startswith(SEQUENCER):
        skipped("the engine is running the %r MIDI client, not the %r one: no "
                "external sequencer can play into it" % (running["client"], SEQUENCER),
                instance)
    port_client, port = sequencer_port(aconnect, instance.process.pid)
    if port is None:
        skipped("the engine's ALSA-sequencer client (pid %d) has opened no port, so "
                "the fixture's readable <midiport> created none"
                % instance.process.pid, instance)
    return (port_client, port), running


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2

    aconnect, aplaymidi = host_tools()
    if aconnect is None or aplaymidi is None:
        skipped("no ALSA-sequencer tooling on this host (aconnect=%r aplaymidi=%r): "
                "this test needs a real MIDI source to play into the engine"
                % (aconnect, aplaymidi))
    if documented_capacity() is None:
        skipped("%s does not state the retained-event bound" % BOUNDS_DOC)

    instance = Instance(sys.argv[1])
    transcript = Transcript()
    instance.spawn()
    results = []
    try:
        client = connect(instance)
        wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        port, running = prepare(session, instance, aconnect)

        small_file = os.path.join(instance.tmp, "retro-small.mid")
        burst_file = os.path.join(instance.tmp, "retro-burst.mid")
        write_smf(small_file, SMALL_NOTES)
        write_smf(burst_file, tuple((0, 36 + (i % 84), 100, 0) for i in range(BURST_PAIRS)))

        context = {"session": session, "instance": instance, "port": port,
                   "aplaymidi": aplaymidi, "small_file": small_file,
                   "burst_file": burst_file, "played": 0}
        print("")
        print("engine   : %r" % running["client"])
        print("source   : aplaymidi -> %d:%d (pid %d)"
              % (port[0], port[1], instance.process.pid))
        results.append(step("the capture is OFF until it is armed",
                            check_default_off, context))
        results.append(step("what was played comes back with its own "
                            "positions and velocities", check_recovery, context))
        context["played"] = 2 * len(SMALL_NOTES)
        results.append(step("ONE control.undo takes the recovered clip back off",
                            check_undo, context))
        results.append(step("more than the window holds: the documented bound",
                            check_bound, context))
        results.append(step("the instance is still answering afterwards",
                            check_alive, context))
    finally:
        instance.close()
    return finish(results)


if __name__ == "__main__":
    sys.exit(main())
