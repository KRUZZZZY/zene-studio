#!/usr/bin/env python3
"""Two Zene instances in ONE real Ableton Link session, driven end to end through
their control sockets (WF-051, docs/LINK-SYNC.md section 6).

THE CLAIM UNDER TEST:

    two real Zene binaries, each with the vendored Ableton Link library and the
    default backend, discover each other as Link peers and share tempo, beat phase
    and start/stop - with no relay between them and nothing but Link's own
    protocol on the wire.

This is the cross-PROCESS half of the proof. AbletonLinkInteropTest is the
in-process half (Zene against a peer built from Link's own API, period by period);
ControlLinkSync is the zene-link-style fallback's own two-instance proof. What no
test here can show is a specific third-party application (there is no Ableton
Live on a CI box): both peers are Zene, speaking the reference library that Live,
Bitwig and every Link app embed.

THE STEPS:
  1. Both instances report model `ableton-link` and the pinned library version.
  2. Both join; each counts the other as a Link peer.
  3. A's ordinary transport.set_tempo reaches B's engine.
  4. B's fractional link.set_session_tempo reaches A's session exactly and A's
     engine rounded (the engine's tempo is an integer).
  5. With start/stop sync on both, A's transport.play starts B; while both play,
     each engine's play head is locked to the session phase (interop.audio.locked)
     with |phase_error_beats| under PHASE_TOLERANCE in every sample and the
     session tempo of step 4 unmoved by the start; B's transport.stop stops A.
  6. B leaves; A's peer count drops and A's tempo does not move.
  7. Both quit with exit code 0.

Exit 0 when every check held, 1 when one did not, 77 (ctest "Skipped") only when
this host measurably cannot deliver a multicast datagram on Link's group.
"""

import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)
from link_sync_evidence import SKIP_EXIT, Evidence, Peer, host_multicast_loopback, sha256_of  # noqa: E402

JOIN_TIMEOUT = 15.0
PROPAGATE_TIMEOUT = 6.0
#: The phase check's bound, stated before it was measured. `phase_error_beats` is
#: the session phase NOW minus the play head's phase, and the play head is where
#: the last rendered period left it: one engine period plus the dummy device's
#: render-ahead in front of now, about 12 ms - 0.024 beats at 120 BPM. 0.06 beats
#: (30 ms) leaves room for scheduling jitter between the two reads and nothing
#: like the up-to-2-beat error of an engine that is not following the session.
PHASE_TOLERANCE = 0.06
PHASE_SAMPLES = 10
A_TEMPO = 133
B_SESSION_TEMPO = 97.5


def wire_tempo_equal(value, expected):
    """Link carries a tempo as whole microseconds per beat (link/Tempo.hpp)."""
    return value is not None and abs(value - expected) <= expected * expected * 0.5 / 60e6 * 1.000001


class InteropRun:
    """Each step is a method, so no function carries the whole proof."""

    def __init__(self, options):
        self.options = options
        self.binary = os.path.abspath(options.binary)
        self.evidence = Evidence()
        self.problems = H.Problems()
        self.calls = []
        self.a = None
        self.b = None

    # -- plumbing ---------------------------------------------------------
    def require(self, condition, text):
        return self.problems.require(condition, text)

    def finding(self, label, text):
        self.evidence.finding(label, text)

    def ok(self, peer, request_id, cmd, args=None):
        reply = peer.call(request_id, cmd, args)
        self.calls.append("[%s] -> %s %s" % (peer.label, cmd, json.dumps(args or {})))
        self.calls.append("[%s] <- %s" % (peer.label, json.dumps(reply)))
        return H.ok_result(reply, request_id)

    def transport(self, peer, request_id):
        return H.ok_result(peer.call(request_id, "transport.get_state"), request_id)

    def wait_until(self, read, predicate, timeout, describe):
        deadline = time.time() + timeout
        last = {}
        while time.time() < deadline:
            last = read()
            if predicate(last):
                return last
            time.sleep(0.05)
        self.problems.add("%s did not hold within %.1fs; last reading: %s" % (describe, timeout, json.dumps(last)))
        return last

    # -- the steps --------------------------------------------------------
    def start_instances(self):
        self.evidence.section("0. two real instances, each with its own control socket")
        self.finding("binary", self.binary)
        self.finding("binary sha256", sha256_of(self.binary))
        self.a = Peer("A", H.start_instance(self.binary))
        self.b = Peer("B", H.start_instance(self.binary))
        for peer in (self.a, self.b):
            H.wait_for_socket(peer.instance)
            peer.client = H.connect(peer.instance)
            H.wait_ready(peer.instance, peer.client, None)
            peer.refresh(1)

    def check_multicast(self):
        delivered, elapsed, problem = host_multicast_loopback()
        self.finding("host multicast loopback", "delivered=%s elapsed_ms=%s %s" % (delivered, elapsed, problem))
        if not delivered:
            print("SKIP: this host does not deliver multicast on 224.76.78.75:20808 (%s)" % problem)
            return SKIP_EXIT
        return 0

    def check_backend(self):
        self.evidence.section("1. both speak real Ableton Link")
        for peer in (self.a, self.b):
            interop = peer.state.get("interop", {})
            self.finding("%s model" % peer.label, str(peer.state.get("model")))
            self.finding("%s library" % peer.label, "%s %s" % (interop.get("library"), interop.get("version")))
            self.require(peer.state.get("model") == "ableton-link", "%s is not on the Ableton Link backend" % peer.label)
            self.require(interop.get("ableton_link") is True, "%s's interop block denies Ableton Link" % peer.label)
            self.require(interop.get("version") == "4.1", "%s reports Link %r, not the pinned 4.1" % (peer.label, interop.get("version")))

    def join(self):
        self.evidence.section("2. both join; each counts the other as a Link peer")
        for index, peer in enumerate((self.a, self.b)):
            self.ok(peer, 10 + index, "link.set_enabled", {"enabled": True})
        for index, peer in enumerate((self.a, self.b)):
            state = self.wait_until(lambda p=peer, i=index: p.refresh(20 + i),
                                    lambda s: s.get("peer_count", 0) >= 1, JOIN_TIMEOUT,
                                    "%s counts a Link peer" % peer.label)
            self.finding("%s peer_count" % peer.label, json.dumps(state.get("peer_count")))

    def tempo_a_to_b(self):
        self.evidence.section("3. A's transport.set_tempo reaches B's engine")
        self.ok(self.a, 30, "transport.set_tempo", {"bpm": A_TEMPO})
        state = self.wait_until(lambda: self.b.refresh(31),
                                lambda s: s.get("engine_tempo") == A_TEMPO and wire_tempo_equal(s.get("session_tempo"), A_TEMPO),
                                PROPAGATE_TIMEOUT, "B follows A's %d BPM" % A_TEMPO)
        self.finding("B engine/session tempo", "%s / %s" % (state.get("engine_tempo"), state.get("session_tempo")))

    def tempo_b_to_a(self):
        self.evidence.section("4. B's fractional session tempo reaches A (exact) and A's engine (rounded)")
        self.ok(self.b, 40, "link.set_session_tempo", {"bpm": B_SESSION_TEMPO})
        state = self.wait_until(lambda: self.a.refresh(41),
                                lambda s: wire_tempo_equal(s.get("session_tempo"), B_SESSION_TEMPO) and s.get("engine_tempo") == 98,
                                PROPAGATE_TIMEOUT, "A follows B's %.1f BPM" % B_SESSION_TEMPO)
        self.finding("A engine/session tempo", "%s / %s" % (state.get("engine_tempo"), state.get("session_tempo")))

    def start_stop_and_phase(self):
        self.evidence.section("5. start/stop sync both ways, and the phase lock while both play")
        for index, peer in enumerate((self.a, self.b)):
            self.ok(peer, 50 + index, "link.set_start_stop_sync", {"start_stop_sync": True})
        self.ok(self.a, 52, "transport.play")
        self.wait_until(lambda: self.transport(self.b, 53), lambda t: t.get("playing") is True,
                        PROPAGATE_TIMEOUT, "A's start starts B")
        time.sleep(4.5)   # the count-in to the next bar plus the lock
        self.sample_phase()
        self.ok(self.b, 60, "transport.stop")
        self.wait_until(lambda: self.transport(self.a, 61), lambda t: t.get("playing") is False,
                        PROPAGATE_TIMEOUT, "B's stop stops A")

    def sample_phase(self):
        worst = 0.0
        for sample in range(PHASE_SAMPLES):
            for index, peer in enumerate((self.a, self.b)):
                state = peer.refresh(70 + 2 * sample + index)
                audio = state.get("interop", {}).get("audio", {})
                error = abs(state.get("phase_error_beats", 99.0))
                worst = max(worst, error)
                # Starting must not move the tempo: a start committed with a timeline
                # captured inside Link's local-modification grace period once put A's
                # previous 133 BPM back into the session (docs/LINK-SYNC.md section 7).
                self.require(state.get("engine_tempo") == 98 and wire_tempo_equal(state.get("session_tempo"), B_SESSION_TEMPO),
                             "%s's tempo moved while playing (sample %d): engine %r, session %r"
                             % (peer.label, sample, state.get("engine_tempo"), state.get("session_tempo")))
                self.require(audio.get("locked") is True, "%s's play head is not locked (sample %d): %s"
                             % (peer.label, sample, json.dumps(audio)))
            time.sleep(0.1)
        self.finding("worst |phase_error_beats|", "%.4f over %d samples of each" % (worst, PHASE_SAMPLES))
        self.require(worst < PHASE_TOLERANCE, "a play head was %.4f beats off the session phase" % worst)

    def leave(self):
        self.evidence.section("6. B leaves; A's peer count drops and its tempo does not move")
        before = self.a.refresh(80).get("engine_tempo")
        self.ok(self.b, 81, "link.set_enabled", {"enabled": False})
        state = self.wait_until(lambda: self.a.refresh(82), lambda s: s.get("peer_count") == 0,
                                JOIN_TIMEOUT, "A's peer count drops to 0")
        self.require(state.get("engine_tempo") == before, "leaving moved A's tempo %r -> %r" % (before, state.get("engine_tempo")))

    def quit(self):
        self.evidence.section("7. both instances shut down through their normal path")
        for index, peer in enumerate((self.a, self.b)):
            self.ok(peer, 90 + index, "link.set_enabled", {"enabled": False})
            peer.call(95 + index, "control.quit")
        for peer in (self.a, self.b):
            exited, code, elapsed = peer.instance.wait_for_exit(H.QUIT_TIMEOUT)
            self.finding("%s exit" % peer.label, "exited=%s code=%s after %.1fs" % (exited, code, elapsed))
            self.require(exited and code == 0, "%s did not quit cleanly (exited=%s code=%r)" % (peer.label, exited, code))

    # -- the run ----------------------------------------------------------
    def run(self):
        self.evidence.section("Zene Studio - two instances, one Ableton Link session")
        if self.check_multicast() == SKIP_EXIT:
            self.evidence.write(self.options.transcript, "SKIP")
            return SKIP_EXIT
        self.start_instances()
        try:
            for step in (self.check_backend, self.join, self.tempo_a_to_b, self.tempo_b_to_a,
                         self.start_stop_and_phase, self.leave, self.quit):
                step()
        except Exception as error:  # noqa: BLE001  (the transcript must survive anything)
            self.problems.add("the run raised %s: %s" % (type(error).__name__, error))
        finally:
            for peer in (self.a, self.b):
                if peer is not None and peer.instance is not None:
                    peer.instance.close()
        return self.report()

    def report(self):
        self.evidence.section("RAW TRANSCRIPT")
        for line in self.calls:
            self.evidence.line(line)
        passed = self.problems.report("two instances, one Ableton Link session")
        self.evidence.write(self.options.transcript, "PASS" if passed else "FAIL")
        return 0 if passed else 1


def main(argv):
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("binary")
    parser.add_argument("--transcript", default=None)
    options = parser.parse_args(argv[1:])
    if not os.path.exists(options.binary):
        print("no binary at %s" % options.binary)
        return 2
    return InteropRun(options).run()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
