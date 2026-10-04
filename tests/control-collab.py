#!/usr/bin/env python3
"""Two real instances in one collaboration session (0.5.0 F6, WF-050/052), end to end.

THE CLAIM: with no server, instance A hosts, instance B joins with A's invite over
TLS-PSK on 127.0.0.1, B receives A's project, and edits made through commands on
EITHER side reach both copies in the host's order. Undo is the host's, shared.

THE STEPS (every assertion reads a copy's own state, never the reply of the edit):
  1. Both start headless; collab.get_state says idle and available. Without the
     feature in the build (available:false) the run exits 77 (Skipped).
  2. A hosts (consent true, map_port false: no router is involved) and hands out a
     "local" editor invite; collab.host without consent is refused.
  3. B joins and reaches peer.state "joined" with the project; A lists B.
  3a. Re-sync both ways (collab.resync on the peer, then on the host) leaves the
     copies equal; the host changes B's role to viewer and back (collab.set_role),
     its peer list says so each time, and an unknown peer or a peer asking is refused.
  4. A adds a track: B's track count follows.
  5. B adds a track: the edit is FORWARDED (B's reply says so), A applies it, and
     both copies end with the same count.
  6. A undoes: the peers are re-synced and B's count follows A's.
  7. A viewer invite: a viewer's edit is refused locally, typed.
  8. B leaves, A stops; both quit with exit code 0.

Usage: QT_QPA_PLATFORM=offscreen python3 control-collab.py <zene-binary>
Exit 0 when every check held, 1 when one did not, 77 when the build has no collaboration.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

SKIP_EXIT = 77
CONVERGE_TIMEOUT = 20.0


class Side:
    """One instance and its control client."""

    def __init__(self, label, binary):
        self.label = label
        self.instance = H.start_instance(binary)
        self.client = None
        self.next_id = 1

    def open(self):
        self.client = H.connect(self.instance)
        H.wait_ready(self.instance, self.client, None)

    def call(self, cmd, args=None):
        self.next_id += 1
        reply = self.client.call(self.next_id, cmd, args or {})
        print("[%s] %s %s -> %s" % (self.label, cmd, args or {}, str(reply)[:300]), flush=True)
        return reply

    def ok(self, cmd, args=None):
        reply = self.call(cmd, args)
        return H.ok_result(reply, self.next_id)

    def track_count(self):
        return self.ok("track.list").get("count", -1)

    def state(self):
        return self.ok("collab.get_state")


def wait_for(predicate, describe, timeout=CONVERGE_TIMEOUT):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if predicate():
            return True
        time.sleep(0.1)
    raise AssertionError("%s did not hold within %.0fs" % (describe, timeout))


def peer_joined(side):
    peer = side.state().get("peer", {})
    return peer.get("state") == "joined" and peer.get("has_project") is True


def step_available(a, b, problems):
    for side in (a, b):
        state = side.state()
        if state.get("available") is not True:
            print("collaboration is not in this build: %s" % state.get("reason"))
            return SKIP_EXIT
        problems.require(state.get("mode") == "idle", "%s is not idle before anything was asked" % side.label)
    return 0


def step_host(a, problems):
    refused = a.call("collab.host", {"consent": False})
    problems.require(refused.get("ok") is False, "collab.host without consent was not refused")
    hosted = a.ok("collab.host", {"consent": True, "map_port": False, "name": "A"})
    problems.require(hosted.get("mode") == "hosting", "A is not hosting: %r" % hosted.get("mode"))
    invite = a.ok("collab.invite", {"role": "editor", "scope": "local"})
    problems.require(invite.get("invite", "").startswith("ZENE1-"), "the invite has the wrong shape")
    problems.require("ZENE1-" not in str(a.state()), "collab.get_state leaks an invite")
    return invite["invite"]


def step_join(a, b, invite, problems):
    joined = b.ok("collab.join", {"invite": invite, "name": "B"})
    problems.require(joined.get("mode") == "joined", "B is not in a session: %r" % joined.get("mode"))
    wait_for(lambda: peer_joined(b), "B joined with the host's project")
    wait_for(lambda: len(a.state().get("host", {}).get("peers", [])) == 1, "A lists B")
    problems.require(a.track_count() == b.track_count(), "B's copy does not match A's after the join")


def peer_role(a, peer_id):
    for peer in a.state().get("host", {}).get("peers", []):
        if peer.get("id") == peer_id:
            return peer.get("role")
    return None


def step_session_controls(a, b, problems):
    resent = b.ok("collab.resync")
    problems.require(resent.get("mode") == "joined", "B's collab.resync did not answer with its session state")
    wait_for(lambda: peer_joined(b), "B holds the host's project after asking for it again")
    hosted = a.ok("collab.resync")
    problems.require(hosted.get("mode") == "hosting", "A's collab.resync did not answer with its session state")
    wait_for(lambda: peer_joined(b) and a.track_count() == b.track_count(), "B matches A after A's re-sync")

    peers = a.state().get("host", {}).get("peers", [])
    problems.require(len(peers) == 1, "A does not list exactly one peer: %r" % peers)
    peer_id = peers[0]["id"]
    problems.require(peer_role(a, peer_id) == "editor", "B did not join as an editor")
    viewer = a.ok("collab.set_role", {"peer": peer_id, "role": "viewer"})
    problems.require(viewer.get("mode") == "hosting", "collab.set_role did not answer with the host's state")
    problems.require(peer_role(a, peer_id) == "viewer", "B's role is not viewer after collab.set_role")
    a.ok("collab.set_role", {"peer": peer_id, "role": "editor"})
    problems.require(peer_role(a, peer_id) == "editor", "B's role is not editor again after collab.set_role")
    unknown = a.call("collab.set_role", {"peer": peer_id + 1000, "role": "viewer"})
    problems.require(unknown.get("ok") is False and (unknown.get("error") or {}).get("kind") == "not_found",
                     "collab.set_role on an unknown peer was not a typed not_found: %r" % unknown)
    asked = b.call("collab.set_role", {"peer": peer_id, "role": "viewer"})
    problems.require(asked.get("ok") is False and (asked.get("error") or {}).get("kind") == "refused",
                     "a peer's collab.set_role was not refused: %r" % asked)


def step_host_edit(a, b, problems):
    before = b.track_count()
    a.ok("track.add", {"type": "instrument", "name": "from A"})
    wait_for(lambda: b.track_count() == before + 1, "A's track.add reached B")
    problems.require(a.track_count() == b.track_count(), "the copies differ after A's edit")


def step_peer_edit(a, b, problems):
    before = a.track_count()
    reply = b.ok("track.add", {"type": "instrument", "name": "from B"})
    problems.require(reply.get("forwarded") is True, "B applied its edit locally instead of forwarding it")
    wait_for(lambda: a.track_count() == before + 1, "B's edit was applied by A")
    wait_for(lambda: b.track_count() == before + 1, "B's edit came back to B from A")


def step_undo(a, b, problems):
    a.ok("control.undo")
    wait_for(lambda: b.track_count() == a.track_count(), "B re-synced after A's undo")
    problems.require(a.state().get("undo", "").startswith("shared"), "the undo model is not reported")


def step_viewer(a, binary, problems):
    viewer = Side("V-viewer", binary)
    try:
        viewer.open()
        invite = a.ok("collab.invite", {"role": "viewer", "scope": "local"})["invite"]
        viewer.ok("collab.join", {"invite": invite, "name": "V"})
        wait_for(lambda: peer_joined(viewer), "the viewer joined")
        refused = viewer.call("track.add", {"type": "instrument"})
        problems.require(refused.get("ok") is False and (refused.get("error") or {}).get("kind") == "refused",
                         "a viewer's edit was not refused: %r" % refused)
        viewer.ok("collab.leave")
    finally:
        viewer.instance.close()


def step_quit(a, b, problems):
    b.ok("collab.leave")
    stopped = a.ok("collab.stop")
    problems.require(stopped.get("stopped") is True, "collab.stop did not report stopping")
    for side in (a, b):
        side.call("control.quit")
    for side in (a, b):
        exited, code, _ = side.instance.wait_for_exit(H.QUIT_TIMEOUT)
        problems.require(exited and code == 0, "%s did not exit cleanly (exited=%s code=%r)" % (side.label, exited, code))


def run(binary):
    problems = H.Problems()
    a = Side("A-host", binary)
    b = Side("B-peer", binary)
    try:
        a.open()
        b.open()
        if step_available(a, b, problems) == SKIP_EXIT:
            return SKIP_EXIT
        invite = step_host(a, problems)
        step_join(a, b, invite, problems)
        step_session_controls(a, b, problems)
        step_host_edit(a, b, problems)
        step_peer_edit(a, b, problems)
        step_undo(a, b, problems)
        step_viewer(a, binary, problems)
        step_quit(a, b, problems)
    except (AssertionError, H.Timeout) as error:
        problems.add("the run stopped: %s" % error)
    finally:
        a.instance.close()
        b.instance.close()
    return 0 if problems.report("collaboration, two instances") else 1


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(run(os.path.abspath(sys.argv[1])))
