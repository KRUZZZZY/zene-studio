#!/usr/bin/env python3
"""RAW control-surface transcript for freeze.bus / freeze.unfreeze_bus (Q050-124).

Drives the REAL binary headless - the bus render is the shipped child render,
`render ... --capture-channel <id>` - and prints every request and reply verbatim.

The fixture is built through the commands an agent has: an audible instrument
track whose channel feeds a BUS (never the master directly), the bus panned so
its own processing is in the take. Then, in order:

  1. renders the live session as the reference;
  2. freezes the bus and requires the take reported, its file a non-silent
     32-bit WAV and bus.list's readback to say frozen and loaded;
  3. DELETES the source note and renders again: with the source silent only the
     take can make a sound, and it must match the reference sample by sample
     (to well below the 16-bit render's own quantisation of a different signal);
  4. puts the note back, saves the project, finds the take on the bus's own
     <mixerchannel> element, reopens it and requires the freeze and its audio
     to survive, and the reopened session to still match the reference;
  5. unfreezes (the reference again), undoes the unfreeze with ONE control.undo
     (the same take back), and unfreezes again;
  6. drives the refusals (not a bus, the master, an unknown channel, unfreezing
     an unfrozen bus) and reads the A16 records the verbs left.

Usage: QT_QPA_PLATFORM=offscreen python3 control-bus-freeze-transcript.py <zene>
Exit 0 only when every check held; 77 (ctest Skipped, never Passed) when this
build has no loadable instrument to make an audible fixture with.
"""

import array
import math
import os
import struct
import sys

import control_socket_harness as H
from freeze_bounce_evidence import (SILENT_DBFS, Recorder, Session, load_audible_instrument,
                                    report_on_abort, report_results, wav_measure)

CLIP_TICKS = 192
# The frozen session must match the reference this closely, in 16-bit steps. A take that was not the bus's output
# (or one shifted by a period) differs by thousands; the same audio differs by the render's rounding alone.
MAX_SAMPLE_STEPS = 3  # a TPDF-dithered 16-bit render may differ by a step or two between runs


def riff_chunks(data):
    """The RIFF chunks after the WAVE header, by id."""
    offset, chunks = 12, {}
    while offset + 8 <= len(data):
        size = struct.unpack("<I", data[offset + 4:offset + 8])[0]
        chunks.setdefault(data[offset:offset + 4], data[offset + 8:offset + 8 + size])
        offset += 8 + size + (size & 1)
    return chunks


def fmt_chunk(body):
    """(format tag, bytes per sample, channels) of a fmt chunk, or None."""
    if body is None or len(body) < 16:
        return None
    tag, channels = struct.unpack("<HH", body[:4])
    bits = struct.unpack("<H", body[14:16])[0]
    if tag == 0xFFFE and len(body) >= 26:  # WAVE_FORMAT_EXTENSIBLE: the sub-format's first two bytes
        tag = struct.unpack("<H", body[24:26])[0]
    return tag, bits // 8, channels


def read_wav(path):
    """(format tag, bytes per sample, channels, samples) of a PCM-16 or IEEE-float WAV; `wave` reads no float."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("%s is not a WAV file" % path)
    chunks = riff_chunks(data)
    fmt, payload = fmt_chunk(chunks.get(b"fmt ")), chunks.get(b"data")
    if fmt is None or payload is None:
        raise ValueError("%s has no fmt or data chunk" % path)
    tag, width, channels = fmt
    values = array.array("f" if tag == 3 else "h")
    values.frombytes(payload[:len(payload) - len(payload) % values.itemsize])
    return tag, width, channels, values


def samples(path):
    tag, width, _, values = read_wav(path)
    return values, (4 if tag == 3 else width)


def take_measure(path):
    """The take's frames, bytes per sample and RMS in dBFS."""
    tag, width, channels, values = read_wav(path)
    scale = 1.0 if tag == 3 else 32768.0
    total = sum(float(v) * float(v) for v in values)
    level = 20.0 * math.log10(math.sqrt(total / max(len(values), 1)) / scale + 1e-20)
    return len(values) // max(channels, 1), width, level


def max_steps(path_a, path_b):
    """Largest per-sample difference of two renders, in 16-bit steps (inf when the shapes differ)."""
    a, width = samples(path_a)
    b, _ = samples(path_b)
    if len(a) != len(b) or not len(a):
        return float("inf")
    scale = 1.0 if width == 2 else 32768.0
    steps = max(abs(x - y) for x, y in zip(a, b)) * scale
    print("compare %s vs %s: %d samples, max difference %.1f steps" % (os.path.basename(path_a),
                                                                       os.path.basename(path_b), len(a), steps))
    return steps


def render(session, path):
    rendered = session.result("render.render", {"out": path, "format": "wav"})
    return rendered.get("path") == path and os.path.exists(path)


def master_id(session):
    channels = session.result("mixer.get_state").get("channels") or []
    return channels[0].get("id") if channels else None


def bus_entry(session, bus):
    for entry in session.result("bus.list").get("buses") or []:
        if entry.get("id") == bus:
            return entry
    return {}


def build_fixture(session, instance, transcript):
    # The channels FIRST: a track made before a channel exists cannot be routed to it headless (its channel model is
    # ranged over the channels that existed when it was made; track.set_mixer_channel then answers the new index but
    # the track keeps playing to the master - the saved file says mixch="0").
    source = session.result("mixer.add_channel").get("channel")
    bus = session.result("bus.create").get("channel")
    master = master_id(session)
    added = session.result("track.add", {"type": "instrument", "name": "Bus Source"})
    track = added.get("track")
    if not track:
        H.fail("track.add returned no track id (%r)" % added, instance, transcript)
    if not load_audible_instrument(session, track):
        print("kinds: %r" % session.result("plugin.list").get("counts_by_kind"))
        return None
    clip = session.result("clip.add", {"track": track, "position": 0, "length": CLIP_TICKS}).get("clip")
    note = session.result("note.add", {"clip": clip, "key": 60, "position": 0, "length": CLIP_TICKS // 2,
                                       "velocity": 120})
    session.result("track.set_mixer_channel", {"track": track, "channel": source})
    session.result("mixer.route_to", {"channel": source, "to": bus})
    session.call("mixer.route_remove", {"channel": source, "to": master})  # the bus is the only way out
    session.result("mixer.set_pan", {"channel": bus, "pan": 0.5})
    return {"track": track, "clip": clip, "note": note.get("note"), "source": source, "bus": bus, "master": master}


def check_routing(session, fixture, recorder, outdir):
    """The fixture really goes through the bus: the saved file routes the track to the source channel."""
    path = os.path.join(outdir, "fixture.mmp")
    session.result("project.save", {"path": path})
    with open(path, errors="replace") as handle:
        text = handle.read()
    index = session.result("track.get_state", {"track": fixture["track"]}).get("mixer_channel_index")
    channels = session.result("mixer.get_state").get("channels") or []
    source_index = next((c.get("index") for c in channels if c.get("id") == fixture["source"]), None)
    recorder.check("the track plays into the channel that feeds the bus",
                   source_index is not None and ('mixch="%d"' % source_index) in text,
                   "source index %r, track state index %r" % (source_index, index))


def check_freeze(session, fixture, recorder, outdir):
    out = os.path.join(outdir, "bus-take.wav")
    frozen = session.result("freeze.bus", {"channel": fixture["bus"], "out": out})
    recorder.check("freeze.bus freezes the bus and reports its take",
                   frozen.get("frozen") is True and frozen.get("audio") == out and frozen.get("audio_ready") is True,
                   "%r" % frozen)
    exists = os.path.exists(out)
    recorder.check("the take file exists", exists, out)
    if exists:
        frames, width, level = take_measure(out)
        print("take: %d frames, %d-byte samples, %.2f dBFS" % (frames, width, level))
        recorder.check("the take is a non-silent float render of the bus",
                       frames == int(frozen.get("frames", -1)) and width == 4 and level > SILENT_DBFS,
                       "frames=%d reported=%r width=%d dBFS=%.2f" % (frames, frozen.get("frames"), width, level))
    entry = bus_entry(session, fixture["bus"])
    recorder.check("bus.list reads the freeze back",
                   entry.get("frozen") is True and entry.get("frozen_audio") == out
                   and entry.get("frozen_audio_ready") is True, "%r" % entry)
    return out


def check_take_plays(session, fixture, recorder, outdir, reference):
    removed = session.result("note.remove", {"clip": fixture["clip"], "note": fixture["note"]})
    recorder.check("the source note is removed", removed.get("note_count") == 0, "%r" % removed)
    path = os.path.join(outdir, "frozen-no-source.wav")
    ok = render(session, path)
    steps = max_steps(path, reference) if ok else float("inf")
    recorder.check("with the source silent the frozen bus renders the reference",
                   steps <= MAX_SAMPLE_STEPS, "max difference %.1f steps" % steps)
    session.result("control.undo")  # the note back
    notes = session.result("roll.get_state", {"clip": fixture["clip"]}).get("notes") or []
    recorder.check("control.undo puts the source note back", len(notes) == 1, "notes=%d" % len(notes))


def check_save_open(session, fixture, recorder, outdir, reference, take):
    project = os.path.join(outdir, "bus-frozen.mmp")
    saved = session.result("project.save", {"path": project})
    recorder.check("project.save wrote the session", saved.get("file") == project and os.path.exists(project),
                   "%r" % saved)
    with open(project, errors="replace") as handle:
        text = handle.read()
    recorder.check("the saved bus carries frozenAudio on its <mixerchannel>",
                   'frozenAudio="%s"' % take in text, "frozenAudio present=%r" % ("frozenAudio=" in text))
    reopened = session.result("project.open", {"path": project})
    recorder.check("project.open reloads it", reopened.get("file") == project, "%r" % reopened)
    entry = bus_entry(session, fixture["bus"])
    recorder.check("the freeze survives save/open, audio loaded",
                   entry.get("frozen") is True and entry.get("frozen_audio_ready") is True, "%r" % entry)
    path = os.path.join(outdir, "reopened.wav")
    steps = max_steps(path, reference) if render(session, path) else float("inf")
    recorder.check("the reopened frozen session renders the reference", steps <= MAX_SAMPLE_STEPS,
                   "max difference %.1f steps" % steps)


def check_unfreeze_and_undo(session, fixture, recorder, outdir, reference, take):
    thawed = session.result("freeze.unfreeze_bus", {"channel": fixture["bus"]})
    recorder.check("freeze.unfreeze_bus unfreezes", thawed.get("frozen") is False, "%r" % thawed)
    recorder.check("unfreezing leaves the take on disk", os.path.exists(take), take)
    path = os.path.join(outdir, "unfrozen.wav")
    steps = max_steps(path, reference) if render(session, path) else float("inf")
    recorder.check("unfrozen, the live bus renders the reference", steps <= MAX_SAMPLE_STEPS,
                   "max difference %.1f steps" % steps)
    depth = session.result("control.undo_depth").get("depth")
    session.result("control.undo")
    after = session.result("control.undo_depth").get("depth")
    entry = bus_entry(session, fixture["bus"])
    recorder.check("ONE control.undo puts the same take back",
                   entry.get("frozen") is True and entry.get("frozen_audio") == take
                   and entry.get("frozen_audio_ready") is True
                   and isinstance(depth, int) and isinstance(after, int) and depth - after == 1,
                   "entry=%r depth %r -> %r" % (entry, depth, after))
    session.result("freeze.unfreeze_bus", {"channel": fixture["bus"]})


def check_refusals(session, fixture, recorder):
    for label, command, args, kind in (
            ("a channel that is not a bus", "freeze.bus", {"channel": fixture["source"]}, "refused"),
            ("the master", "freeze.bus", {"channel": fixture["master"]}, "refused"),
            ("an unknown channel", "freeze.bus", {"channel": "ch-999999"}, "not_found"),
            ("unfreezing an unfrozen bus", "freeze.unfreeze_bus", {"channel": fixture["bus"]}, "refused")):
        error = session.typed_error(command, args)
        recorder.check("%s is refused (%s)" % (label, kind), error.get("kind") == kind, "%r" % error)


def check_transactions(session, recorder):
    records = [r for r in (session.result("control.transactions").get("transactions") or [])
               if r.get("command") in ("freeze.bus", "freeze.unfreeze_bus")]
    recorder.check("every bus freeze verb left a reversible true_inverse A16 record",
                   len(records) >= 3 and all(r.get("class") == "true_inverse" and r.get("reversible") is True
                                             for r in records),
                   "%r" % [(r.get("command"), r.get("class"), r.get("reversible")) for r in records])


def run_checks(session, instance, recorder, transcript):
    fixture = build_fixture(session, instance, transcript)
    if fixture is None:
        return None
    outdir = instance.tmp
    check_routing(session, fixture, recorder, outdir)
    reference = os.path.join(outdir, "reference.wav")
    recorder.check("the live reference renders", render(session, reference), reference)
    level = wav_measure(reference)[1] if os.path.exists(reference) else float("-inf")
    recorder.check("the reference is the bus's audio, not silence", level > SILENT_DBFS, "dBFS=%.2f" % level)
    take = check_freeze(session, fixture, recorder, outdir)
    check_take_plays(session, fixture, recorder, outdir, reference)
    check_save_open(session, fixture, recorder, outdir, reference, take)
    check_unfreeze_and_undo(session, fixture, recorder, outdir, reference, take)
    check_refusals(session, fixture, recorder)
    check_transactions(session, recorder)
    reply = session.call("control.quit")
    if reply.get("ok") is True:
        session.client.close()
        exited, code, _ = instance.wait_for_exit(H.QUIT_TIMEOUT)
        recorder.check("control.quit stops the instance", exited and code == 0, "code=%r" % code)
    return fixture


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    fixture = None
    with H.start_instance(argv[1]) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        session.result("control.version")
        try:
            fixture = run_checks(session, instance, recorder, transcript)
        except BaseException:
            report_on_abort(recorder, transcript, instance)
            raise
    report_results(recorder)
    transcript.dump()
    if fixture is None:
        H.ok("no audible instrument in this build: Skipped, never Passed")
        return 77
    if recorder.problems:
        recorder.problems.report("freeze.bus control-surface transcript")
        return 1
    H.ok("freeze.bus / freeze.unfreeze_bus control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
