#!/usr/bin/env python3
"""Q050-066 stage 2 over the control socket: each drum-rack pad's effect chain is its own, in state and in sound.

Starts the REAL `zene` binary headless (its own throwaway config) and drives it as an agent does:

  1. a two-pad rack (track.drum_rack), one note on each pad; track.drum_rack_get_state reads both chains
     empty and each pad's chain as its own (chain_shared false);
  2. an amplifier is loaded on the KICK pad's channel (plugin.load, target = the pad's `channel`) and turned
     to 0 % (plugin.param_set): the read-back lists it on the kick's chain and on no other pad's;
  3. SOUND: the rack now renders equal to the rack with the kick muted and no effect anywhere (the snare
     alone) under the repository's audio comparison contract (tests/golden_audio_lib.py) - so the kick's
     chain processed the kick, and the snare passed through untouched. NEGATIVE CONTROL: the untouched
     rack does not render like the snare alone, so that comparison can fail;
  4. undo, one step each: control.undo puts the setting back (the rack renders as before) and control.redo
     makes it again (the snare alone); a second undo takes the effect itself away (every chain empty, the
     rack renders as before) and its redo puts it back on the kick's chain only;
  5. the chains survive a save and reopen: the kick's amplifier, the snare's empty chain, and the sound.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-drum-rack-chains.py <zene>
Exit 0 only when every check held.
"""

import os
import sys

import control_socket_harness as H
import golden_audio_lib as G
from freeze_bounce_evidence import Recorder, Session, SILENT_DBFS, report_results

SAMPLES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "samples", "drums")
CLIP_TICKS = 192


def sample(name):
    return os.path.normpath(os.path.join(SAMPLES, name))


def note_on_pad(session, pad, key, position):
    clip = session.result("clip.add", {"track": pad, "position": 0, "length": CLIP_TICKS * 2}).get("clip")
    session.result("note.add", {"clip": clip, "key": key, "position": position, "length": 48, "velocity": 110})


def pads_of(session, folder):
    return session.result("track.drum_rack_get_state", {"track": folder}).get("pads", [])


def chains(session, folder):
    """Each pad's chain as its plug-in names, in pad order."""
    return [[effect.get("plugin") for effect in pad.get("chain", [])] for pad in pads_of(session, folder)]


def render(session, path):
    reply = session.result("render.render", {"out": path, "format": "wav"})
    return path if reply.get("path") else None


def same_sound(recorder, name, floor_paths, first, other, expect_equal=True):
    if not first or not other:
        recorder.check(name, False, "a render failed: %r %r" % (first, other))
        return
    tolerance = G.tolerance(G.measure_floor(floor_paths))
    passed, lines = G.verdict(G.compare(first, other), tolerance)
    print("%s:\n%s" % (name, "\n".join(lines)))
    recorder.check(name, passed if expect_equal else not passed, "\n".join(lines))


def amplifier_device(session):
    for device in session.result("plugin.list", {"kind": "effect", "format": "builtin", "loadable_only": True}).get("devices") or []:
        if str(device.get("name", "")).lower() == "amplifier":
            return device.get("id")
    return None


def build_rack(session, recorder, work):
    """The rack, its notes and the reference renders: (folder, kick, snare, floor renders, snare-alone render)."""
    made = session.result("track.drum_rack", {"name": "Kit", "pads": [
        {"sample": sample("bassdrum01.ogg"), "name": "Kick"}, {"sample": sample("snare01.ogg"), "name": "Snare", "key": 38}]})
    if made.get("error"):
        return None
    folder = made["folder"]
    note_on_pad(session, made["tracks"][0], 36, 0)
    note_on_pad(session, made["tracks"][1], 38, CLIP_TICKS // 2)
    kick, snare = pads_of(session, folder)
    recorder.check("both pads read back an empty chain of their own",
                   [(p.get("chain"), p.get("chain_shared")) for p in (kick, snare)] == [([], False), ([], False)],
                   "pads=%r" % [kick, snare])
    floor = [render(session, os.path.join(work, "rack-1.wav")), render(session, os.path.join(work, "rack-2.wav"))]
    level = G.file_dbfs(floor[0])[1] if floor[0] else None
    recorder.check("the rack renders audible audio", level is not None and level > SILENT_DBFS, "dBFS=%r" % level)
    session.result("track.set_mute", {"track": kick["track"], "muted": True})
    alone = render(session, os.path.join(work, "snare-alone.wav"))
    session.result("track.set_mute", {"track": kick["track"], "muted": False})
    same_sound(recorder, "negative control: the whole rack does not render like the snare alone", floor, floor[0], alone,
               expect_equal=False)
    return folder, kick, snare, floor, alone


def silence_kick(session, recorder, kick, folder):
    """An amplifier at 0 % on the kick's own chain. Returns its fx id."""
    device = amplifier_device(session)
    if device is None:
        recorder.check("the build lists a loadable amplifier module", False, "plugin.list has none")
        return None
    loaded = session.result("plugin.load", {"target": kick["channel"], "device": device})
    fx = loaded.get("id")
    turned = session.result("plugin.param_set", {"target": kick["channel"], "plugin": fx, "name": "Volume", "value": 0})
    recorder.check("the amplifier loads on the kick pad's channel and turns to 0 %",
                   bool(fx) and not turned.get("error"), "load=%r set=%r" % (loaded, turned))
    pads = pads_of(session, folder)
    recorder.check("the read-back lists the amplifier on the kick's chain and on no other pad's",
                   [[e.get("id") for e in p.get("chain", [])] for p in pads] == [[fx], []], "pads=%r" % pads)
    return fx


def check_undo_redo(session, recorder, work, folder, kick, floor, alone):
    """Each chain edit is one undo step: the setting, then the effect itself."""
    session.result("control.undo")
    same_sound(recorder, "one control.undo puts the kick's amplifier back to 100 %: the rack renders as before", floor,
               floor[0], render(session, os.path.join(work, "setting-undone.wav")))
    session.result("control.redo")
    same_sound(recorder, "control.redo turns it to 0 % again: the rack renders as the snare alone", floor, alone,
               render(session, os.path.join(work, "setting-redone.wav")))
    session.result("control.undo")
    session.result("control.undo")
    recorder.check("a second control.undo takes the effect itself away: every chain is empty",
                   chains(session, folder) == [[], []], "chains=%r" % chains(session, folder))
    same_sound(recorder, "with the effect undone the rack renders as it did before it", floor, floor[0],
               render(session, os.path.join(work, "effect-undone.wav")))
    session.result("control.redo")
    recorder.check("control.redo puts the amplifier back on the kick's chain only",
                   chains(session, folder) == [["amplifier"], []], "chains=%r" % chains(session, folder))
    # A redone load is a new instance at its defaults (plugin.load's redo), so the setting is made again on it.
    redone = (pads_of(session, folder) or [{}])[0].get("chain") or [{}]
    fx = redone[0].get("id")
    if not fx:
        recorder.check("the redone amplifier is in the kick's read-back chain", False, "chain=%r" % redone)
        return
    session.result("plugin.param_set", {"target": kick["channel"], "plugin": fx, "name": "Volume", "value": 0})
    same_sound(recorder, "the redone amplifier at 0 % silences the kick only again", floor, alone,
               render(session, os.path.join(work, "effect-redone.wav")))


def check_round_trip(session, recorder, work, folder, floor, alone):
    saved = os.path.join(work, "chains.mmp")
    session.result("project.save", {"path": saved})
    session.result("control.undo")  # the file, not memory, must answer after the reopen
    opened = session.result("project.open", {"path": saved})
    recorder.check("the saved rack opens with no load error", opened.get("error_count") == 0, "reply=%r" % opened)
    recorder.check("the reopened rack reads the kick's amplifier and the snare's empty chain",
                   chains(session, folder) == [["amplifier"], []], "chains=%r" % chains(session, folder))
    same_sound(recorder, "the reopened rack renders as the snare alone", floor, alone,
               render(session, os.path.join(work, "reopened.wav")))


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
        listed = session.result("plugin.list", {"kind": "instrument", "loadable_only": True}).get("devices") or []
        if not any(str(d.get("name", "")).lower() == "audiofileprocessor" for d in listed):
            H.fail("build lists no loadable audiofileprocessor module", instance, transcript)
        work = instance.workspace
        rack = build_rack(session, recorder, work)
        if rack is None:
            transcript.dump()
            H.fail("track.drum_rack refused the fixture", instance, transcript)
        folder, kick, _snare, floor, alone = rack
        fx = silence_kick(session, recorder, kick, folder)
        if fx is not None:
            same_sound(recorder, "the kick's chain silences the kick only: the rack renders as the snare alone", floor,
                       alone, render(session, os.path.join(work, "kick-silenced.wav")))
            check_undo_redo(session, recorder, work, folder, kick, floor, alone)
            check_round_trip(session, recorder, work, folder, floor, alone)
        log = instance.read_log()
        recorder.check("no sanitizer or crash report in the instance log",
                       "AddressSanitizer" not in log and "Segmentation" not in log, "log tail=%r" % log[-400:])
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("drum rack pad chains control-surface driver")
        return 1
    H.ok("drum rack pad chains control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
