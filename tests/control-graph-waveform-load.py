#!/usr/bin/env python3
"""Q050-486 over the control socket: saved plug-in waveforms load exactly, or safely refuse.

Starts the REAL `zene` binary headless (its own throwaway config) and, on one fixture built
through the agent's own commands (an instrument track carrying Watsyn, a Wave Shaper effect on
it, one clip with one note):

  1. a project whose Watsyn `a1_wave` and Wave Shaper `waveShape` hold drawn (non-default)
     waveforms opens with no load error, renders audibly, saves those graphs back with identical
     float values, and reopens to a render equal to the first under the repository's audio
     comparison contract (tests/golden_audio_lib.py: the same-build run-to-run floor, then
     max |delta| LSB/dBFS, level and envelope each against its own tolerance); the NEGATIVE
     CONTROL renders the default waveforms and requires that comparison to FAIL, so it can;
  2. the same project with `a1_wave` truncated by one value and `waveShape` carrying a NaN opens
     without crashing, reports one diagnostic per damaged graph in project.open's `errors`,
     keeps both plug-ins' default graphs (saved back and compared value for value), and still
     renders usable, finite, audible audio.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-graph-waveform-load.py <zene>
Exit 0 only when every check held; 77 (ctest Skipped) when the build has no watsyn or
waveshaper module, because a fixture without them proves nothing.
"""

import base64
import math
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET

import control_socket_harness as H
import golden_audio_lib as G
from freeze_bounce_evidence import Recorder, Session, SILENT_DBFS, report_results

INSTRUMENT, EFFECT = "watsyn", "waveshaper"
WAVE_ATTRIBUTE, SHAPE_ATTRIBUTE = "a1_wave", "waveShape"
CLIP_TICKS = 192


def device_id(session, kind, name):
    listed = session.result("plugin.list", {"kind": kind, "loadable_only": True}).get("devices") or []
    for device in listed:
        if str(device.get("name", "")).lower() == name or str(device.get("id", "")).lower() == name:
            return device.get("id")
    return None


def build_fixture(session):
    """An instrument track with Watsyn, a Wave Shaper on it, one clip with one note. True when built."""
    instrument, effect = device_id(session, "instrument", INSTRUMENT), device_id(session, "effect", EFFECT)
    if instrument is None or effect is None:
        print("this build lists no loadable %s/%s module" % (INSTRUMENT, EFFECT))
        return False
    track = session.result("track.add", {"type": "instrument", "name": "Wave"}).get("track")
    loaded = session.result("plugin.load", {"target": track, "device": instrument})
    shaper = session.result("plugin.load", {"target": track, "device": effect})
    clip = session.result("clip.add", {"track": track, "position": 0, "length": CLIP_TICKS}).get("clip")
    session.result("note.add", {"clip": clip, "key": 57, "position": 0, "length": CLIP_TICKS // 2, "velocity": 120})
    return bool(track and clip and not loaded.get("error") and not shaper.get("error"))


def floats(encoded):
    raw = base64.b64decode(encoded)
    return list(struct.unpack("<%df" % (len(raw) // 4), raw))


def encode(values):
    return base64.b64encode(struct.pack("<%df" % len(values), *values)).decode("ascii")


def read_project(path):
    with open(path, "rb") as handle:
        blob = handle.read()
    if not blob.lstrip().startswith(b"<"):
        raise H.Timeout("%s is not plain XML (a .mmp is expected to be)" % path)
    return blob.decode("utf-8")


def graph_element(tree, tag):
    found = [e for e in tree.iter(tag)]
    return found[0] if found else None


def rewrite(source, target, changes):
    """Copy a saved project, replacing attributes: changes = {(tag, attribute): value}."""
    text = read_project(source)
    for (tag, attribute), value in changes.items():
        pattern = re.compile(r'(<%s\b[^>]*?\s%s=")[^"]*(")' % (re.escape(tag), re.escape(attribute)))
        text, count = pattern.subn(lambda m: m.group(1) + value + m.group(2), text, count=1)
        if count != 1:
            raise H.Timeout("no %s %s attribute in %s" % (tag, attribute, source))
    with open(target, "w", encoding="utf-8") as handle:
        handle.write(text)


def saved_graphs(path):
    tree = ET.fromstring(read_project(path))
    wave, shaper = graph_element(tree, "watsyn"), graph_element(tree, "waveshapercontrols")
    return (floats(wave.get(WAVE_ATTRIBUTE)) if wave is not None else None,
            floats(shaper.get(SHAPE_ATTRIBUTE)) if shaper is not None else None)


def render(session, path):
    reply = session.result("render.render", {"out": path, "format": "wav"})
    if not reply.get("path"):
        return None
    wav = G.read_wav(path)
    return {"frames": wav["frames"], "dbfs": G.dbfs(G._rms(wav["flat"])),
            "finite": all(math.isfinite(v) for v in wav["flat"])}


def drawn_waveforms(count_wave, count_shape):
    wave = [0.9 * (2.0 * ((i * 3) % count_wave) / count_wave - 1.0) for i in range(count_wave)]  # a folded saw
    shape = [math.sqrt((i + 1.0) / count_shape) for i in range(count_shape)]  # a soft-knee curve
    return wave, shape


def check_valid_round_trip(session, recorder, work, base, defaults):
    wave, shape = drawn_waveforms(len(defaults[0]), len(defaults[1]))
    drawn = os.path.join(work, "drawn.mmp")
    rewrite(base, drawn, {("watsyn", WAVE_ATTRIBUTE): encode(wave), ("waveshapercontrols", SHAPE_ATTRIBUTE): encode(shape)})
    # struct packs doubles to float32: compare against what the file actually holds.
    wave, shape = saved_graphs(drawn)

    opened = session.result("project.open", {"path": drawn})
    recorder.check("a project with drawn waveforms opens with no load error",
                   opened.get("file") and opened.get("error_count") == 0, "reply=%r" % opened)
    first, second = os.path.join(work, "drawn-1.wav"), os.path.join(work, "drawn-2.wav")
    measured = render(session, first)
    render(session, second)
    recorder.check("it renders usable audio (audible, finite)", usable(measured), "render=%r" % measured)

    resaved = os.path.join(work, "drawn-resaved.mmp")
    session.result("project.save", {"path": resaved})
    saved_wave, saved_shape = saved_graphs(resaved)
    recorder.check("both drawn graphs are saved back with identical float values",
                   saved_wave == wave and saved_shape == shape,
                   "wave equal=%s shape equal=%s" % (saved_wave == wave, saved_shape == shape))

    reopened = session.result("project.open", {"path": resaved})
    again = os.path.join(work, "resaved.wav")
    render(session, again)
    floor = G.measure_floor([first, second])
    tolerance = G.tolerance(floor)
    passed, lines = G.verdict(G.compare(first, again), tolerance)
    print("reopened vs first render (floor %.3f LSB):\n%s" % (floor["max_delta_lsb"], "\n".join(lines)))
    recorder.check("the reopened project renders equal to the first within the measured floor",
                   reopened.get("error_count") == 0 and passed, "\n".join(lines))

    # Negative control: the default waveforms sound different, and the same comparison must say so.
    session.result("project.open", {"path": base})
    plain = os.path.join(work, "default.wav")
    render(session, plain)
    differs, control_lines = G.verdict(G.compare(first, plain), tolerance)
    print("negative control, default waveforms vs drawn:\n%s" % "\n".join(control_lines))
    recorder.check("negative control: the comparison fails a render with the default waveforms",
                   not differs, "\n".join(control_lines))
    return drawn, wave


def reported(messages, owner, attribute):
    """How many load errors name this plug-in and this graph attribute."""
    return sum(1 for m in messages if m.startswith(owner + ":") and '"%s"' % attribute in m)


def usable(measured):
    return measured is not None and measured["finite"] and measured["dbfs"] > SILENT_DBFS


def check_damaged(session, recorder, instance, work, drawn, wave, defaults):
    damaged = os.path.join(work, "damaged.mmp")
    shape_with_nan = list(defaults[1])
    shape_with_nan[7] = float("nan")
    rewrite(drawn, damaged, {("watsyn", WAVE_ATTRIBUTE): encode(wave[:-1]),
                             ("waveshapercontrols", SHAPE_ATTRIBUTE): encode(shape_with_nan)})
    opened = session.result("project.open", {"path": damaged})
    messages = [e.get("message", "") for e in opened.get("errors") or []]
    alive = session.result("control.ping").get("error") is None
    recorder.check("the damaged project opens and the instance is still answering",
                   bool(opened.get("file")) and alive, "reply=%r" % opened)
    recorder.check("project.open reports the truncated Watsyn wave",
                   reported(messages, "Watsyn", WAVE_ATTRIBUTE) == 1, "errors=%r" % messages)
    recorder.check("project.open reports the Wave Shaper curve with a NaN",
                   reported(messages, "Wave Shaper", SHAPE_ATTRIBUTE) == 1, "errors=%r" % messages)

    measured = render(session, os.path.join(work, "damaged.wav"))
    recorder.check("the damaged project still renders usable audio (audible, finite)", usable(measured),
                   "render=%r" % measured)
    resaved = os.path.join(work, "damaged-resaved.mmp")
    session.result("project.save", {"path": resaved})
    kept = saved_graphs(resaved)
    recorder.check("both damaged graphs keep the plug-ins' default waveforms", kept == defaults,
                   "wave kept=%s shape kept=%s" % (kept[0] == defaults[0], kept[1] == defaults[1]))
    log = instance.read_log()
    recorder.check("no sanitizer or crash report in the instance log",
                   "AddressSanitizer" not in log and "Segmentation" not in log, "log tail=%r" % log[-400:])


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
        if not build_fixture(session):
            transcript.dump()
            return 77
        work = instance.workspace
        base = os.path.join(work, "base.mmp")
        session.result("project.save", {"path": base})
        defaults = saved_graphs(base)
        if defaults[0] is None or defaults[1] is None:
            transcript.dump()
            H.fail("the saved fixture carries no watsyn/waveshapercontrols element", instance, transcript)
        drawn, wave = check_valid_round_trip(session, recorder, work, base, defaults)
        check_damaged(session, recorder, instance, work, drawn, wave, defaults)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("graph waveform load control-surface driver")
        return 1
    H.ok("graph waveform load control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
