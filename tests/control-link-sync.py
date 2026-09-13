#!/usr/bin/env python3
"""The two-instance proof for Zene Studio's session sync (D11 "Ableton Link
sync"), driven END TO END through the control socket, with two real binaries.

THE CLAIM UNDER TEST:

    two Zene instances on one box join ONE session, and one of them drives the
    other's tempo and beat phase without any relay between them.

WHAT IS SYNCING, SAID PLAINLY BEFORE ANY NUMBER BELOW. The session being driven
is `zene-link-style` - Ableton Link's SEMANTICS (a shared session tempo, a shared
beat phase on one timeline, a quantum, a peer set) implemented in this tree, over
UDP multicast on 224.76.78.75:20808, the group Link itself uses for discovery. It
is NOT the Ableton Link library: that library is not vendored in this build, and
`link.get_state` says so in its own `interop` block. The licence finding that
made that a SCOPE decision rather than a legal one is recorded in
docs/LINK-SYNC.md section 1: Ableton Link's LICENSE.md is GPL-2.0-or-later (its
"proprietary" line is an offer of a separate commercial licence, not a second arm
of the one it grants), so vendoring it is permitted for this GPL-2.0-or-later
product - a follow-up lane, not a blocked one. This transcript therefore proves
the ENGINE-SIDE sync and its wire, and no assertion here is about interoperation
with a third-party Link application.

THE STEPS, and what each one is evidence OF:

  1. Two real instances start headless with their own --control-socket, and both
     report `transport.available: true` and an empty session. If the transport
     cannot start on this host the run exits 77 (ctest "Skipped"), never 0 and
     never "Passed".
  2. Both join (`link.set_enabled true`). Each instance then SEES THE OTHER as a
     peer with the other's own peer id - not a broadcast that nobody hears.
  3. The two converge on ONE session tempo and ONE revision owner without any
     client relaying between them: whoever declared the newer revision wins, on
     both sides. Which one that is is reported, not assumed.
  4. THE HEADLINE: instance A declares a session tempo (`link.set_session_tempo`)
     and B - which is driven by nothing but its socket - reports that tempo in
     both its session state AND its engine (`engine_tempo`), with A as the
     revision owner. Measured by polling B; the assertion is on B's state, never
     on the command's own reply.
  5. The same drive through an ORDINARY tempo change: A's `transport.set_tempo`
     reaches B with no sync-specific call at all, because the model watches the
     engine's own tempo for a change it did not itself make.
  6. PHASE: A's and B's shared `beat` are compared against the elapsed wall time
     between the two reads and the session tempo. They must agree to within a
     small fraction of a beat - a model that copied a peer's beat without
     advancing it by the datagram's transit time could not pass this, and the
     transcript prints both samples.
  7. Both instances are publishing (their `published_count` grows between two
     samples) - a session with no announcements is not a session.
  8. B leaves: B's session empties, and A's peer table drops B within the
     documented timeout, while the tempo A is playing does not move. Leaving is
     not the same as resetting, and this is where that is measured.
  9. Both instances quit through their normal shutdown, with exit code 0 and no
     last-resort guard line in either log.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-link-sync.py <zene-binary> \\
        [--transcript PATH] [--timeout SECONDS]

Exit code 0 only when every assertion held, 1 when one did not, 77 when this
host cannot carry announcements at all. --transcript writes the raw
request/response log plus the derived evidence, and is written even when a step
raises, so the committed evidence shows what happened rather than what was hoped
for.
"""

import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)
# The evidence transcript, the hash helper and the one-instance wrapper live in
# tests/link_sync_evidence.py: this file is the CLAIM and the checks, and the
# plumbing was split out for the same Gate 7 reason control_socket_harness.py
# was split out of its flows.
from link_sync_evidence import SKIP_EXIT, Evidence, Peer, sha256_of  # noqa: E402

#: How long to wait for a state that only a working session produces: a peer to
#: appear, a tempo to be adopted, a departed peer to expire. Generous - a loaded
#: build box is not a metronome, and a bound that expires is a FAILURE here.
CONVERGE_TIMEOUT = 15.0
EXPIRE_TIMEOUT = 10.0
#: The phase tolerance, IN BEATS. 0.15 beats at 145 BPM is 62 ms: far above the
#: reply latency the harness adds and far below the 2.5-beat error a model that
#: ignored the transit term would show.
PHASE_TOLERANCE_BEATS = 0.15
#: The deliberate gap between reading A's beat and B's beat, so the phase
#: comparison has a real interval to compare against (see sample_phase).
PHASE_GAP_SECONDS = 0.3
#: The tempo the leader declares, and the tempo it then sets through the
#: transport. Neither is the 120 BPM an instance starts at, so "B follows" is a
#: measurement and not a coincidence.
DECLARED_TEMPO = 145.0
TRANSPORT_TEMPO = 132

class SyncRun:
    """Two real instances, one session, step by step.

    Each step is a method so that no single function carries the whole proof -
    the gate that measures this file's complexity is the same one that measures
    the C++ side, and it is right about both.
    """

    def __init__(self, options):
        self.options = options
        self.binary = os.path.abspath(options.binary)
        self.evidence = Evidence()
        self.problems = H.Problems()
        self.calls = []
        self.leader = None
        self.follower = None
        self.declared_tempo = 0.0
        self.samples = []

    # ------------------------------------------------------------------
    # plumbing
    # ------------------------------------------------------------------

    def require(self, condition, text):
        self.problems.require(condition, text)
        return bool(condition)

    def finding(self, label, text):
        self.evidence.finding(label, text)

    def exchange(self, peer, request_id, cmd, args=None):
        reply = peer.call(request_id, cmd, args)
        self.calls.append("[%s] -> %s %s" % (peer.label, cmd, json.dumps(args or {})))
        self.calls.append("[%s] <- %s" % (peer.label, json.dumps(reply)))
        return reply

    def ok(self, peer, request_id, cmd, args=None):
        return H.ok_result(self.exchange(peer, request_id, cmd, args), request_id)

    def read(self, peer, request_id):
        return peer.refresh(request_id)

    def wait_until(self, peer, request_id, predicate, timeout, describe):
        """Poll link.get_state until `predicate` holds. Bounded: a hang is a
        failure, and the last reading is what the transcript shows."""
        deadline = time.time() + timeout
        last = {}
        while time.time() < deadline:
            last = self.read(peer, request_id)
            if predicate(last):
                return last
            time.sleep(0.05)
        self.problems.add("%s (%s) did not hold within %.1fs; last state: %s"
                          % (describe, peer.label, timeout,
                             json.dumps({key: last.get(key) for key in
                                         ("session_tempo", "engine_tempo", "peer_count")})))
        return last

    # ------------------------------------------------------------------
    # the steps
    # ------------------------------------------------------------------

    def start_instances(self):
        self.evidence.section("0. two real instances, each with its own control socket")
        self.finding("binary", self.binary)
        self.finding("binary sha256", sha256_of(self.binary))
        self.leader = Peer("A-leader", H.start_instance(self.binary))
        self.follower = Peer("B-follower", H.start_instance(self.binary))
        for peer in (self.leader, self.follower):
            H.wait_for_socket(peer.instance)
            peer.client = H.connect(peer.instance)
            H.wait_ready(peer.instance, peer.client, None)
            peer.refresh(1)
            self.finding("%s socket" % peer.label, peer.instance.socket_path)
            self.finding("%s peer id" % peer.label, peer.peer_id())
            self.finding("%s model" % peer.label, str(peer.state.get("model")))

    def check_model(self):
        self.evidence.section("1. the model each instance speaks, before anything is enabled")
        for peer in (self.leader, self.follower):
            transport = peer.state.get("transport", {})
            interop = peer.state.get("interop", {})
            self.finding("%s transport" % peer.label, json.dumps(transport))
            self.finding("%s interop" % peer.label, json.dumps(interop))
            self.require(peer.state.get("model") == "zene-link-style",
                         "%s reports model %r, expected zene-link-style"
                         % (peer.label, peer.state.get("model")))
            self.require(interop.get("ableton_link") is False,
                         "%s claims Ableton Link interoperability, which this build does not have"
                         % peer.label)
            self.require("LICENSE.md" in str(interop.get("reason", "")),
                         "%s's interop block does not point at the licence record" % peer.label)
            self.require(peer.state.get("enabled") is False,
                         "%s reports sync already on before anything enabled it" % peer.label)
            self.require(peer.state.get("peer_count") == 0,
                         "%s joined a session before link.set_enabled" % peer.label)
        return 0

    def check_transport(self):
        """Availability is only knowable once the transport is STARTED, and
        link.set_enabled is what starts it - a state read must not open a socket,
        so the check belongs here, after the join, and not in step 1."""
        self.evidence.section("2b. whether announcements can travel at all")
        for peer in (self.leader, self.follower):
            transport = peer.state.get("transport", {})
            self.finding("%s transport" % peer.label, json.dumps(transport))
            if transport.get("available") is not True:
                print("SKIP: %s cannot carry announcements: %s"
                      % (peer.label, transport.get("reason")))
                print("      the model is still covered by the registered ctest "
                      "ControlLinkCommandsTest")
                return SKIP_EXIT
        return 0

    def join(self):
        self.evidence.section("2. both instances join the session")
        for index, peer in enumerate((self.leader, self.follower)):
            reply = self.ok(peer, 10 + index, "link.set_enabled", {"enabled": True})
            self.finding("%s set_enabled" % peer.label,
                         "enabled=%s peer_count=%s endpoint=%s"
                         % (reply.get("enabled"), reply.get("peer_count"),
                            reply.get("transport", {}).get("endpoint")))
            self.require(reply.get("enabled") is True,
                         "%s did not report sync on after enabling it" % peer.label)
        # A sees B, and B sees A - by the OTHER's own peer id.
        leader_id = self.leader.peer_id()
        follower_id = self.follower.peer_id()
        first = self.wait_until(self.leader, 20, lambda state: any(
            peer.get("id") == follower_id for peer in state.get("peers", [])),
            CONVERGE_TIMEOUT, "A sees B as a peer")
        second = self.wait_until(self.follower, 21, lambda state: any(
            peer.get("id") == leader_id for peer in state.get("peers", [])),
            CONVERGE_TIMEOUT, "B sees A as a peer")
        self.finding("A peers", json.dumps(first.get("peers")))
        self.finding("B peers", json.dumps([{key: peer.get(key) for key in ("id", "tempo",
                                                                            "revision", "age_ms")}
                                            for peer in second.get("peers", [])]))
        self.require(first.get("peer_count", 0) >= 1,
                     "A's peer table is empty after both instances joined")
        self.require(second.get("peer_count", 0) >= 1,
                     "B's peer table is empty after both instances joined")
        self.read(self.leader, 22)
        self.read(self.follower, 23)

    def converge(self):
        self.evidence.section("3. one session tempo, agreed by both, with no relay")
        deadline = time.time() + CONVERGE_TIMEOUT
        agreed = False
        while time.time() < deadline:
            self.read(self.leader, 30)
            self.read(self.follower, 31)
            leader_tempo = self.leader.state.get("session_tempo")
            follower_tempo = self.follower.state.get("session_tempo")
            if leader_tempo == follower_tempo:
                agreed = True
                break
            time.sleep(0.05)
        self.finding("A session tempo", "%s (owner %s, revision %s)"
                     % (self.leader.state.get("session_tempo"),
                        self.leader.state.get("session_revision_owner"),
                        self.leader.state.get("session_revision")))
        self.finding("B session tempo", "%s (owner %s, revision %s)"
                     % (self.follower.state.get("session_tempo"),
                        self.follower.state.get("session_revision_owner"),
                        self.follower.state.get("session_revision")))
        self.require(agreed, "the two instances never agreed on one session tempo: %r vs %r"
                             % (self.leader.state.get("session_tempo"),
                                self.follower.state.get("session_tempo")))
        self.require(self.leader.state.get("session_revision_owner")
                     == self.follower.state.get("session_revision_owner"),
                     "the two instances name different revision owners, so the session is split")

    def drive_declared(self):
        self.evidence.section("4. THE HEADLINE: A declares a session tempo; B follows")
        declared = self.ok(self.leader, 40, "link.set_session_tempo", {"bpm": DECLARED_TEMPO})
        self.finding("A declared", "session_tempo=%s revision=%s owner=%s"
                     % (declared.get("session_tempo"), declared.get("session_revision"),
                        declared.get("session_revision_owner")))
        self.require(declared.get("session_tempo") == DECLARED_TEMPO,
                     "A's own session tempo is %r after declaring %r"
                     % (declared.get("session_tempo"), DECLARED_TEMPO))
        # B is driven by NOTHING but its socket: the assertion is on B's state.
        state = self.wait_until(self.follower, 41,
                                lambda report: report.get("session_tempo") == DECLARED_TEMPO,
                                CONVERGE_TIMEOUT, "B adopts the declared tempo")
        self.finding("B session tempo", json.dumps(state.get("session_tempo")))
        self.finding("B engine tempo", json.dumps(state.get("engine_tempo")))
        self.finding("B revision owner", json.dumps(state.get("session_revision_owner")))
        self.find_tempo_in_engine(state)
        self.require(state.get("session_revision_owner") == self.leader.peer_id(),
                     "B attributes the session tempo to %r, not to A (%r)"
                     % (state.get("session_revision_owner"), self.leader.peer_id()))
        self.require(state.get("tempo_is_local") is False,
                     "B still calls the session tempo its own, so it did not adopt anything")
        self.declared_tempo = DECLARED_TEMPO

    def find_tempo_in_engine(self, state):
        """B's ENGINE tempo, polled to the rounded session tempo: the tempo the
        instance actually plays, not a number in a state block."""
        self.wait_until(self.follower, 42,
                        lambda report: report.get("engine_tempo") == int(DECLARED_TEMPO),
                        CONVERGE_TIMEOUT, "B's engine plays the adopted tempo")

    def drive_through_transport(self):
        self.evidence.section("5. an ORDINARY tempo change reaches the session too")
        self.ok(self.leader, 50, "transport.set_tempo", {"bpm": TRANSPORT_TEMPO})
        self.finding("A transport.set_tempo", "%d" % TRANSPORT_TEMPO)
        state = self.wait_until(self.follower, 51,
                                lambda report: report.get("session_tempo") == float(TRANSPORT_TEMPO),
                                CONVERGE_TIMEOUT, "B adopts a tempo set with transport.set_tempo")
        self.finding("B session tempo", json.dumps(state.get("session_tempo")))
        self.finding("B engine tempo", json.dumps(state.get("engine_tempo")))
        self.require(state.get("engine_tempo") == TRANSPORT_TEMPO,
                     "B's engine tempo is %r, not the %d A set with transport.set_tempo"
                     % (state.get("engine_tempo"), TRANSPORT_TEMPO))

    def sample_phase(self, request_id):
        """A's beat and tempo, then - after a DELIBERATE gap - B's beat, with the
        client's own clock around both. The comparison is against the elapsed
        time, not against a shared timestamp this test would have to guess, and
        the gap is what makes it a comparison: with the two reads back to back
        the expected advance is ~0 and the assertion would hold for any two
        numbers. At 132 BPM, 0.3 s is 0.66 beats - so a model that copied a
        peer's beat without advancing it by the elapsed time cannot pass."""
        leader = self.read(self.leader, request_id)
        after = time.monotonic()
        time.sleep(PHASE_GAP_SECONDS)
        follower = self.read(self.follower, request_id + 1)
        return {"leader_beat": leader.get("beat"), "leader_tempo": leader.get("session_tempo"),
                "follower_beat": follower.get("beat"),
                "elapsed": time.monotonic() - after,
                "follower_state": follower}

    def check_phase_sample(self, index, sample):
        tempo = float(sample["leader_tempo"])
        expected = float(sample["follower_beat"]) - float(sample["leader_beat"])
        # What B's beat SHOULD have gained over the wall time between the reads.
        wanted = tempo * sample["elapsed"] / 60.0
        drift = abs(expected - wanted)
        self.finding("sample %d" % index, "A beat %s, B beat %s, elapsed %.3fs at %s BPM"
                     % (sample["leader_beat"], sample["follower_beat"], sample["elapsed"], tempo))
        self.finding("  phase agreement", "B advanced %.4f beats, %.4f expected (drift %.4f)"
                     % (expected, wanted, drift))
        self.require(tempo > 0.0, "the session tempo is %r, so the phase cannot be compared" % tempo)
        self.require(drift < PHASE_TOLERANCE_BEATS,
                     "sample %d: the shared beat drifted %.4f beats (tolerance %.2f) - the two "
                     "instances are not on one timeline" % (index, drift, PHASE_TOLERANCE_BEATS))
        state = sample["follower_state"]
        quantum = float(state.get("quantum", 0))
        phase = float(state.get("phase_beats", -1))
        self.require(0.0 <= phase < quantum,
                     "B's phase %r is outside its quantum %r" % (phase, quantum))
        error = float(state.get("phase_error_beats", 0))
        self.require(-quantum / 2.0 <= error <= quantum / 2.0,
                     "B's phase error %r is not wrapped into +-quantum/2" % error)

    def phase(self):
        self.evidence.section("6. the shared beat phase, measured against elapsed wall time")
        for index in (1, 2):
            sample = self.sample_phase(60 + 10 * index)
            self.samples.append(sample)
            self.check_phase_sample(index, sample)

    def liveness(self):
        self.evidence.section("7. both instances are announcing")
        first = (int(self.read(self.leader, 80).get("published_count", 0)),
                 int(self.read(self.follower, 81).get("published_count", 0)))
        time.sleep(0.5)
        second = (int(self.read(self.leader, 82).get("published_count", 0)),
                  int(self.read(self.follower, 83).get("published_count", 0)))
        self.finding("A published", "%d -> %d" % (first[0], second[0]))
        self.finding("B published", "%d -> %d" % (first[1], second[1]))
        self.require(second[0] > first[0], "A stopped announcing (published_count %r)" % second[0])
        self.require(second[1] > first[1], "B stopped announcing (published_count %r)" % second[1])

    def leave(self):
        self.evidence.section("8. B leaves; A's session empties and the tempo does not move")
        self.finding("A tempo before", json.dumps(self.read(self.leader, 90).get("engine_tempo")))
        self.ok(self.follower, 91, "link.set_enabled", {"enabled": False})
        gone = self.read(self.follower, 92)
        self.finding("B after leaving", "enabled=%s peer_count=%s"
                     % (gone.get("enabled"), gone.get("peer_count")))
        self.require(gone.get("peer_count") == 0, "B still lists peers after leaving")
        self.require(gone.get("enabled") is False, "B still reports sync on after leaving")
        state = self.wait_until(self.leader, 93,
                                lambda report: not any(peer.get("id") == self.follower.peer_id()
                                                       for peer in report.get("peers", [])),
                                EXPIRE_TIMEOUT, "B expires out of A's peer table")
        self.finding("A peers after expiry", json.dumps(state.get("peers")))
        self.finding("A tempo after", json.dumps(state.get("engine_tempo")))
        self.require(state.get("engine_tempo") == TRANSPORT_TEMPO,
                     "leaving the session moved A's tempo to %r - leaving is not resetting"
                     % state.get("engine_tempo"))
        self.require(state.get("peer_count") == 0,
                     "A still lists %r peers after B left" % state.get("peer_count"))

    def quit(self):
        self.evidence.section("9. both instances shut down through their normal path")
        for index, peer in enumerate((self.leader, self.follower)):
            self.ok(peer, 100 + index, "link.set_enabled", {"enabled": False})
            self.exchange(peer, 110 + index, "control.quit")
        for peer in (self.leader, self.follower):
            exited, code, elapsed = peer.instance.wait_for_exit(H.QUIT_TIMEOUT)
            self.finding("%s exit" % peer.label, "exited=%s code=%s after %.1fs"
                         % (exited, code, elapsed))
            self.require(exited, "%s did not exit inside %.0fs" % (peer.label, H.QUIT_TIMEOUT))
            self.require(code == 0, "%s exited with code %r" % (peer.label, code))
            log = peer.instance.read_log()
            self.require(H.FATAL_GUARD_MARKER not in log,
                         "%s's last-resort guard fired: the shutdown path is broken" % peer.label)
            self.require(H.LEGACY_WATCHDOG_LINE not in log,
                         "%s printed the pre-fix watchdog line" % peer.label)

    # ------------------------------------------------------------------
    # the run
    # ------------------------------------------------------------------

    def run(self):
        self.evidence.section("Zene Studio 0.3.0-alpha - session sync, two instances")
        self.finding("claim", "two Zene instances join one session and one drives the other's "
                              "tempo and phase, with no relay between them")
        self.finding("model", "zene-link-style (Ableton Link SEMANTICS, not the Ableton Link "
                              "library - see docs/LINK-SYNC.md section 1)")
        self.start_instances()
        try:
            for step in (self.check_model, self.join, self.check_transport,
                         self.converge, self.drive_declared, self.drive_through_transport,
                         self.phase, self.liveness, self.leave, self.quit):
                if step() == SKIP_EXIT:
                    self.evidence.write(self.options.transcript, "SKIP")
                    return SKIP_EXIT
        except Exception as error:  # noqa: BLE001  (the transcript must survive anything)
            self.problems.add("the run raised %s: %s" % (type(error).__name__, error))
            self.finding("raised", "%s: %s" % (type(error).__name__, error))
        finally:
            for peer in (self.leader, self.follower):
                if peer.instance is not None:
                    peer.instance.close()
        return self.report()

    def report(self):
        self.evidence.section("RAW TRANSCRIPT (every request, every reply)")
        for line in self.calls:
            self.evidence.line(line)
        self.evidence.section("RESULT")
        passed = self.problems.report("session sync, two instances")
        for item in self.problems.items:
            self.evidence.line("  FAILED CHECK: %s" % item)
        self.evidence.line("  %s" % ("PASS: two instances, one session, A drives B's tempo and "
                                     "phase through the socket" if passed else
                                     "FAIL: see the failed checks above"))
        self.evidence.write(self.options.transcript, "PASS" if passed else "FAIL")
        if self.options.transcript:
            print("transcript: %s" % self.options.transcript)
        return 0 if passed else 1


def main(argv):
    global CONVERGE_TIMEOUT
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("binary")
    parser.add_argument("--transcript", default=None)
    parser.add_argument("--timeout", type=float, default=CONVERGE_TIMEOUT)
    options = parser.parse_args(argv[1:])
    if not os.path.exists(options.binary):
        print("no binary at %s" % options.binary)
        return 2
    CONVERGE_TIMEOUT = options.timeout
    return SyncRun(options).run()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
