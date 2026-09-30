#!/usr/bin/env python3
"""END-TO-END proof of the cycle-permitted signal-graph submode (feedback.*, #709).

THE CLAIM UNDER TEST (board card #709; scope contract: engine + command group +
registered proof + a limitations line): the mixer's acyclicity rule becomes
OPTIONAL behind an explicit submode - while `feedback.enable` is in force a
`bus -> effect -> same bus` send is accepted, every point of use says
compensation is SUSPENDED quoting REAPER's warning, and leaving removes the loop
sends, restores normal PDC and refuses the loop again.

WHY A CTEST AND NOT A UNIT TEST. PdcMixerTest.cpp proves the compensation
arithmetic and control-routing-commands.py proves the default-mode refusal;
neither proves card #709's acceptance ON THE WIRE - the refusal flipping to
acceptance only inside the submode, the suspension stated where the write
happens, and the negative control for "no silent timing misalignment for a
user who never enabled the mode": the default-mode `pdc.report`,
`mixer.get_state` and saved bytes equal to the baseline CAPTURED FROM THE
PRE-CHANGE BUILD (docs/709-logs/709-baseline-prechange.md, tip 4ef3065fa),
then equal AGAIN, byte for byte, after disable.

WHAT IT ASSERTS, in numbers: default mode refuses the closing send typed,
writing nothing; the fixture's pdc.report / mixer.get_state / saved bytes match
the pre-change baseline; feedback.enable says pdc_suspended=true with REAPER's
warning ("can risk damaging audio equipment"); only then does the closing send
succeed, its result saying feedback=true, pdc_suspended=true with the warning;
feedback.get_state lists it at compensation_frames=0 (cross-checked in
pdc.report, the NON-loop send on its baseline value); feedback.disable reports
what it removed, all three baselines return BYTE FOR BYTE, the send is refused
again typed, one control.undo restores mode and send as ONE step, and
control.transactions records the feedback verbs as true_inverse.

Usage: QT_QPA_PLATFORM=offscreen python3 control-feedback-commands.py <zene> [--capture]
    (--capture = the fixture + baseline dump only: PRE-CHANGE builds run nothing else)

Exit codes: 0 every check held; 1 a check failed; 2 no binary at the path.
"""

from __future__ import annotations

import hashlib, json, os, sys  # noqa: E401  (one line: the file-length ratchet is at 500)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

#: The built-in effect inside the loop; a module this build ships (the same
#: pick control-routing-commands.py makes, so both proofs need one plugin).
PREFERRED_EFFECT = "amplifier"
#: The A16 record a mutating feedback verb must leave behind.
TRUE_INVERSE_RECORD = ("true_inverse", True, True)
#: REAPER's warning, carried verbatim by feedback.enable, feedback.get_state
#: and the result that writes a loop-closing send (REAPER User Guide, main
#: changes 6.66-6.70).
REAPER_WARNING = ("feedback routing can in some instances be useful, but can "
                  "risk damaging audio equipment (REAPER User Guide, main "
                  "changes 6.66-6.70)")

#: Baselines: pdc/mixer JSON + save sha256 captured at 4ef3065fa (pre-change,
#: both still those values). The save sha ROTATED three times (see the rotation
#: doc): docs/709-logs/709-baseline-rotate.md; record: 709-baseline-prechange.md.
BASELINE_PDC = '''{"channel_count":3,"channels":[{"chain_latency_frames":0,"id":"ch-1","index":0,"input_latency_frames":0,"is_bus":false,"is_master":true,"muted":false,"name":"Master","send_count":0,"sends":[],"sidechain_receive_count":0,"sidechain_send_count":0,"sidechain_sends":[],"volume":1},{"chain_latency_frames":0,"id":"ch-9","index":1,"input_latency_frames":0,"is_bus":true,"is_master":false,"muted":false,"name":"Bus 1","send_count":2,"sends":[{"amount":1,"compensation_frames":0,"from":"ch-9","pre_fader":false,"to":"ch-1"},{"amount":1,"compensation_frames":0,"from":"ch-9","pre_fader":false,"to":"ch-10"}],"sidechain_receive_count":0,"sidechain_send_count":0,"sidechain_sends":[],"volume":1},{"chain_latency_frames":0,"id":"ch-10","index":2,"input_latency_frames":0,"is_bus":false,"is_master":false,"muted":false,"name":"Channel 2","send_count":1,"sends":[{"amount":1,"compensation_frames":0,"from":"ch-10","pre_fader":false,"to":"ch-1"}],"sidechain_receive_count":0,"sidechain_send_count":0,"sidechain_sends":[],"volume":1}],"delay_line_capacity_frames":16384,"delay_line_clamped":false,"note":"total_latency_frames is the delay from a source entering the mixer to the master output (Mixer::totalLatencyFrames); input_latency_frames is the alignment point the mixer publishes per channel (Mixer::channelInputLatency). Both are recomputed once per period by Mixer::updateLatencyCompensation - no command sets them, and this command writes nothing","route_count":3,"routes":[{"amount":1,"compensation_frames":0,"from":"ch-9","pre_fader":false,"to":"ch-1"},{"amount":1,"compensation_frames":0,"from":"ch-10","pre_fader":false,"to":"ch-1"},{"amount":1,"compensation_frames":0,"from":"ch-9","pre_fader":false,"to":"ch-10"}],"sidechain":{"count":0,"note":"sidechain sends ARE in this engine (Mixer::createSidechainSend, src/core/Mixer.cpp); create or adjust one with mixer.sidechain_to and read its tap and compensation here","routes":[],"supported":true,"tap_points":["post_fader","pre_fx","pre_fader","post_fader_no_gain"]},"total_latency_frames":0,"track_input_count":4,"track_inputs":[{"channel":"ch-1","latency_frames":0,"name":"Default preset"},{"channel":"ch-1","latency_frames":0,"name":"TripleOscillator"},{"channel":"ch-1","latency_frames":0,"name":"Sample track"},{"channel":"ch-1","latency_frames":0,"name":"Kicker"}]}'''
BASELINE_MIXER = '''{"channels":[{"id":"ch-1","index":0,"is_bus":false,"is_master":true,"muted":false,"name":"Master","pan":0,"sends":[],"soloed":false,"volume":1},{"id":"ch-9","index":1,"is_bus":true,"is_master":false,"muted":false,"name":"Bus 1","pan":0,"sends":[{"amount":1,"pre_fader":false,"to":"ch-1"},{"amount":1,"pre_fader":false,"to":"ch-10"}],"soloed":false,"volume":1},{"id":"ch-10","index":2,"is_bus":false,"is_master":false,"muted":false,"name":"Channel 2","pan":0,"sends":[{"amount":1,"pre_fader":false,"to":"ch-1"}],"soloed":false,"volume":1}],"count":3}'''
BASELINE_SAVE_SHA256 = "5131ec2c53891bd7b301610ad81e55db3733c5603e8439afaf05a99edb1c9140"


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


def canonical(payload):
    """One deterministic text for a JSON payload - the byte-for-byte comparison."""
    return json.dumps(payload, sort_keys=True, separators=(",", ":"))


def channel_named(state, wanted):
    for channel in state.get("channels") or []:
        if channel.get("id") == wanted:
            return channel
    return None


def send_to(channel, dest):
    for send in (channel or {}).get("sends") or []:
        if send.get("to") == dest:
            return send
    return None


def pdc_route(report, source, dest):
    for route in report.get("routes") or []:
        if route.get("from") == source and route.get("to") == dest:
            return route
    return None


def loadable_effects(catalogue):
    """The loadable built-in effects of plugin.list, preferred one first."""
    entries = [entry for entry in catalogue.get("devices") or []
               if entry.get("kind") == "effect" and entry.get("loadable") is True]
    preferred = [entry for entry in entries if entry.get("name") == PREFERRED_EFFECT]
    rest = [entry for entry in entries if entry.get("name") != PREFERRED_EFFECT]
    return preferred + rest


def save_path_for(binary):
    """A FIXED project path both builds save to - the string may be embedded
    in the file, so the two runs must write the same bytes at the same path."""
    here = os.path.dirname(os.path.abspath(binary))
    tests_dir = os.path.join(here, "tests")
    os.makedirs(tests_dir, exist_ok=True)
    return os.path.join(tests_dir, "709-feedback-fixture.mmpz")


def canonical_project_sha(path):
    """sha256 of canonical_project_xml(path)."""
    return hashlib.sha256(canonical_project_xml(path)).hexdigest()


def canonical_project_xml(path):
    """The saved project's XML in CANONICAL form (the bytes the baseline hashes).

    Drops instance/build/journal/window-LAYOUT metadata - writer,
    creatorversion, the <z:provenance> journal (wall-clock stamps), the ONE
    <z:index> row that restates it, the GUI view sections and the window-geometry
    attrs; none of it is project content - and sorts attributes. Rotations of the
    constant: docs/709-logs/709-baseline-rotate.md."""
    import xml.etree.ElementTree as ET
    import zlib

    with open(path, "rb") as handle:
        root = ET.fromstring(zlib.decompress(handle.read()[4:]))

    def clean(element):
        for v in ("writer", "creatorversion", "creatorplatform", "creatorplatformtype",
                  "x", "y", "width", "height", "maximized", "visible"):
            element.attrib.pop(v, None)
        [element.remove(c) for c in list(element) if c.tag.endswith("}provenance") or c.tag.endswith("}section") and c.get("name") == "z:provenance"
         or c.tag in ("ControllerRackView", "automationeditor", "pianoroll", "projectnotes",
                      "timeline", "automationtrack")]  # reasons: docs/SAVE-CANONICAL-STABILITY.md 4
        items = sorted(element.attrib.items())
        element.attrib.clear()
        element.attrib.update(items)
        for child in element:
            clean(child)

    clean(root)
    return ET.tostring(root)


def save_and_hash(session, path):
    """project.save to `path` and the canonical sha256 of the saved content."""
    saved = session.result("project.save", {"path": path})
    if saved.get("saved") is not True:
        return None, saved
    return canonical_project_sha(path), saved


def capture_bundle(session, binary):
    """The three baseline measurements of the fixture, in canonical text."""
    pdc = canonical(session.result("pdc.report"))
    mixer = canonical(session.result("mixer.get_state"))
    digest, saved = save_and_hash(session, save_path_for(binary))
    return {"pdc": pdc, "mixer": mixer, "save_sha256": digest, "save": saved}


# ---------------------------------------------------------------------------
# the fixture: a bus, an effect inside the loop, one send - and the refused
# closing send. IDENTICAL in capture mode and in the full run, so the
# baseline comparison is a like-for-like measurement.
# ---------------------------------------------------------------------------


def build_fixture(session, recorder, capture):
    """Builds bus -> effect -> return -> (refused) -> bus; returns the ids."""
    bus = session.result("bus.create")
    bus_id = bus.get("channel")
    ret = session.result("mixer.add_channel", {})
    ch_id = ret.get("channel")
    if not bus_id or not ch_id:
        H.fail("the fixture channels were not created (bus=%r channel=%r)" % (bus, ret),
               None, None)
    effects = loadable_effects(session.result("plugin.list"))
    loaded = _load_effect(session, effects, bus_id)
    where = "bus"
    if not loaded and ch_id:
        loaded = _load_effect(session, effects, ch_id)
        where = "channel"
    edge = session.result("mixer.send_to", {"channel": bus_id, "to": ch_id,
                                            "amount": 1.0})
    closing = session.typed_error("mixer.send_to", {"channel": ch_id, "to": bus_id,
                                                    "amount": 0.7})
    if not capture:
        _check_fixture(session, recorder, bus_id, ch_id, edge, closing, where, loaded)
    return {"bus": bus_id, "return": ch_id, "effect_where": where,
            "loaded": loaded}


def _load_effect(session, effects, target):
    for entry in effects[:1]:
        if not target:
            return []
        reply = session.result("plugin.load", {"target": target,
                                               "device": entry.get("id")})
        if reply.get("id"):
            return [reply["id"]]
    return []


def _check_fixture(session, recorder, bus_id, ch_id, edge, closing, where, loaded):
    """The default-mode half of the fixture, measured."""
    chain = session.result("routing.get_state", {"target": bus_id if where == "bus" else ch_id})
    recorder.check("the loop runs through a loaded effect (on the %s)" % where,
                   bool(loaded) and (chain.get("chain") or {}).get("effect_count") == 1,
                   "loaded=%r chain=%r" % (loaded, chain.get("chain")))
    recorder.check("the first send of the loop is written normally",
                   edge.get("amount") == 1.0 and edge.get("created") is True
                   and edge.get("feedback") is False,
                   "edge=%r" % (edge,))
    recorder.check("NEGATIVE CONTROL: default mode refuses the closing send, typed",
                   closing.get("kind") == "refused"
                   and "feedback" in (closing.get("message") or ""),
                   "error=%r" % (closing,))
    recorder.check("the refused write left no send behind",
                   send_to(channel_named(session.result("mixer.get_state"), ch_id),
                           bus_id) is None,
                   "state=%r" % (session.result("mixer.get_state"),))


# ---------------------------------------------------------------------------
# the baseline comparison: a normal project's PDC and mixer state, byte for
# byte, against the capture from the pre-change build.
# ---------------------------------------------------------------------------


def check_baselines(session, recorder, binary, stage):
    """pdc.report, mixer.get_state and the saved project against the baseline."""
    bundle = capture_bundle(session, binary)
    recorder.check("%s: pdc.report equals the pre-change baseline byte for byte"
                   % stage, bundle["pdc"] == BASELINE_PDC,
                   "expected=%r seen=%r" % (BASELINE_PDC[:120], bundle["pdc"][:120]))
    recorder.check("%s: mixer.get_state equals the pre-change baseline byte for byte"
                   % stage, bundle["mixer"] == BASELINE_MIXER,
                   "expected=%r seen=%r" % (BASELINE_MIXER[:120], bundle["mixer"][:120]))
    recorder.check("%s: the saved project equals the pre-change bytes (sha256)"
                   % stage, bundle["save_sha256"] == BASELINE_SAVE_SHA256,
                   "expected=%r seen=%r save=%r"
                   % (BASELINE_SAVE_SHA256, bundle["save_sha256"], bundle["save"]))
    if bundle["save_sha256"] != BASELINE_SAVE_SHA256 and bundle["save"].get("file"):
        # Evidence for a platform difference (macOS differed on hosted run 36720131455): the
        # canonical XML the hash is taken over, to diff against the same text from a passing box.
        print("---- canonical XML of %s (%s) ----" % (bundle["save"]["file"], stage))
        print(canonical_project_xml(bundle["save"]["file"]).decode("utf-8", "replace")[:40000])
        print("---- end canonical XML ----")


# ---------------------------------------------------------------------------
# the submode itself
# ---------------------------------------------------------------------------


def check_disabled_state(session, recorder):
    state = session.result("feedback.get_state")
    recorder.check("feedback.get_state starts with the submode off",
                   state.get("enabled") is False
                   and state.get("pdc_suspended") is False
                   and state.get("feedback_routes") == [],
                   "state=%r" % (state,))


def check_enable(session, recorder):
    """feedback.enable states the suspension and carries REAPER's warning."""
    reply = session.result("feedback.enable")
    suspension = (reply.get("suspension") or "").lower()
    warning = reply.get("warning") or ""
    recorder.check("feedback.enable turns the submode on and says PDC is suspended",
                   reply.get("enabled") is True
                   and reply.get("pdc_suspended") is True
                   and "suspend" in suspension and "compensation" in suspension,
                   "reply=%r" % (reply,))
    recorder.check("feedback.enable carries REAPER's feedback warning verbatim",
                   warning == REAPER_WARNING, "warning=%r" % (warning,))
    return reply


def check_loop_accepted(session, recorder, fixture):
    """Acceptance (1)+(2): only inside the submode, and the write says so."""
    ch_id, bus_id = fixture["return"], fixture["bus"]
    made = session.result("mixer.send_to", {"channel": ch_id, "to": bus_id,
                                            "amount": 0.7})
    recorder.check("inside the submode the bus -> effect -> same bus send is accepted",
                   made.get("feedback") is True and made.get("created") is True
                   and made.get("pdc_suspended") is True
                   and made.get("warning") == REAPER_WARNING
                   and made.get("pre_fader") is True,
                   "made=%r" % (made,))
    state = session.result("mixer.get_state")
    send = send_to(channel_named(state, ch_id), bus_id)
    close_enough = abs(send.get("amount", 0) - 0.7) < 1e-6 if send else False
    recorder.check("the accepted send exists with the amount asked for",
                   send is not None and close_enough and send.get("pre_fader") is True,
                   "send=%r" % (send,))
    return made


def check_suspension_at_use(session, recorder, fixture):
    """The suspension is readable where PDC is read: compensation is 0 on the
    loop send, both in feedback.get_state and in pdc.report itself."""
    ch_id, bus_id = fixture["return"], fixture["bus"]
    state = session.result("feedback.get_state")
    listed = state.get("feedback_routes") or []
    mine = [r for r in listed if (r.get("from"), r.get("to")) == (ch_id, bus_id)]
    head = {"enabled": state.get("enabled"), "pdc_suspended": state.get("pdc_suspended")}
    on_wire = pdc_route(session.result("pdc.report"), ch_id, bus_id)
    got = head == {"enabled": True, "pdc_suspended": True} and len(listed) == 1
    got = got and len(mine) == 1 and mine[0].get("compensation_frames") == 0
    recorder.check("feedback.get_state lists the loop send with compensation 0",
                   got, "state=%r" % (state,))
    wire = on_wire is not None and on_wire.get("compensation_frames") == 0
    recorder.check("pdc.report shows the loop send at compensation 0 (suspended)",
                   wire, "route=%r" % (on_wire,))


def check_normal_pdc_unchanged(session, recorder, fixture):
    """Everything OUTSIDE the loop equals the baseline, number for number."""
    report = session.result("pdc.report")
    base = json.loads(BASELINE_PDC)
    baseline_route = pdc_route(base, fixture["bus"], fixture["return"])
    current_route = pdc_route(report, fixture["bus"], fixture["return"])
    same_route = current_route is not None and current_route == baseline_route
    recorder.check("the NON-loop send keeps exactly its baseline compensation",
                   same_route, "baseline=%r current=%r" % (baseline_route, current_route))
    total = report.get("total_latency_frames") == base.get("total_latency_frames")
    recorder.check("total_latency_frames of the normal paths equals the baseline",
                   total, "total=%r" % (report.get("total_latency_frames"),))


def check_disable(session, recorder, fixture):
    """Acceptance (3)+(4): leaving removes the loop send and says so."""
    reply = session.result("feedback.disable")
    want = {"enabled": False, "pdc_suspended": False, "removed": 1}
    got = {key: reply.get(key) for key in want}
    mine = [r for r in reply.get("feedback_routes") or []
            if (r.get("from"), r.get("to")) == (fixture["return"], fixture["bus"])]
    amount_ok = len(mine) == 1 and abs(mine[0].get("amount", 0) - 0.7) < 1e-6
    ok = got == want and amount_ok
    ok = ok and "normal" in (reply.get("note") or "").lower()
    recorder.check("feedback.disable leaves the submode and reports what it removed",
                   ok, "reply=%r" % (reply,))
    return reply


def check_refused_again(session, recorder, fixture):
    ch_id, bus_id = fixture["return"], fixture["bus"]
    back = session.typed_error("mixer.send_to", {"channel": ch_id, "to": bus_id,
                                                 "amount": 0.7})
    recorder.check("after leaving, the same loop send is refused again, typed",
                   back.get("kind") == "refused"
                   and "feedback" in (back.get("message") or "")
                   and send_to(channel_named(session.result("mixer.get_state"), ch_id),
                               bus_id) is None,
                   "error=%r" % (back,))


def check_undo_of_disable(session, recorder, fixture):
    """One control.undo puts the submode back with the send restored (the
    step on top of the stack IS the disable: refusals record nothing and the
    baseline saves come earlier)."""
    undone = session.result("control.undo")
    if undone.get("undone_command") != "feedback.disable":
        # Defensive: pop any later recorded step (a save, say) and retry once.
        undone = session.result("control.undo")
    state = session.result("feedback.get_state")
    restored = pdc_route(session.result("pdc.report"), fixture["return"], fixture["bus"])
    good = undone.get("undone_command") == "feedback.disable"
    good = good and state.get("enabled") is True and state.get("pdc_suspended") is True
    good = good and restored is not None and restored.get("compensation_frames") == 0
    good = good and abs(restored.get("amount", 0) - 0.7) < 1e-6
    recorder.check("control.undo of the disable restores mode and send as one step",
                   good, "undone=%r state=%r route=%r" % (undone, state, restored))


def check_transactions(session, recorder):
    records = [record for record in
               session.result("control.transactions").get("transactions") or []
               if record.get("command") in ("feedback.enable", "feedback.disable")]
    signatures = {(record.get("class"), record.get("reversible"),
                   bool(record.get("mechanism"))) for record in records}
    recorder.check("both feedback verbs record an A16 true_inverse transaction",
                   len(records) >= 2 and signatures == {TRUE_INVERSE_RECORD},
                   "records=%r" % (records,))


def check_quit(session, instance, recorder):
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        recorder.check("control.quit answered", False, "%r" % reply)
        return
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    recorder.check("control.quit stops the instance", exited and code == 0,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))


def run_capture(session, binary, recorder, fixture):
    """Capture mode: the fixture and the three baseline measurements, no assertions."""
    bundle = capture_bundle(session, binary)
    bundle["fixture"] = fixture
    print("BASELINE-BEGIN")
    print(json.dumps(bundle, sort_keys=True, indent=2))
    print("BASELINE-END")
    return 0


def report(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-70s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)
    problems = recorder.problems()
    if problems:
        print("")
        print("FAIL: the feedback submode proof has %d failed check(s)" % len(problems))
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
    capture = "--capture" in argv[2:]
    recorder = Recorder()
    transcript = H.Transcript()
    with H.start_instance(argv[1]) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        print("instance: %s" % argv[1])
        print("socket:   %s" % instance.socket_path)
        fixture = build_fixture(session, recorder, capture)
        if capture:
            code = run_capture(session, argv[1], recorder, fixture)
            session.result("control.quit")
            transcript.dump()
            return code
        check_baselines(session, recorder, argv[1], "default mode")
        check_disabled_state(session, recorder)
        check_enable(session, recorder)
        check_loop_accepted(session, recorder, fixture)
        check_suspension_at_use(session, recorder, fixture)
        check_normal_pdc_unchanged(session, recorder, fixture)
        check_disable(session, recorder, fixture)
        check_refused_again(session, recorder, fixture)
        check_undo_of_disable(session, recorder, fixture)
        # Leave again for the final baselines: the undo above restored the
        # loop, and the post-disable measurement must see the normal state.
        check_disable(session, recorder, fixture)
        check_baselines(session, recorder, argv[1], "after disable")
        check_transactions(session, recorder)
        check_quit(session, instance, recorder)
    transcript.dump()
    code = report(recorder)
    if code == 0:
        H.ok("feedback submode socket proof (every check held)")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
