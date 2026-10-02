#!/usr/bin/env python3
"""The 0.5.0 engine wiring (lane L01), driven end to end over the control socket.

The engine audit (research/daw-ui-specs/synthesis/engine-audit.md) found capabilities that were
built but not reachable. This driver starts the REAL `zene` binary headless and proves each one
is wired in:

  1. `plugin.load {position}` inserts a device at a slot instead of appending (dsp.get_state's
     order), and one control.undo removes it;
  2. `plugin.load {chain}` fills a parallel chain of a channel's rack (rack.add_chain), which
     before was an empty shell (E16); a track target asking for a rack chain, and a chain that
     does not exist, are refused, typed;
  3. `chain.save` / `chain.apply {chain}` capture and restore a rack chain (E17);
  4. `track.set_mixer_channel` routes a track into a channel, and one control.undo routes it
     back;
  5. `session.set_scene {tempo_override: false}` switches a scene's tempo override off again
     (E37: it used to be one-way);
  6. the Lua scripting spine: `script.run` with `zene.command(...)` drives the registry and
     answers a table; a sandbox-leaving command is refused inside the script.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-engine-wiring.py <zene>
Exit 0 only when every check held.
"""

import sys

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results


def builtin_effects(session):
    listed = session.result("plugin.list", {"kind": "effect", "loadable_only": True}).get("devices") or []
    return [d.get("id") for d in listed if d.get("format") == "builtin"]


def chain_ids(session, target, chain=0):
    chains = session.result("dsp.get_state", {"target": target}).get("chains") or []
    if chain >= len(chains):
        return []
    return [d.get("id") for d in chains[chain].get("devices") or []]


def check_position(session, recorder, devices):
    channel = session.result("mixer.add_channel").get("channel")
    first = session.result("plugin.load", {"target": channel, "device": devices[0]}).get("id")
    second = session.result("plugin.load", {"target": channel, "device": devices[1]}).get("id")
    inserted = session.result("plugin.load", {"target": channel, "device": devices[0], "position": 0})
    order = chain_ids(session, channel)
    recorder.check("plugin.load {position: 0} inserts in front",
                   inserted.get("index") == 0 and order == [inserted.get("id"), first, second], "order=%r" % order)
    session.result("control.undo")
    order = chain_ids(session, channel)
    recorder.check("one undo removes the inserted device", order == [first, second], "order=%r" % order)
    return channel


def check_rack_chain(session, recorder, devices, channel):
    session.result("rack.add_chain", {"channel": channel})
    loaded = session.call("plugin.load", {"target": channel, "device": devices[0], "chain": 1})
    rack = session.result("rack.get_state", {"channel": channel})
    counts = [c.get("device_count") for c in rack.get("chains") or []]
    recorder.check("plugin.load {chain: 1} fills the rack's parallel chain",
                   loaded.get("ok") and len(counts) > 1 and counts[1] == 1, "reply=%r rack=%r" % (loaded, counts))
    track = session.result("track.add", {"type": "instrument"}).get("track")
    refused = session.call("plugin.load", {"target": track, "device": devices[0], "chain": 1})
    recorder.check("a track has no rack chain: refused, typed",
                   (refused.get("error") or {}).get("kind") == "invalid_args", "reply=%r" % refused)
    missing = session.call("plugin.load", {"target": channel, "device": devices[0], "chain": 9})
    recorder.check("a chain that does not exist: not_found",
                   (missing.get("error") or {}).get("kind") == "not_found", "reply=%r" % missing)
    return track


def check_chain_presets(session, recorder, channel):
    saved = session.call("chain.save", {"target": channel, "chain": 1, "name": "l01-rack-chain", "overwrite": True})
    other = session.result("mixer.add_channel").get("channel")
    session.result("rack.add_chain", {"channel": other})
    applied = session.call("chain.apply", {"target": other, "chain": 1, "name": "l01-rack-chain"})
    counts = [c.get("device_count") for c in session.result("rack.get_state", {"channel": other}).get("chains") or []]
    recorder.check("chain.save/apply {chain} copy a rack chain to another channel's rack",
                   saved.get("ok") and applied.get("ok") and counts[1:2] == [1], "save=%r apply=%r counts=%r"
                   % (saved, applied, counts))


def check_track_channel(session, recorder, track):
    channel = session.result("mixer.add_channel").get("channel")
    reply = session.result("track.set_mixer_channel", {"track": track, "channel": channel})
    recorder.check("track.set_mixer_channel routes the track", reply.get("channel") == channel, "reply=%r" % reply)
    session.result("control.undo")
    again = session.result("track.set_mixer_channel", {"track": track, "channel": reply.get("previous")})
    recorder.check("one undo put the previous channel back (re-setting it changes nothing)",
                   again.get("previous") == reply.get("previous"), "after undo=%r" % again)


def check_scene_override(session, recorder):
    session.call("session.set_grid", {"tracks": 1, "scenes": 1})
    on = session.result("session.set_scene", {"scene": 0, "tempo": 150}).get("scene") or {}
    off = session.result("session.set_scene", {"scene": 0, "tempo_override": False}).get("scene") or {}
    recorder.check("a scene's tempo override can be switched off again (E37)",
                   on.get("tempo_enabled") is True and off.get("tempo_enabled") is False and off.get("tempo") == 150,
                   "on=%r off=%r" % (on, off))


def check_lua_spine(session, recorder):
    source = ("local r, err = zene.command('track.add', {type = 'instrument', name = 'Lua spine'})\n"
              "print('added ' .. tostring(err == nil) .. ' ' .. string.sub(r.track, 1, 4))\n"
              "local q, why, kind = zene.command('control.quit')\n"
              "print('quit ' .. tostring(q) .. ' ' .. tostring(kind))\n"
              "print('commands ' .. tostring(#zene.commands() > 300))\n")
    ran = session.result("script.run", {"source": source})
    lines = ran.get("log") or ran.get("lines") or ran.get("output") or []
    text = "\n".join(lines) if isinstance(lines, list) else str(lines)
    recorder.check("zene.command drives the registry from Lua", "added true trk-" in text, "log=%r" % ran)
    recorder.check("a sandbox-leaving command is refused inside a script", "quit nil refused" in text, "log=%r" % ran)
    recorder.check("zene.commands lists the registry", "commands true" in text, "log=%r" % ran)


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
        session = Session(client, transcript)
        devices = builtin_effects(session)
        # Every build carries the built-in effects (amplifier, bass booster, ...): fewer than two
        # means the catalogue or this filter broke, which is a failure, not a skip (review L01-1 #12).
        recorder.check("the catalogue lists at least two loadable built-in effects", len(devices) >= 2,
                       "devices=%r" % devices)
        if len(devices) < 2:
            report_results(recorder)
            recorder.problems.report("engine wiring control-surface driver")
            return 1
        channel = check_position(session, recorder, devices)
        track = check_rack_chain(session, recorder, devices, channel)
        check_chain_presets(session, recorder, channel)
        check_track_channel(session, recorder, track)
        check_scene_override(session, recorder)
        check_lua_spine(session, recorder)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("engine wiring control-surface driver")
        return 1
    H.ok("engine wiring control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
