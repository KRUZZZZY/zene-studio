#!/usr/bin/env python3
"""The Inspector and the Device panel, driven end to end over the control socket.

The shell's Inspector and Device panel (docs/UI-SHELL.md §8) follow one selection: a track, a
clip on it, or a mixer channel. `window.select` is the agent's click, and its reply is what the
two panels draw. This driver starts the REAL `zene` binary headless and proves:

  1. `window.select {track}` selects the track: the Inspector names it, and the Device panel
     draws the track's instrument and every device on its chain, in chain order;
  2. `window.select {clip}` selects the clip AND its track;
  3. `window.select {channel}` selects a mixer channel alone: the Device panel draws its
     inserts and the modulator driving one of them, and the Inspector names the channel;
  4. a device removed over the socket leaves the panel, and its undo draws it again: the
     panel follows the chain;
  5. an unknown id is refused, typed, and leaves the selection as it was;
  6. `window.get_state` reports the same selection as `selection`;
  7. `window.screenshot` renders the Arrange view with the Device panel shown;
  8. 0.5.0 L10: plugin.copy copies a device to another channel and chain.copy a chain, plugin.ab
     stores and toggles, plugin.organise marks a device and plugin.organisation lists it,
     plugin.paths_get answers the folders, rack.wrap wraps a chain, remote.* answers.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-devices.py <zene> [<screenshot dir>]
Exit 0 only when every check held.
"""

import os
import sys
import time

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results


def effects(session):
    listed = session.result("plugin.list", {"kind": "effect", "loadable_only": True}).get("devices") or []
    return [d.get("id") for d in listed if d.get("format") == "builtin"] or [d.get("id") for d in listed]


def load(session, target, devices):
    for device in devices:
        reply = session.call("plugin.load", {"target": target, "device": device})
        if reply.get("ok"):
            return (reply.get("result") or {}).get("id")
    return None


def plugins(reply):
    return [c.get("plugin") for c in (reply.get("devices") or {}).get("cards") or []]


def check_track(session, recorder, devices):
    track = session.result("track.add", {"type": "instrument", "name": "Lead"}).get("track")
    first, second = load(session, track, devices), load(session, track, devices)
    reply = session.result("window.select", {"track": track})
    recorder.check("window.select names the track in the Inspector",
                   reply.get("track") == track and (reply.get("inspector") or {}).get("name") == "Lead", "reply=%r" % reply)
    drawn = plugins(reply)
    recorder.check("the Device panel draws the track's devices in chain order",
                   [p for p in drawn if p != "inst"] == [first, second] and first is not None, "cards=%r" % drawn)
    return track, first


def check_clip(session, recorder, track):
    clip = session.result("clip.add", {"track": track, "position": 0}).get("clip")
    reply = session.result("window.select", {"clip": clip})
    recorder.check("selecting a clip selects its track", reply.get("clip") == clip and reply.get("track") == track,
                   "reply=%r" % reply)


def check_channel(session, recorder, devices):
    channel = session.result("mixer.add_channel").get("channel")
    effect = load(session, channel, devices)
    chains = session.result("dsp.get_state", {"target": channel}).get("chains") or [{}]
    parameter = ((chains[0].get("devices") or [{}])[0].get("parameters") or [{}])[-1].get("name")
    modulator = session.result("modulator.create", {"name": "Wobble", "rate": 2.0}).get("modulator")
    session.result("modulator.target_set", {"modulator": modulator, "channel": channel, "chain": 0, "effect": 0,
                                            "parameter": parameter, "depth": 0.3})
    reply = session.result("window.select", {"channel": channel})
    devices_state = reply.get("devices") or {}
    recorder.check("a mixer channel is selected on its own",
                   reply.get("channel") == channel and not reply.get("track"), "reply=%r" % reply)
    recorder.check("the Device panel draws the channel's insert and its modulator",
                   plugins(reply) == [effect] and devices_state.get("modulators") == [modulator],
                   "devices=%r" % devices_state)
    recorder.check("the Inspector names the channel", (reply.get("inspector") or {}).get("channel") == channel,
                   "inspector=%r" % reply.get("inspector"))
    return channel, effect


def check_follow(session, recorder, track, first):
    session.result("window.select", {"track": track})
    session.result("plugin.unload", {"target": track, "plugin": first})
    time.sleep(0.3)
    drawn = plugins(session.result("window.select"))
    recorder.check("a device removed over the socket leaves the panel", first not in drawn, "cards=%r" % drawn)
    session.result("control.undo")
    time.sleep(0.3)
    # The undo re-instantiates the device at its index under a NEW fx-<n> (fx-id renumbering is
    # by design, AGENTS.md): the card comes back first in the chain, whatever its id.
    redrawn = plugins(session.result("window.select"))
    recorder.check("its undo draws it again, first in the chain",
                   len(redrawn) == len(drawn) + 1 and redrawn[1:] == drawn, "cards=%r after %r" % (redrawn, drawn))


def check_refusal(session, recorder, track):
    reply = session.call("window.select", {"track": "trk-99999"})
    kind = (reply.get("error") or {}).get("kind")
    recorder.check("an unknown track is refused, typed", not reply.get("ok") and kind == "not_found", "reply=%r" % reply)
    state = session.result("window.get_state").get("selection") or {}
    recorder.check("the refusal leaves the selection, and get_state reports it", state.get("track") == track,
                   "selection=%r" % state)


def check_devices_l10(session, recorder, devices):
    a = session.result("mixer.add_channel").get("channel")
    b = session.result("mixer.add_channel").get("channel")
    fx = load(session, a, devices)
    if fx is None:
        recorder.check("an effect loads for the L10 checks", False, "no loadable effect")
        return
    copied = session.result("plugin.copy", {"target": a, "plugin": fx, "to": b})
    recorder.check("plugin.copy puts the device on another channel", len(copied.get("plugins") or []) == 1, "reply=%r" % copied)
    chained = session.result("chain.copy", {"target": a, "to": b, "replace": True})
    recorder.check("chain.copy replaces the destination's chain", chained.get("count") == 1, "reply=%r" % chained)
    session.result("plugin.ab", {"target": a, "plugin": fx, "action": "store", "slot": "a"})
    toggled = session.result("plugin.ab", {"target": a, "plugin": fx, "action": "toggle"})
    recorder.check("plugin.ab toggles between A and B", toggled.get("has_a") and toggled.get("has_b"), "reply=%r" % toggled)
    marked = session.result("plugin.organise", {"device": devices[0], "favourite": True, "folder": "Driver"})
    listed = session.result("plugin.organisation", {"folder": "Driver"})
    recorder.check("plugin.organise files a device and plugin.organisation lists it",
                   marked.get("favourite") and len(listed.get("entries") or []) == 1, "reply=%r" % listed)
    session.result("control.undo")
    paths = session.result("plugin.paths_get")
    recorder.check("plugin.paths_get answers every format", len(paths.get("formats") or []) == 3, "reply=%r" % paths)
    wrapped = session.result("rack.wrap", {"channel": a})
    recorder.check("rack.wrap moves the chain into a rack chain", wrapped.get("chain", 0) >= 1, "reply=%r" % wrapped)
    session.result("control.undo")
    remote = session.result("remote.state")
    recorder.check("remote.state answers with nothing connected", remote.get("connected") is False, "reply=%r" % remote)


def check_screenshot(session, recorder, outdir):
    session.result("window.set_view", {"view": "arrange"})
    panels = {p.get("id"): p for p in session.result("window.get_state").get("panels") or []}
    if not (panels.get("devices") or {}).get("visible"):
        session.result("window.toggle", {"editor": "devices"})
    time.sleep(0.3)
    path = os.path.join(outdir, "device-panel-arrange.png")
    shot = session.result("window.screenshot", {"path": path})
    recorder.check("the Arrange view renders with the Device panel", (shot.get("width") or 0) >= 1600
                   and os.path.exists(path), "reply=%r" % shot)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    with H.start_instance(argv[1], workingdir=None) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        outdir = os.path.abspath(argv[2]) if len(argv) > 2 else instance.tmp
        os.makedirs(outdir, exist_ok=True)
        session = Session(client, transcript)
        devices = effects(session)
        if not devices:
            H.ok("device panel control-surface driver: SKIPPED (this host loads no effect module)")
            return 0
        track, first = check_track(session, recorder, devices)
        check_clip(session, recorder, track)
        check_channel(session, recorder, devices)
        check_follow(session, recorder, track, first)
        check_refusal(session, recorder, track)
        check_devices_l10(session, recorder, devices)
        check_screenshot(session, recorder, outdir)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("device panel control-surface driver")
        return 1
    H.ok("device panel control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
