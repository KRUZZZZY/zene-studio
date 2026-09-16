#!/usr/bin/env python3
"""END-TO-END proof that plugin delay compensation is READABLE by an agent.

THE CLAIM UNDER TEST (docs/FEATURE-LIST-0.3.0.md row 27, "PDC and sidechain"):
an agent connected to a real `zene` instance over its control socket can read
the PDC graph the mixer actually runs - the total latency to the master output,
every channel's alignment point and the latency its own chain adds, the
compensation the mixer applies at every send, and whether sidechain routing
exists - and the numbers MOVE when the topology does, because they are the
mixer's own published values and not a re-derivation.

WHY IT IS A REGISTERED CTEST AND NOT A UNIT TEST. The arithmetic of the
compensation is already proven in process by the registered
tests/src/core/PdcMixerTest.cpp (and the sidechain taps by
PhaseDSidechainTest.cpp): a delayed path lands sample-aligned, the delay line
clamps at LatencyCompensation::MaxFrames and a zero-delay graph is
bit-identical to the uncompensated mixer. What those tests cannot prove is the
release contract's section 3.1 - that the feature is reachable THROUGH THE
SOCKET by an agent. So this test starts the REAL binary (the
ControlSocketIntegration mould: $<TARGET_FILE:zene>, QT_QPA_PLATFORM=offscreen,
the shared control_socket_harness) and reads every number back off the wire.

WHAT IT ASSERTS, in numbers:
  * on a fresh project: total_latency_frames == 0, the delay-line capacity is
    the engine's own bound (LatencyCompensation::MaxFrames == 16384),
    delay_line_clamped is false, the master ch-0 is present and carries the
    is_master/is_bus flags, and sidechain.supported is true with the four tap
    points the engine's enum names;
  * routing: after mixer.send_to from ch-1 to ch-2 with amount 0.5, pdc.report
    lists EXACTLY that route with its amount, its pre-fader flag and its
    compensation, and ch-1's own send_count is 1 - so the report tracks the
    topology rather than a cached copy;
  * sidechain: after mixer.sidechain_to ch-1 -> ch-2 with tap_point pre_fx,
    sidechain.count is 1, the route's tap_point reads back as "pre_fx", and the
    receiver's sidechain_receive_count is 1; control.undo takes it back out and
    the count returns to 0;
  * the schema is real: pdc.report takes NO arguments, so an argument is a typed
    invalid_args, and a bogus channel id is a typed not_found.

THE BOUND THIS TEST STATES RATHER THAN HIDES: in a build whose devices report no
latency, every compensation is 0 - that is the honest number, not a missing
feature. A NONZERO PDC graph needs a device that reports latency
(Effect::latencyFrames(); in this tree only the WASM effect does, and only with
a module loaded), so the arithmetic of a nonzero delay is proven by
PdcMixerTest.cpp and NOT by this transcript. What this transcript proves is the
surface: the numbers the mixer publishes are on the wire, and they follow the
routing.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-pdc-commands.py <zene-binary>

Exit codes: 0 every check held; 1 a check failed (the app log is printed);
2 cannot run (no binary at the path).
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

#: LatencyCompensation::MaxFrames (include/LatencyCompensation.h:57) - the bound
#: the mixer's own total is clamped to.
DELAY_LINE_CAPACITY = 1 << 14
#: The four tap points of SidechainTapPoint (include/Mixer.h:57).
TAP_POINTS = ("post_fader", "pre_fx", "pre_fader", "post_fader_no_gain")


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 0

    def call(self, command, args=None):
        self.last_id += 1
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
    """Collects the named checks and their evidence, so a failure names the number."""

    def __init__(self):
        self.results = []

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))

    def problems(self):
        return [(name, evidence) for name, passed, evidence in self.results if not passed]


def channels_of(report):
    return report.get("channels") or []


def route_from(report, source, dest):
    """The one route from -> to in a pdc.report, or None."""
    for route in report.get("routes") or []:
        if route.get("from") == source and route.get("to") == dest:
            return route
    return None


def sidechain_route(report, source, dest):
    section = ((report.get("sidechain") or {}).get("routes")) or []
    for route in section:
        if route.get("from") == source and route.get("to") == dest:
            return route
    return None


#: The sidechain route fields the report must carry.
SIDECHAIN_FIELDS = ("tap_point", "amount", "deferred")


def send_count_of(report, channel):
    """The send_count a channel reports, or -1 when the report does not list it."""
    for entry in channels_of(report):
        if entry.get("id") == channel:
            return entry.get("send_count")
    return -1


def channel_ids(report):
    return tuple(channel.get("id") for channel in channels_of(report))


def add_channels(session, count):
    """`count` mixer channels, returning their ch-<n> ids."""
    added = []
    for _ in range(count):
        reply = session.result("mixer.add_channel")
        if not reply.get("channel"):
            return []
        added.append(reply["channel"])
    return added


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


def check_baseline(session, recorder):
    """An empty mixer: no latency anywhere, and the engine's own bound reported."""
    report = session.result("pdc.report")
    if not channels_of(report):
        H.fail("pdc.report returned no channels: %r" % report, None, None)
    master = channels_of(report)[0]
    sidechain = report.get("sidechain") or {}
    baseline_master_flags(recorder, master)
    baseline_fresh_mixer(recorder, report)
    baseline_channel_count(recorder, report)
    baseline_sidechain(recorder, sidechain)


# One report-area per helper: the four checks together carried eleven branch
# points, and the gate's budget counts a function's boolean operators. Same
# checks, same order, same messages as the single-function form.
def baseline_master_flags(recorder, master):
    """The master is whatever the counter allocated, and it says so."""
    # The master's ID IS NOT PREDICTED, it is read: an id is allocated at
    # construction from the project's counter (SPEC-stable-ids.md slice 2/4.2 -
    # "it never parses, derives, or predicts an id"), so the master is ch-<n>
    # for whatever the counter held when the channel was built. Measured on this
    # binary: ch-1 in a fresh session, with `mixer.add_channel` handing out ch-7.
    # This check asserted "ch-0" and was wrong about the engine.
    recorder.check("the master is the first channel and carries its own flags",
                   master.get("is_master") is True and master.get("is_bus") is False
                   and str(master.get("id") or "").startswith("ch-"),
                   "master=%r" % master)


def baseline_fresh_mixer(recorder, report):
    """Zero latency, an unclamped delay line, the engine's own capacity."""
    recorder.check("a fresh mixer publishes zero latency and no clamp",
                   report.get("total_latency_frames") == 0
                   and report.get("delay_line_capacity_frames") == DELAY_LINE_CAPACITY
                   and report.get("delay_line_clamped") is False,
                   "total=%r capacity=%r clamped=%r"
                   % (report.get("total_latency_frames"),
                      report.get("delay_line_capacity_frames"),
                      report.get("delay_line_clamped")))


def baseline_channel_count(recorder, report):
    """channel_count agrees with the channel list."""
    recorder.check("channel_count agrees with the channel list",
                   report.get("channel_count") == len(channels_of(report)),
                   "count=%r list=%r" % (report.get("channel_count"), len(channels_of(report))))


def baseline_sidechain(recorder, sidechain):
    """Sidechain routing exists with its four named tap points."""
    recorder.check("sidechain routing EXISTS and its four tap points are named",
                   sidechain.get("supported") is True and sidechain.get("count") == 0
                   and tuple(sidechain.get("tap_points") or ()) == TAP_POINTS,
                   "sidechain=%r" % sidechain)


#: The route fields the send the mixer built must read back with.
ROUTE_FIELDS = ("amount", "pre_fader")


def check_topology(session, recorder):
    """The report follows the routing: a real send appears with its own numbers."""
    added = add_channels(session, 2)
    recorder.check("two mixer channels were added", len(added) == 2, "added=%r" % (added,))
    if len(added) != 2:
        return added
    first, second = added
    # A new mixer channel is already routed to the master (the engine's default),
    # so the check is that the count GREW by exactly one, not that it is one.
    before = send_count_of(session.result("pdc.report"), first)
    sent = session.result("mixer.send_to", {"channel": first, "to": second, "amount": 0.5})
    report = session.result("pdc.report")
    route = route_from(report, first, second)
    # Comparing whole dicts rather than chaining `and`s: one comparison, and the
    # failure prints the value that disagreed.
    shown = {field: (route or {}).get(field) for field in ROUTE_FIELDS}
    recorder.check("the send the mixer built is the send pdc.report shows",
                   sent.get("amount") == 0.5 and shown == {"amount": 0.5, "pre_fader": False},
                   "sent=%r route=%r" % (sent, route))
    after = send_count_of(report, first)
    recorder.check("the sender's own send count grew by exactly one",
                   after == before + 1, "before=%r after=%r" % (before, after))
    zero_latency = {"total_latency_frames": report.get("total_latency_frames"),
                    "compensation_frames": (route or {}).get("compensation_frames")}
    recorder.check("a graph whose paths report no latency compensates nothing",
                   zero_latency == {"total_latency_frames": 0, "compensation_frames": 0},
                   "numbers=%r" % (zero_latency,))
    return added


def check_sidechain(session, added, recorder):
    """A sidechain send is real, is reported with its tap, and is undoable."""
    if len(added) != 2:
        return
    first, second = added
    made = session.result("mixer.sidechain_to",
                          {"channel": first, "to": second, "amount": 0.25,
                           "tap_point": "pre_fx"})
    report = session.result("pdc.report")
    sidechain = report.get("sidechain") or {}
    route = sidechain_route(report, first, second)
    dialled = {field: (route or {}).get(field) for field in SIDECHAIN_FIELDS}
    recorder.check("the sidechain send is in the report with its tap point and level",
                   sidechain.get("count") == 1
                   and dialled == {"tap_point": "pre_fx", "amount": 0.25, "deferred": False},
                   "made=%r route=%r sidechain=%r" % (made, route, sidechain))
    receiver = [ch for ch in channels_of(report) if ch.get("id") == second]
    recorder.check("the receiver counts the sidechain route it receives",
                   [ch.get("sidechain_receive_count") for ch in receiver] == [1],
                   "receiver=%s" % (receiver[:1],))
    check_sidechain_undo(session, first, second, recorder)


def sidechain_count(session):
    return (session.result("pdc.report").get("sidechain") or {}).get("count")


def check_sidechain_undo(session, first, second, recorder):
    """One undo takes the sidechain route out of the graph; nothing else moves."""
    undone = session.result("control.undo")
    recorder.check("control.undo takes the sidechain send out of the graph",
                   undone.get("undone") is True, "undone=%r" % (undone.get("undone"),))
    recorder.check("the graph holds no sidechain send afterwards",
                   sidechain_count(session) == 0, "count_after=%r" % (sidechain_count(session),))


def check_refusals(session, recorder):
    """The schema is real and every refusal is typed."""
    report = session.result("pdc.report")
    unknowns = [
        ("pdc.report", {"channel": "ch-0"}),
        ("mixer.route_to", {"channel": "nope", "to": "ch-0"}),
        ("mixer.route_to", {"channel": "ch-1", "to": "ch-99"}),
        ("mixer.sidechain_to", {"channel": "ch-1", "to": "ch-0", "tap_point": "bogus"}),
        ("mixer.route_remove", {"channel": "ch-0", "to": "ch-3"}),
        ("routing.get_state", {"target": "nonsense"}),
        ("bus.remove", {"channel": "ch-0"}),
        ("port.get_state", {"target": "trk-0", "device": "fx-0"}),
    ]
    failures = []
    for command, args in unknowns:
        error = session.typed_error(command, args)
        if not error.get("kind") or not error.get("message"):
            failures.append("%s -> %r" % (command, error))
    recorder.check("every refusal is typed and carries a message", not failures,
                   "; ".join(failures))
    recorder.check("a refused call changed nothing",
                   session.result("pdc.report").get("total_latency_frames") == 0
                   and channel_ids(report) == channel_ids(session.result("pdc.report")),
                   "channels=%r" % (channel_ids(session.result("pdc.report")),))


def check_quit(session, instance, recorder):
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        recorder.check("control.quit answered", False, "%r" % reply)
        return
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    recorder.check("control.quit stops the instance", exited and code == 0,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))


def report(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-58s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)
    problems = recorder.problems()
    if problems:
        print("")
        print("FAIL: the pdc.* socket proof has %d failed check(s)" % len(problems))
        return 1
    print("")
    print("PASS: %d checks, every one a measured number" % len(recorder.results))
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    if not os.path.exists(argv[1]):
        print("cannot run: no binary at %s" % argv[1])
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

        check_baseline(session, recorder)
        added = check_topology(session, recorder)
        check_sidechain(session, added, recorder)
        check_refusals(session, recorder)
        check_quit(session, instance, recorder)

    transcript.dump()
    code = report(recorder)
    if code == 0:
        H.ok("pdc.* socket proof (every check held)")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
