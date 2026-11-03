"""Q050-277: the clock-synced capture checks of tests/control-clock-commands.py (check 13), kept in a module of their own.

`session.record_slot` with `steps` and `clock_sync` on the live engine: the refusals, then START once at the capture's start
tick, STOP exactly the chosen steps later and exactly the span's clock pulses between (what the engine HANDED its MIDI
client, read from clock.get_state's counters - the bound docs/KNOWN-LIMITATIONS.md states). The master is off when this runs
(the undo checks left it so), and is off again when it returns. Takes the transcript's Session and Recorder objects."""

import time

GRID_PULSE_TICKS = 2
GRID_BEAT_TICKS = 12
CAPTURE_STEPS = 4
CAPTURE_WAIT_S = 8.0


def emitted(session, name):
    master = session.result("clock.get_state").get("master") or {}
    return int((master.get("emitted") or {}).get(name, 0))


def prepare_cell(session):
    """A sample track's column in a one-cell grid, launching at once; the cell's address."""
    column = int(session.result("track.add", {"type": "sample"}).get("index", 0))
    session.result("session.set_grid", {"tracks": column + 1, "scenes": 1})
    session.result("session.set_quantisation", {"quantisation": "none"})
    return {"track": column, "scene": 0}


def check_refusals(session, recorder, cell):
    both = session.typed_error("session.record_slot", dict(cell, bars=1, steps=4))
    recorder.check("a capture asking for bars AND steps is invalid_args",
                   both.get("kind") == "invalid_args", "error=%r" % both)
    off = session.typed_error("session.record_slot", dict(cell, steps=CAPTURE_STEPS, clock_sync=True))
    recorder.check("clock_sync with the master off is refused and names clock.master_set",
                   off.get("kind") == "refused" and "clock.master_set" in str(off.get("message")), "error=%r" % off)
    status = session.result("session.record_slot_status")
    recorder.check("the refused capture armed nothing", status.get("state") == "idle", "status=%r" % status)


def master_on(session):
    """Turns the master on towards the first writable port (ALSA's Midi Through loopback: nothing leaves the box)."""
    writable = session.result("midi.device_list").get("writable") or []
    args = {"enabled": True}
    if writable:
        args["port"] = writable[0]
    return session.result("clock.master_set", args)


def run_capture(session, cell):
    """Arms the synced capture and waits for its STOP; returns (armed reply, status, START/STOP/pulse counts sent)."""
    before = [emitted(session, name) for name in ("start", "stop", "clock")]
    armed = session.result("session.record_slot", dict(cell, steps=CAPTURE_STEPS, clock_sync=True))
    deadline = time.time() + CAPTURE_WAIT_S
    while time.time() < deadline and emitted(session, "stop") == before[1]:
        time.sleep(0.05)
    sent = [emitted(session, name) - was for name, was in zip(("start", "stop", "clock"), before)]
    return armed, session.result("session.record_slot_status"), sent


def check_boundaries(recorder, armed, status, sent):
    span = status.get("clock") or {}
    ticks = CAPTURE_STEPS * GRID_BEAT_TICKS
    recorder.check("the capture is armed for its steps with the clock synced",
                   armed.get("clock_sync") is True and armed.get("length_unit") == "steps"
                   and armed.get("length_ticks") == ticks, "armed=%r" % armed)
    recorder.check("START went out once, at the capture's start tick",
                   sent[0] == 1 and span.get("start_tick") == status.get("start_tick"), "sent=%r span=%r" % (sent, span))
    recorder.check("STOP went out once, exactly the chosen steps after START",
                   sent[1] == 1 and span.get("active") is False
                   and span.get("stop_tick") == span.get("start_tick") + ticks, "sent=%r span=%r" % (sent, span))
    recorder.check("exactly the span's clock pulses went between them",
                   sent[2] == ticks // GRID_PULSE_TICKS and span.get("pulses") == ticks // GRID_PULSE_TICKS,
                   "sent=%r span=%r" % (sent, span))


def check_clock_synced_capture(session, recorder):
    cell = prepare_cell(session)
    check_refusals(session, recorder, cell)
    if not master_on(session).get("port_ready"):
        nobody = session.typed_error("session.record_slot", dict(cell, steps=CAPTURE_STEPS, clock_sync=True))
        recorder.check("with no destination for the clock the synced capture is refused (no writable MIDI port here)",
                       nobody.get("kind") == "refused", "error=%r" % nobody)
    else:
        armed, status, sent = run_capture(session, cell)
        check_boundaries(recorder, armed, status, sent)
        session.result("session.record_slot", dict(cell, cancel=True))
    session.result("clock.master_set", {"enabled": False})
