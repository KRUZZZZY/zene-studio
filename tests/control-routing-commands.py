#!/usr/bin/env python3
"""END-TO-END proof that the ROUTING is drivable and readable by an agent.

THE CLAIM UNDER TEST (docs/FEATURE-LIST-0.3.0.md rows 27-28). Two halves:
  * `routing.get_state` - the routing graph a signal is actually processed
    through. Row 28 says the graph is "in the tree with registered tests ... and
    live in the audio path; no command group"; this asserts the group, and
    asserts it on a MEASURED graph: load effects and the node set, the
    connections and the cached topological order the audio thread walks are read
    back off the wire.
  * `mixer.route_to` / `mixer.send_to` / `mixer.sidechain_to` /
    `mixer.route_remove` - ableton-gap/AGENT-TOOLING.md:186 names route_to and
    send_to, and the tip registered neither. This is their transcript: the mixer
    group's other five ids live in control-socket-integration.py, and these four
    are here because they ARE the routing surface routing.get_state reads.

WHY IT IS A REGISTERED CTEST AND NOT A UNIT TEST. tests/src/core/
RoutingGraphTest.cpp and RoutingGraphLiveTest.cpp already prove the graph's
arithmetic in process (a linear chain's plan, a cycle refused, the block the
output node carries). What they cannot prove is the release contract's section
3.1: that the topology is reachable THROUGH THE SOCKET by an agent. So this runs
the REAL binary ($<TARGET_FILE:zene>, QT_QPA_PLATFORM=offscreen, the shared
control_socket_harness) and reads every number back off the wire.

WHAT IT ASSERTS, in numbers:
  * a channel's chain with ONE loaded effect reports a graph of exactly two
    nodes (the chain input and the effect), one connection 0 -> 1, output_node 1
    and processing_order (0, 1); loading a second effect makes it three nodes,
    two connections and output_node 2 - the graph follows the effect list;
  * a track target reports kind "track", and a mixer-channel target reports its
    rack object (a track has no rack: rack is null);
  * mixer.route_to writes a unity routing, mixer.send_to writes a send with its
    amount, and a send INTO a bus comes back pre-fader without being asked -
    the engine's own rule (Mixer::createChannelSend);
  * mixer.route_remove removes it and control.undo brings it back WITH its
    amount and pre-fader flag, as ONE step;
  * the engine's feedback rule is the refusal: routing a channel back into a
    channel that already routes to it is a typed refusal, and a channel routing
    to itself is invalid_args.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-routing-commands.py <zene-binary>

Exit codes: 0 every check held; 1 a check failed (the app log is printed);
2 cannot run (no binary at the path).
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

#: The built-in effect this proof loads. It is a module this build ships, so no
#: external plugin file is needed; plugin.list is still consulted first and the
#: first loadable built-in EFFECT is used.
PREFERRED_EFFECT = "amplifier"
#: The A16 record a routing verb must leave behind: class, reversibility, and a
#: non-empty mechanism.
TRUE_INVERSE_RECORD = ("true_inverse", True, True)


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


def loadable_effects(catalogue):
    """The loadable built-in effects of plugin.list, preferred one first."""
    entries = [entry for entry in catalogue.get("devices") or []
               if entry.get("kind") == "effect" and entry.get("loadable") is True]
    preferred = [entry for entry in entries if entry.get("name") == PREFERRED_EFFECT]
    rest = [entry for entry in entries if entry.get("name") != PREFERRED_EFFECT]
    return preferred + rest


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


def graph_of(state):
    return (state.get("chain") or {}).get("graph") or {}


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


#: The graph a chain of ONE effect must report: two nodes (the chain input and
#: the effect), one connection 0 -> 1, and the order the audio thread walks.
ONE_EFFECT_GRAPH = {
    "node_count": 2,
    "connection_count": 1,
    "connections": [{"from": 0, "from_port": 0, "to": 1, "to_port": 0}],
    "processing_order": [0, 1],
    "output_node": 1,
}
#: ... and the chain-level answers that go with it.
ONE_EFFECT_CHAIN = {"effect_count": 1, "routes_through_graph": True}
#: An empty chain: no nodes at all, and no output node (-1).
EMPTY_CHAIN = {"kind": "track", "effect_count": 0, "node_count": 0, "output_node": -1}


def subset(source, keys):
    """The named keys of a dict, so a check compares whole dicts (one comparison
    instead of a chain of `and`s, and the failure prints the value that differed)."""
    return {key: (source or {}).get(key) for key in keys}


def load_effects(session, track_id, effects, count):
    """Loads up to `count` loadable effects onto the track; returns the loaded ids."""
    loaded = []
    for entry in effects[:count]:
        reply = session.result("plugin.load", {"target": track_id, "device": entry.get("id")})
        if reply.get("id"):
            loaded.append(reply["id"])
    return loaded


def check_chain_graph(session, catalogue, recorder):
    """The graph the chain renders through follows the effect list, measurably."""
    track = session.result("track.add", {"type": "instrument", "name": "Routing Target"})
    track_id = track.get("track")
    if not track_id:
        H.fail("track.add returned no track id (%r)" % track, None, None)
        return

    empty = session.result("routing.get_state", {"target": track_id})
    empty_seen = subset(empty, ("kind", "rack"))
    empty_seen["effect_count"] = (empty.get("chain") or {}).get("effect_count")
    empty_seen.update(subset(graph_of(empty), ("node_count", "output_node")))
    recorder.check("a track target reports itself and an empty chain graph",
                   empty_seen == EMPTY_CHAIN, "empty_seen=%r" % (empty_seen,))

    effects = loadable_effects(catalogue)
    recorder.check("this build offers a loadable built-in effect to route",
                   bool(effects), "effects=%r" % (effects[:3],))
    if not effects:
        return

    recorder.check("the effects were loaded onto the track",
                   len(load_effects(session, track_id, effects, 1)) == 1,
                   "effects=%r" % (effects[:1],))
    one = session.result("routing.get_state", {"target": track_id})
    graph = graph_of(one)
    recorder.check("one loaded effect is a two-node graph, wired input -> effect",
                   subset(graph, ONE_EFFECT_GRAPH) == ONE_EFFECT_GRAPH
                   and subset(one.get("chain"), ONE_EFFECT_CHAIN) == ONE_EFFECT_CHAIN,
                   "graph=%r chain=%r" % (graph, one.get("chain")))
    recorder.check("the loaded nodes carry the engine's own type names",
                   [node.get("type") for node in graph.get("nodes") or []]
                   == ["chain_input", "effect"],
                   "nodes=%r" % (graph.get("nodes"),))


def check_rack_graph(session, recorder):
    """A mixer channel has a rack object with its own graph; a track has none."""
    added = session.result("mixer.add_channel", {}).get("channel")
    if not added:
        recorder.check("a mixer channel was added", False, "mixer.add_channel returned no id")
        return
    state = session.result("routing.get_state", {"target": added})
    rack = state.get("rack") or {}
    recorder.check("a mixer channel reports its rack and that rack's graph",
                   state.get("kind") == "channel" and isinstance(state.get("rack"), dict)
                   and "graph" in rack and "routed_chains" in rack
                   and isinstance(rack.get("selected_chain"), int),
                   "rack=%r" % (rack,))
    recorder.check("a channel's chain is the channel's own effect chain",
                   (state.get("chain") or {}).get("effect_count") == 0,
                   "chain=%r" % (state.get("chain"),))
    session.result("mixer.remove_channel", {"channel": added})


def check_route_and_send(session, recorder):
    """route_to and send_to build real sends, and undo takes each one back."""
    first = session.result("mixer.add_channel", {}).get("channel")
    second = session.result("mixer.add_channel", {}).get("channel")
    if not first or not second:
        recorder.check("two mixer channels were added", False,
                       "first=%r second=%r" % (first, second))
        return None
    routed = session.result("mixer.route_to", {"channel": first, "to": second})
    state = session.result("mixer.get_state")
    send = send_to(channel_named(state, first), second)
    recorder.check("mixer.route_to writes a unity routing on the channel",
                   routed.get("amount") == 1.0 and routed.get("created") is True
                   and send is not None and send.get("amount") == 1.0
                   and send.get("pre_fader") is False,
                   "routed=%r send=%r" % (routed, send))
    undone = session.result("control.undo")
    recorder.check("control.undo removes the routing it created",
                   undone.get("undone") is True
                   and send_to(channel_named(session.result("mixer.get_state"), first),
                               second) is None,
                   "undone=%r sends=%r"
                   % (undone.get("undone"),
                      channel_named(session.result("mixer.get_state"), first)))

    session.result("mixer.send_to", {"channel": first, "to": second, "amount": 0.25})
    sent = send_to(channel_named(session.result("mixer.get_state"), first), second)
    recorder.check("mixer.send_to writes the amount it was given",
                   sent is not None and sent.get("amount") == 0.25,
                   "send=%r" % (sent,))
    return (first, second)


def check_bus_prefader_rule(session, recorder, ends):
    """The engine's own rule: a send INTO a bus is pre-fader without being asked."""
    if ends is None:
        return
    first = ends[0]
    bus = session.result("bus.create")
    if not bus.get("channel"):
        recorder.check("bus.create returned a channel", False, "bus.create=%r" % bus)
        return
    recorder.check("bus.create makes a channel the engine calls a bus",
                   bus.get("is_bus") is True and bus.get("name", "").startswith("Bus"),
                   "bus=%r" % (bus,))
    session.result("mixer.send_to", {"channel": first, "to": bus.get("channel"), "amount": 0.5})
    state = session.result("mixer.get_state")
    send = send_to(channel_named(state, first), bus.get("channel"))
    recorder.check("a send into a bus is pre-fader by the engine's own rule",
                   send is not None and send.get("pre_fader") is True
                   and send.get("amount") == 0.5,
                   "send=%r" % (send,))
    listed = session.result("bus.list")
    recorder.check("bus.list finds the bus and agrees with the count",
                   bus.get("channel") in [entry.get("id") for entry in listed.get("buses") or []]
                   and listed.get("count") == len(listed.get("buses") or []),
                   "buses=%r" % (listed.get("buses"),))


def check_route_remove_and_undo(session, recorder, ends):
    """Removing a send is reversible WITH its amount and pre-fader flag."""
    if ends is None:
        return
    first, second = ends
    removed = session.result("mixer.route_remove", {"channel": first, "to": second})
    gone = send_to(channel_named(session.result("mixer.get_state"), first), second)
    recorder.check("mixer.route_remove takes the send off the channel",
                   removed.get("removed") is True and gone is None,
                   "removed=%r send_after=%r" % (removed, gone))
    session.result("control.undo")
    back = send_to(channel_named(session.result("mixer.get_state"), first), second)
    recorder.check("control.undo brings the send back with its own amount",
                   back is not None and back.get("amount") == 0.25,
                   "send_after_undo=%r" % (back,))
    records = [record for record in session.result("control.transactions").get("transactions") or []
               if record.get("command") in ("mixer.route_remove", "mixer.route_to",
                                            "mixer.send_to")]
    signatures = {(record.get("class"), record.get("reversible"),
                   bool(record.get("mechanism"))) for record in records}
    recorder.check("every routing verb records an A16 transaction classified true_inverse",
                   signatures == {TRUE_INVERSE_RECORD},
                   "records=%s" % (records[-3:] or [],))


def sidechain_routes(session, source, dest):
    """Every sidechain route the report lists from -> to."""
    routes = ((session.result("pdc.report").get("sidechain") or {}).get("routes")) or []
    return [route for route in routes
            if route.get("from") == source and route.get("to") == dest]


def check_sidechain(session, recorder, ends):
    """A sidechain send is a real route with a tap point, and undo takes it back."""
    if ends is None:
        return
    first, second = ends
    made = session.result("mixer.sidechain_to",
                          {"channel": first, "to": second, "amount": 0.75,
                           "tap_point": "post_fader_no_gain"})
    mine = sidechain_routes(session, first, second)
    dialled = subset(mine[0] if mine else None, ("tap_point", "amount"))
    recorder.check("the sidechain route is reported with the tap point asked for",
                   made.get("tap_point") == "post_fader_no_gain"
                   and dialled == {"tap_point": "post_fader_no_gain", "amount": 0.75},
                   "made=%r routes=%r" % (made, mine))
    session.result("control.undo")
    recorder.check("control.undo removes the sidechain route as one step",
                   sidechain_routes(session, first, second) == [], "after=%r" % (mine,))


def check_feedback_refusal(session, recorder, ends):
    """The engine's feedback rule is the refusal, and it writes nothing."""
    if ends is None:
        return
    first, second = ends
    session.result("mixer.route_to", {"channel": first, "to": second})
    back = session.typed_error("mixer.route_to", {"channel": second, "to": first})
    recorder.check("routing back into a channel that routes to you is refused, typed",
                   back.get("kind") == "refused" and bool(back.get("message")),
                   "error=%r" % (back,))
    itself = session.typed_error("mixer.route_to", {"channel": first, "to": first})
    recorder.check("a channel cannot route to itself",
                   itself.get("kind") == "invalid_args", "error=%r" % (itself,))
    recorder.check("the refusals left the graph alone",
                   send_to(channel_named(session.result("mixer.get_state"), second), first) is None
                   and send_to(channel_named(session.result("mixer.get_state"), first),
                               first) is None,
                   "second=%r" % (channel_named(session.result("mixer.get_state"), second),))
    session.result("control.undo")


def load_catalogue(session):
    catalogues = session.result("plugin.list")
    if catalogues.get("devices"):
        return catalogues
    return session.result("plugin.list", {"kind": "effect"})


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
        print("FAIL: the routing socket proof has %d failed check(s)" % len(problems))
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

        check_chain_graph(session, load_catalogue(session), recorder)
        check_rack_graph(session, recorder)
        ends = check_route_and_send(session, recorder)
        check_bus_prefader_rule(session, recorder, ends)
        check_route_remove_and_undo(session, recorder, ends)
        check_sidechain(session, recorder, ends)
        check_feedback_refusal(session, recorder, ends)
        check_quit(session, instance, recorder)

    transcript.dump()
    code = report(recorder)
    if code == 0:
        H.ok("routing socket proof (every check held)")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
