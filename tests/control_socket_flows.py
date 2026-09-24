#!/usr/bin/env python3
"""The case/flow payloads for the control-surface tests: the DIALOGUE, not the plumbing.

Split out of `control_socket_harness.py` on 2026-09-12: the harness core -
`Instance`, `Client`, the readiness poll and the assertion helpers - had grown
past the 500-line per-file ratchet (Gate 7) together with the case payloads.
This module holds the parts a TEST calls, not the parts that start a process or
speak the protocol:

  - the multi-instance flow helpers the headless tests drive
    (`healthy_control`, `diagnose_block`, `parse_args`, `report_pre_fix`);
  - the pure `evidence -> problems` checkers (`check_*`), which take the raw
    evidence a test has already gathered and return the list of failed claims.
    There is no I/O in them: a checker can be pointed at a hand-written reply
    (that is what tests/control-negative-control.py does);
  - one reader, `read_cap_refusal`: it takes an already-connected client and the
    problems it reports into, because the case that needs it must NOT send on the
    connection it is reading (the server is retiring it), so `Client.call` is not
    usable there - and a raw one-line read was measured being fooled by a late
    reply to an earlier request (job 103762607496, linux-arm64). This is the only
    thing here that touches a socket, and it owns no bound of its own.

Everything these functions call - the bounds, the launch recipe, the socket
client, the readiness poll - is imported from `control_socket_harness`; there is
no second copy of any of it.

Usage is unchanged (each test script owns its own argv):

    QT_QPA_PLATFORM=offscreen python3 <test>.py <lmms> [...]

Exit code 0 only when every assertion passed. A hang is still a failure, and
every bound still lives in `control_socket_harness`.
"""

import json
import sys
import time

from control_socket_harness import (  # noqa: E402
    FATAL_GUARD_MARKER, LEGACY_WATCHDOG_LINE, PING_TIMEOUT, Blocked, Timeout, Transcript,
    connect, ok, ok_result, start_instance, wait_ready,
)


def read_cap_refusal(client, problems):
    """The typed refusal for an over-cap request line, once it IS the cap refusal.

    Raw on purpose: `Client.call` would SEND on the connection being retired, so the
    caller cannot use it, and a raw reader that takes the first line has already been
    fooled once. It therefore applies the harness's own staleness rule itself - a line
    that answers a DISPATCHED request (id >= 0) is a late reply to an earlier request
    and is discarded; an over-cap line is never dispatched, so its refusal is the line
    with a negative id.

    Measured, job 103762607496 (linux-arm64): the readiness poll's pings are answered
    late there (one core inside Engine::init for ~34s), a ping's reply was still in the
    buffer AHEAD of the refusal, and the first raw line was taken as the refusal -
    "an over-cap request line answered {'id': 0, ...}", "the refusal does not name the
    1048576-byte cap: {}", "the refusal carries id 0". The refusal itself had been sent
    (typed, naming the cap, id -1): it was one line further back.

    Bounded by PING_TIMEOUT across the discards, so a pre-fix server that buffered the
    junk and answered it as a dispatched request still fails, as "the line was buffered
    instead of capped". Returns the refusal, or None after adding the problem.
    """
    deadline = time.time() + PING_TIMEOUT
    while True:
        remaining = deadline - time.time()
        try:
            line = client._read_line(remaining if remaining > 0 else 0.05)  # noqa: SLF001
        except Timeout as nothing:
            problems.add("no refusal arrived for an over-cap request line inside %.0fs: %s "
                         "(the line was buffered instead of capped)" % (PING_TIMEOUT, nothing))
            return None
        try:
            candidate = json.loads(line.decode("utf-8", "replace"))
        except ValueError:
            print("note: discarded a line that is not a JSON reply while reading the cap "
                  "refusal: %r" % line[:160])
            continue
        if candidate.get("id", 0) < 0:
            return candidate
        print("note: discarded a reply to an earlier request while reading the cap refusal: %s"
              % line.decode("utf-8", "replace")[:160])


def read_pipelined_reply(client, expected_id, timeout):
    """(reply, byte-length) for `expected_id`, discarding earlier requests' replies.

    The second raw reader, and it exists for the SAME measured reason `read_cap_refusal`
    does: a raw reader that takes the first line is fooled by a late reply to an earlier
    request). Its caller (tests/control-socket-path-safety.py, the large-reply case) has
    SENT a BATCH of requests and then reads that batch, so `Client.call` is not usable -
    there is no "the" reply to match - and the batch is large enough that the readiness
    poll's own reply can still be ahead of it in the buffer.

    MEASURED, job 103810839355 (linux-arm64, run 34789449244): `wait_ready`'s pings are
    answered late there (one core inside Engine::init for ~34s, the same stall
    control_socket_harness.STARTUP_BOUND documents), so a ping's reply - `{"id":0,…,
    "pong":true,…}`, 244 bytes - was in the buffer AHEAD of the batch. The case's first
    raw `_read_line` took it and the case reported two problems:

        reply 0 was {'id': 0, 'ok': True, … 'pong': True …}, expected the ok reply for id 900
        this case proves nothing here: only 244 bytes were needed …

    Note what those two sentences together say: the case never read ONE batch reply, so
    nothing about the server's large-reply behaviour was measured - and the replies were
    not truncated, the reader was one line out of step. So the staleness rule is applied
    exactly as the harness's `Client.call` applies it (a line answering a DISPATCHED
    request that is not `expected_id` is a late reply to an earlier request and is
    discarded with the same note); the batch's own ids are >= expected_id's, so only
    EARLIER ids are discarded as stale, and any other id is RETURNED so the caller's own
    "reply N was …" check reports it rather than silently dropping it.

    Returns (reply, byte-length), and raises the harness's `Blocked`/`Timeout` when nothing
    matching arrives inside `timeout` (across the discards), so the caller keeps its own
    wording for a batch reply that never arrived.
    """
    deadline = time.time() + timeout
    while True:
        remaining = deadline - time.time()
        line = client._read_line(remaining if remaining > 0 else 0.05)  # noqa: SLF001
        try:
            candidate = json.loads(line.decode("utf-8", "replace"))
        except ValueError:
            print("note: discarded a line that is not a JSON reply while reading the "
                  "batch: %r" % line[:160])
            continue
        if candidate.get("id", expected_id) < expected_id:
            print("note: discarded a reply to an earlier request while reading the batch: "
                  "%s" % line.decode("utf-8", "replace")[:160])
            continue
        return candidate, len(line)


# ---------------------------------------------------------------------------
# flow helpers - a whole case, in one call
# ---------------------------------------------------------------------------


def diagnose_block(instance, client, transcript, request_id=99):
    """After a Blocked, say WHICH block signature was measured.

    Returns one sentence for the report. Raises AssertionError when the instance
    is demonstrably NOT blocked (a ping that answers `engine_ready:true`).
    """
    try:
        reply = client.call(request_id, "control.ping", timeout=PING_TIMEOUT, transcript=transcript)
    except Blocked:
        return "the socket stopped answering altogether (the UI thread is not dispatching events)"
    result = reply.get("result") or {}
    if not result.get("pong"):
        raise AssertionError("control.ping answered without pong: %r" % (reply,))
    if result.get("engine_ready"):
        raise AssertionError("the instance reports engine_ready while presumably blocked: %r"
                             % (reply,))
    return ("the nested modal loop is still dispatching: control.ping answers "
            "pong=true engine_ready=false, i.e. the startup path is parked inside the box")


def healthy_control(binary, seconds=90.0):
    """Prove the binary CAN serve a control socket on this machine, right now.

    A negative control must not be satisfiable by a broken environment: a binary
    started outside its data directory (or on a machine with no Qt platform
    plugin) never becomes ready either, which would make "never became ready"
    mean nothing.  This starts the SAME binary with a working directory that
    exists and the dummy device, and returns how long it took to become ready.
    """
    instance = start_instance(binary)
    transcript = Transcript()
    try:
        client = connect(instance)
        started = time.time()
        wait_ready(instance, client, transcript, seconds=seconds, ping_timeout=PING_TIMEOUT)
        return time.time() - started
    finally:
        instance.close()


def parse_args(usage, minimum_argv=1):
    """argv[1..] with a `--expect-blocked` switch; returns (mode, rest)."""
    argv = [a for a in sys.argv[1:] if a != "--expect-blocked"]
    expect_blocked = "--expect-blocked" in sys.argv[1:]
    if len(argv) < minimum_argv:
        print(usage)
        sys.exit(2)
    return expect_blocked, argv


def report_pre_fix(what):
    ok("%s [pre-fix mode: the block was reproduced, as task #625 measured]" % what)


# ---------------------------------------------------------------------------
# checkers - pure evidence -> problems
# ---------------------------------------------------------------------------


def check_clean_shutdown(exited, exit_code, socket_exists, stderr_text, elapsed_s,
                         expect_socket_unlinked=True):
    """A clean shutdown: gone in time, exit 0, no watchdog, socket unlinked."""
    problems = []
    if not exited:
        problems.append("the process did not exit within the bounded wait (%.1fs); "
                        "the shutdown is blocked" % elapsed_s)
    elif exit_code != 0:
        problems.append("the process exited with %s, expected 0" % exit_code)
    if LEGACY_WATCHDOG_LINE in stderr_text:
        problems.append("stderr carries the pre-fix watchdog line %r: the event loop did not stop"
                        % LEGACY_WATCHDOG_LINE)
    if FATAL_GUARD_MARKER in stderr_text:
        problems.append("stderr carries the last-resort guard line: the normal shutdown did not "
                        "complete (it must never fire)")
    if expect_socket_unlinked and socket_exists:
        problems.append("the control socket file was not unlinked on exit")
    return problems


def check_recovery_cleaned(exited, recovery_exists):
    """A clean quit removes the autosave recovery file.

    `MainWindow::closeEvent` calls `sessionCleanup()` on an accepted close when
    autosave is on, and that removes `recover.mmp`. A shutdown that leaves it
    behind is a failure: the file is the crash marker the next launch offers to
    recover, so a clean quit that keeps it makes the next launch lie.
    """
    problems = []
    if exited and recovery_exists:
        problems.append("the autosave recovery file survived a clean quit "
                        "(MainWindow::closeEvent -> sessionCleanup did not run)")
    return problems


def check_ping_shape(reply, request_id=1):
    """control.ping carries liveness, readiness and the audio report, always."""
    problems = []
    if reply.get("id") != request_id:
        problems.append("ping reply id %r != %r" % (reply.get("id"), request_id))
    if reply.get("ok") is not True:
        problems.append("ping answered %r, expected ok=true (ping must work before the engine "
                        "is ready - that is its whole purpose)" % reply)
        return problems
    result = reply.get("result") or {}
    if result.get("pong") is not True:
        problems.append("ping result has no pong=true: %r" % result)
    if not isinstance(result.get("engine_ready"), bool):
        problems.append("ping result has no boolean engine_ready: %r" % result)
    audio = result.get("audio")
    if not isinstance(audio, dict):
        problems.append("ping result has no audio report: %r" % result)
    else:
        for field in ("state", "device", "requested", "start_failed", "sound_output"):
            if field not in audio:
                problems.append("ping audio report is missing '%s': %r" % (field, audio))
    return problems


def check_readiness_reason(reply):
    """While engine_ready is false, ping must say WHY - a code and a message."""
    problems = []
    result = reply.get("result") or {}
    if result.get("engine_ready") is not False:
        problems.append("expected engine_ready=false to check the reason, got %r" % result)
        return problems
    reason = result.get("reason")
    if not isinstance(reason, dict):
        problems.append("engine_ready=false with no 'reason' object: a bare false is exactly "
                        "the defect this test targets: %r" % result)
        return problems
    if not reason.get("code"):
        problems.append("reason carries no code: %r" % reason)
    if not reason.get("message"):
        problems.append("reason carries no message: %r" % reason)
    return problems


def check_busy_carries_reason(reply):
    """An engine command issued while not ready is a typed busy error, explained."""
    problems = []
    if reply.get("ok") is not False:
        problems.append("an engine command before readiness answered %r, expected a typed "
                        "failure" % reply)
        return problems
    error = reply.get("error") or {}
    if error.get("kind") != "busy":
        problems.append("expected error.kind=busy before readiness, got %r" % error)
    message = error.get("message") or ""
    if not message:
        problems.append("the busy error carries no message: %r" % error)
    return problems


def check_audio_fallback(reply, requested):
    """The no-sound-card report: names the backend that failed, and the fallback."""
    problems = []
    result = reply.get("result") or {}
    audio = result.get("audio") or {}
    if audio.get("start_failed") is not True:
        problems.append("the configured device '%s' cannot open on this box, but the audio "
                        "report says start_failed=%r" % (requested, audio.get("start_failed")))
    if audio.get("state") != "dummy_fallback":
        problems.append("audio.state is %r, expected 'dummy_fallback' (the product fell back "
                        "to the dummy device and must say so)" % audio.get("state"))
    if audio.get("sound_output") is not False:
        problems.append("audio.sound_output is %r, expected false (a dummy device makes no "
                        "sound)" % audio.get("sound_output"))
    if requested not in (audio.get("requested") or ""):
        problems.append("the report does not name the requested backend %r: %r"
                        % (requested, audio.get("requested")))
    if not (audio.get("message") or ""):
        problems.append("the fallback is silent: no audio.message explaining the failure: %r"
                        % audio)
    return problems


def check_requires_device_refusal(reply):
    """A device-dependent command refuses, typed, naming the backend."""
    problems = []
    if reply.get("ok") is not False:
        problems.append("transport.play answered %r on an instance whose audio device failed "
                        "to open; it must refuse with a typed error" % reply)
        return problems
    error = reply.get("error") or {}
    if error.get("kind") != "requires":
        problems.append("expected error.kind=requires, got %r" % error)
    message = error.get("message") or ""
    if not message:
        problems.append("the refusal carries no message: %r" % error)
    return problems


# ---------------------------------------------------------------------------
# the parameter metadata contract (BUG-CTL-5) - a save-canonical-style check on
# the device surface: what plugin.param_get REPORTS is what plugin.param_set
# ENFORCES, for every parameter of a device, probed on both sides.
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# E. the parameter metadata contract (BUG-CTL-5)
# ---------------------------------------------------------------------------


def amplifier_device(catalogue):
    """The catalogue's built-in Amplifier entry, or None (reported, never a
    silent skip: it is a compiled-in device, so its absence is a defect)."""
    for entry in catalogue.get("devices") or []:
        if entry.get("name") == "amplifier":
            return entry
    return None


def _parameter_of(reply):
    """The `parameter` object of a plugin.param_get reply, or {} when refused."""
    if reply.get("ok") is not True:
        return {}
    return (reply.get("result") or {}).get("parameter") or {}


def declared_parameters(client, call, host, effect, first_id):
    """Every parameter of `effect` as plugin.param_get reports it, in the order
    the getter hands them out - stopped by the first typed refusal, which is how
    the wire says the list ended."""
    specs = []
    for offset in range(0, 32):
        reply = call(client, first_id + offset, "plugin.param_get",
                     {"target": host, "plugin": effect, "index": offset})
        if reply.get("ok") is not True:
            break
        specs.append(_parameter_of(reply))
    return specs


def check_edge(client, call, problems, where, spec, request_id, label, value, accepted):
    """One probe of one parameter's edge. `accepted` says which half of the
    contract this probe is: a value INSIDE the reported range must be taken, a
    value just outside it must be refused, with a message that states the very
    range the getter reported. That equality - not just a refusal - is the
    contract: a setter that refused a wider or narrower band would pass a
    bare "was it refused" check and still lie."""
    reply = call(client, request_id, "plugin.param_set",
                 dict(where, name=spec["name"], value=value))
    error = reply.get("error") or {}
    if accepted:
        problems.require(reply.get("ok") is True,
                         "%s: %s at %s was refused (%r), so the reported range is not the "
                         "range the setter takes" % (spec["name"], label, value, error))
        return request_id
    problems.require(reply.get("ok") is not True,
                     "%s: %s at %s was ACCEPTED, so the reported %g..%g range is narrower than "
                     "the enforcement" % (spec["name"], label, value, spec["min"], spec["max"]))
    problems.require("outside the range %g..%g" % (spec["min"], spec["max"]) in error.get("message", ""),
                     "%s: the refusal for %s does not state the range plugin.param_get reported "
                     "(%g..%g): %r" % (spec["name"], label, spec["min"], spec["max"], error))
    problems.require(error.get("kind") == "invalid_args",
                     "%s: %s was refused with %r, not the typed invalid_args"
                     % (spec["name"], label, error.get("kind")))
    return request_id


def check_one_parameter(client, call, problems, host, effect, spec, first_id):
    """One parameter's contract: the setter enforces the range the getter
    reported, probed just-outside on BOTH sides and just-inside on both, by
    NAME and by INDEX. A boolean is additionally probed at a fractional value,
    because its accepted domain is two values, not a continuous range. Returns
    the next free request id and the (label, text) notes the caller prints -
    this module owns no transcript."""
    notes = []
    low, high = float(spec["min"]), float(spec["max"])
    step = float(spec.get("step") or 0.0) or (high - low) or 1.0
    where = {"target": host, "plugin": effect}
    for label, value, accepted in (("below min", low - step, False), ("above max", high + step, False),
                                   ("at min", low, True), ("at max", high, True)):
        check_edge(client, call, problems, where, spec, first_id, label, value, accepted)
        first_id += 1
    # The INDEX-addressed leg: the same parameter, the same bounds.
    by_index = _parameter_of(call(client, first_id + 1, "plugin.param_get", dict(where, index=spec["index"])))
    problems.require(by_index.get("min") == spec.get("min") and by_index.get("max") == spec.get("max"),
                     "%s: index %s reports %r..%r while the name leg reported %r..%r"
                     % (spec["name"], spec["index"], by_index.get("min"), by_index.get("max"),
                        spec.get("min"), spec.get("max")))
    if spec.get("type") == "boolean":
        notes.append(check_boolean_parameter(client, call, problems, where, spec, first_id + 2))
    return first_id + 3, notes


def check_boolean_parameter(client, call, problems, where, spec, first_id):
    """A boolean's enforced domain is the two values the engine can act on.
    `AutomatableModel::castValue<bool>` reads it as `std::round(v) != 0`, so a
    fractional value would be REPORTED as one thing and acted on as another -
    the contract lie BUG-CTL-5 fixes."""
    low, high = float(spec["min"]), float(spec["max"])
    fraction = low + (high - low) / 2.0
    reply = call(client, first_id, "plugin.param_set", dict(where, name=spec["name"], value=fraction))
    problems.require(reply.get("ok") is not True,
                     "%s: the fractional value %g was ACCEPTED on a boolean; the engine reads it "
                     "as round(%g) != 0, so the reply would name a value it never acts on"
                     % (spec["name"], fraction, fraction))
    problems.require("not a boolean" in (reply.get("error") or {}).get("message", ""),
                     "%s: the refusal of %g does not say the parameter is a boolean: %r"
                     % (spec["name"], fraction, (reply.get("error") or {}).get("message")))
    ok_result(call(client, first_id + 1, "plugin.param_set",
                     dict(where, name=spec["name"], value=0)), first_id + 1)
    read_back = _parameter_of(call(client, first_id + 2, "plugin.param_get",
                                  dict(where, name=spec["name"])))
    problems.require(read_back.get("value") in (0, 1),
                     "%s: a boolean reported the value %r; the engine's own reading of it is "
                     "castValue<bool>, which is 0 or 1" % (spec["name"], read_back.get("value")))
    return ("boolean %s" % spec["name"],
            "range %g..%g, fractional %g -> %s, value read back after setting 0: %r"
            % (low, high, fraction, (reply.get("error") or {}).get("message"),
               read_back.get("value")))


def the_parameter_metadata_is_the_enforcement(client, call, problems, report):
    """Section E - BUG-CTL-5. For EVERY parameter of the built-in amplifier, the
    min/max plugin.param_get reports must be the bounds plugin.param_set
    enforces, on both sides, by name and by index - and a boolean must take the
    two values the engine can act on."""
    host = ok_result(call(client, 600, "track.add", {"type": "instrument"}), 600)["track"]
    catalogue = ok_result(call(client, 601, "plugin.list",
                                 {"kind": "effect", "loadable_only": True}), 601)
    device = amplifier_device(catalogue)
    if device is None:
        problems.add("section E could not run: plugin.list offered no 'amplifier' device")
        return
    effect = ok_result(call(client, 602, "plugin.load",
                              {"target": host, "device": device["id"]}), 602)["id"]
    specs = declared_parameters(client, call, host, effect, 610)
    problems.require(len(specs) > 1,
                     "the amplifier reported %d parameters; the amplifier's own metadata "
                     "(enabled, wet/dry, decay, sync, volume) is what this section exists for"
                     % len(specs))
    request_id = 700
    for spec in specs:
        request_id, notes = check_one_parameter(client, call, problems, host, effect, spec, request_id)
        for label, text in notes:
            report(label, text)
    report("amplifier parameters", "%d checked over %d..%d: %s"
           % (len(specs), 610, 610 + len(specs) - 1,
              ", ".join("%s=%g..%g(%s)" % (s["name"], s["min"], s["max"], s["type"])
                        for s in specs)))
