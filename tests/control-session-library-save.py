#!/usr/bin/env python3
"""Q050-273.1 stage 1: an audio launcher cell saved into the shared library over the control socket, then
reused in another cell after the project is reopened - no window at any point (docs/050-features/Q050-273.1.md).

WHAT IT PROVES, on the real binary, through --control-socket:

  SAVE    a populated AUDIO cell is saved with browser.save_clip {track, scene}: the reply names the .wav in
          <user samples>/Clips, its kind and its byte count, the file exists, the tags asked for are on it,
          and the saved WAV decodes to the source file's own frames (rate, frame count and every sample
          within float tolerance) - the entry is the cell's audio, not a re-render of the cell's settings.
  READ    browser.query finds that entry under the samples root with kind audio; browser.tags carries the
          tag; a second save without overwrite is REFUSED and leaves the file alone; control.undo removes
          it and redo writes it back (one true-inverse step, read back from control.transactions).
  REOPEN  the project is saved, a new project is made, the saved one is reopened, and the entry is STILL
          found by browser.query; session.set_slot puts it in another audio cell, and that cell's source
          decodes to the same frames.
  FAIL    missing, empty, damaged, unsupported, over-decoded-limit, MIDI and empty-cell saves are all typed
          refusals that leave no file behind and do not replace the destination they were asked to overwrite.
          The decoded-size case starts an instance with ZENE_TEST_LIBRARY_SAVE_MAX_DECODED_BYTES between the
          source's ENCODED and DECODED size - the distinction the old input-size check missed - and the
          allocation and write-fault cases each start an instance with its own deterministic seam (a
          separate instance is how a per-process environment seam is set through the shared harness).
  GUI/AGENT the cell menu's Save to Library... (tested in SessionLibrarySaveTest) runs the same command;
          here the agent drives it with no window at all.

WHAT IT DOES NOT PROVE: rendering the reused cell's audio through the engine (SessionAudioCellPlaybackTest
and the live launcher tests own that); a MIDI cell (stage 2 of Q050-273: the command refuses one by name,
which this test checks).

Usage: QT_QPA_PLATFORM=offscreen python3 control-session-library-save.py <zene>
Exit 0 only when every check held.
"""

import math
import os
import struct
import sys
import tempfile
import wave

import control_socket_harness as H

REQUEST_IDS = iter(range(1, 2000))

#: The float tolerance when comparing decoded frames: a 16-bit PCM source through a float32 WAV is exact,
#: but the comparison is stated rather than assumed.
FRAME_TOLERANCE = 1e-6
TONE_HZ = 440.0
TONE_SECONDS = 0.5
TONE_RATE = 44100


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript

    def call(self, command, args=None):
        return self.client.call(next(REQUEST_IDS), command, args, transcript=self.transcript)

    def result(self, command, args=None):
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}


class Recorder:
    """Collects the named checks and their evidence."""

    def __init__(self):
        self.results = []
        self.problems = H.Problems()

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))
        if not passed:
            self.problems.add("%s (%s)" % (name, evidence))


def report_results(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-66s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)


def write_tone(path, frequency=TONE_HZ, seconds=TONE_SECONDS, rate=TONE_RATE):
    """A real 16-bit mono WAV, written here rather than by the code under test."""
    frames = int(seconds * rate)
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(rate)
        handle.writeframes(b"".join(
            struct.pack("<h", int(0.5 * 32767 * math.sin(2 * math.pi * frequency * i / rate)))
            for i in range(frames)))


def read_wav(path):
    """(rate, channels, frames) of a RIFF/WAVE file this test reads ITSELF, or None.

    The standard library's `wave` module reads integer PCM only, and the library writes 32-bit
    FLOAT (`SF_FORMAT_WAV | SF_FORMAT_FLOAT`, format tag 3), so the chunks are parsed here: it is
    the test's own reader, not the product's, and it handles both PCM and IEEE float.
    """
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError:
        return None
    if len(data) < 12 or data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        return None
    fmt = None
    payload = b""
    at = 12
    while at + 8 <= len(data):
        chunk_id = data[at:at + 4]
        size = struct.unpack("<I", data[at + 4:at + 8])[0]
        body = data[at + 8:at + 8 + size]
        if chunk_id == b"fmt ":
            fmt = body
        elif chunk_id == b"data":
            payload = body
        at += 8 + size + (size % 2)
    if fmt is None or len(fmt) < 16:
        return None
    tag, channels, rate, _, _, bits = struct.unpack("<HHIIHH", fmt[:16])
    if channels <= 0 or rate <= 0 or bits <= 0 or not payload:
        return None
    if tag == 3 and bits == 32:
        values = struct.unpack("<%df" % (len(payload) // 4), payload)
    elif tag == 3 and bits == 64:
        values = struct.unpack("<%dd" % (len(payload) // 8), payload)
    elif bits == 16:
        values = [value / 32768.0 for value in struct.unpack("<%dh" % (len(payload) // 2), payload)]
    elif bits == 24:
        values = []
        for i in range(0, len(payload), 3):
            raw = int.from_bytes(payload[i:i + 3], "little", signed=False)
            if raw & 0x800000:
                raw -= 1 << 24
            values.append(raw / 8388608.0)
    elif bits == 32:
        values = [value / 2147483648.0 for value in struct.unpack("<%di" % (len(payload) // 4), payload)]
    else:
        return None
    frames = [tuple(values[i:i + channels]) for i in range(0, len(values), channels)]
    return rate, channels, frames


def samples_root(session):
    """The absolute path of the browser's samples root, which is where the library's Clips folder lives."""
    for root in session.result("browser.roots").get("roots", []):
        if root.get("id") == "samples":
            return root.get("path", "")
    return ""


def compare(left, right):
    """The largest sample difference between two WAVs, in [0, 1]; None when either is unreadable, or
    when their rates/lengths differ. Mono is compared as the stereo the library writes (both channels
    equal), so a mono source and the stereo entry it became compare on the samples themselves."""
    a = read_wav(left)
    b = read_wav(right)
    if a is None or b is None:
        return None
    if a[0] != b[0] or len(a[2]) != len(b[2]):
        return None

    def stereo(frame):
        return (frame[0], frame[0]) if len(frame) == 1 else (frame[0], frame[1])

    worst = 0.0
    for frame_a, frame_b in zip(a[2], b[2]):
        sa = stereo(frame_a)
        sb = stereo(frame_b)
        for sample_a, sample_b in zip(sa, sb):
            worst = max(worst, abs(sample_a - sample_b))
    return worst


def decoded_frames(path):
    measured = read_wav(path)
    return len(measured[2]) if measured is not None else -1


def slot(session, track, scene):
    for cell in session.result("session.get_state").get("slots", []):
        if int(cell.get("track", -1)) == track and int(cell.get("scene", -1)) == scene:
            return cell
    return {}


def query(session, text):
    reply = session.result("browser.query", {"root": "samples", "text": text, "kind": "audio"})
    return reply, [item for item in reply.get("items", []) if item.get("name") == text]


def files_in(directory):
    try:
        return sorted(os.listdir(directory))
    except OSError:
        return []


def start(binary, extra_env=None):
    """One live instance in its own temp world, ready, with a connected client and its transcript."""
    instance = H.start_instance(binary, extra_env=extra_env)
    transcript = H.Transcript()
    H.wait_for_socket(instance)
    client = H.connect(instance)
    H.wait_ready(instance, client, transcript)
    return instance, Session(client, transcript), transcript


def populate(session, source, recorder):
    """A sample track, a 2x2 grid, the audio cell under test and a MIDI control cell."""
    added = session.result("track.add", {"type": "sample", "name": "Drums"})
    recorder.check("a sample track is added", "error" not in added, "%r" % added)
    recorder.check("the grid is built", "error" not in session.result("session.set_grid", {"tracks": 2, "scenes": 2}), "")
    recorder.check("the audio cell is populated",
                   "error" not in session.result("session.set_slot", {"track": 0, "scene": 0, "type": "audio",
                                                                      "source": source, "name": "kick"}),
                   "source=%s" % source)
    session.result("session.set_slot", {"track": 1, "scene": 0, "type": "midi", "pattern": 0, "name": "lead"})


def drive_save(session, recorder, source):
    """The ordinary save: the reply, the file, the frames, the tags and the one true-inverse step."""
    saved = session.result("browser.save_clip", {"track": 0, "scene": 0, "tags": ["drums", "one-shot"]})
    path = saved.get("path", "")
    recorder.check("browser.save_clip saves the audio cell", "error" not in saved and path.endswith("kick.wav"),
                   "%r" % saved)
    recorder.check("the reply says the entry is audio", saved.get("kind") == "audio", "%r" % saved.get("kind"))
    recorder.check("the reply's byte count is the file's", int(saved.get("bytes", -1)) ==
                   (os.path.getsize(path) if os.path.isfile(path) else -2), "%r" % saved.get("bytes"))
    recorder.check("the reply carries the tags added", saved.get("tags") == ["drums", "one-shot"],
                   "%r" % saved.get("tags"))
    worst = compare(source, path) if path else None
    recorder.check("the saved WAV holds the source's own frames (rate, count, every sample)",
                   worst is not None and worst <= FRAME_TOLERANCE, "worst=%r" % worst)
    root = samples_root(session)
    recorder.check("the entry landed in the user samples tree", bool(path) and root != ""
                   and os.path.commonpath([path, root]) == os.path.normpath(root),
                   "path=%r root=%r" % (path, root))

    before = open(path, "rb").read() if os.path.isfile(path) else b""
    refused = session.call("browser.save_clip", {"track": 0, "scene": 0})
    recorder.check("a second save without overwrite is refused", refused.get("ok") is False
                   and (refused.get("error") or {}).get("kind") == "refused", "%r" % (refused.get("error"),))
    recorder.check("the refused save left the file alone",
                   os.path.isfile(path) and open(path, "rb").read() == before, "%s" % path)

    reply, items = query(session, "kick.wav")
    recorder.check("browser.query finds the saved entry under the samples root",
                   "error" not in reply and len(items) == 1, "query=%r" % reply)
    recorder.check("the found entry is the file saved, kind audio",
                   bool(items) and items[0].get("path") == path and items[0].get("kind") == "audio",
                   "%r" % (items[:1],))
    tags = session.result("browser.tags")
    tagged = [entry for entry in tags.get("tags", []) if entry.get("tag") == "drums"]
    recorder.check("browser.tags counts the saved file under its tag",
                   bool(tagged) and int(tagged[0].get("files", 0)) >= 1, "%r" % (tagged[:1],))
    tagged_query, tagged_items = query(session, "kick.wav")
    recorder.check("browser.query reports the file's tags",
                   "error" not in tagged_query and bool(tagged_items)
                   and sorted(tagged_items[0].get("tags", [])) == ["drums", "one-shot"],
                   "%r" % (tagged_items[:1],))

    undone = session.result("control.undo")
    recorder.check("control.undo removes the library file", "error" not in undone and not os.path.exists(path),
                   "undo=%r exists=%r" % (undone, os.path.exists(path)))
    redone = session.result("control.redo")
    recorder.check("control.redo writes it back", "error" not in redone and os.path.isfile(path), "redo=%r" % redone)
    transactions = session.result("control.transactions").get("transactions", [])
    last = next((entry for entry in reversed(transactions)
                 if entry.get("command") == "browser.save_clip"), {})
    recorder.check("the save is recorded as a true_inverse browser.save_clip step",
                   last.get("class") == "true_inverse" and last.get("reversible") is True, "%r" % (last,))
    return path


def drive_refusals(session, recorder, workdir, source):
    """Every refusal that needs no environment seam - each promising nothing was written or replaced."""
    clips = os.path.join(samples_root(session), "Clips")
    before = files_in(clips)

    def refused(name, args, kind="refused", message=None):
        reply = session.call("browser.save_clip", args)
        error = reply.get("error") or {}
        held = reply.get("ok") is False and error.get("kind") == kind
        if message is not None:
            held = held and message in (error.get("message") or "")
        recorder.check(name, held, "%r" % (reply,))
        recorder.check(name + " wrote nothing", files_in(clips) == before, "%r" % (files_in(clips),))

    refused("an empty cell is refused", {"track": 1, "scene": 1})
    refused("a MIDI cell is refused by name (stage 2)", {"track": 1, "scene": 0}, message="MIDI")
    refused("an address outside the grid is invalid args", {"track": 9, "scene": 0}, kind="invalid_args")
    refused("both addresses at once are invalid args", {"clip": "clip-0", "track": 0, "scene": 0}, kind="invalid_args")
    refused("one coordinate alone is invalid args", {"track": 0}, kind="invalid_args")
    refused("no address at all is invalid args", {}, kind="invalid_args")

    missing = os.path.join(workdir, "missing.wav")
    empty = os.path.join(workdir, "empty.wav")
    damaged = os.path.join(workdir, "damaged.wav")
    unsupported = os.path.join(workdir, "notes.txt")
    open(empty, "wb").close()
    with open(damaged, "wb") as handle:
        handle.write(b"this is not a RIFF file")
    with open(unsupported, "w") as handle:
        handle.write("notes")
    for name, invalid in (("missing", missing), ("empty", empty), ("damaged", damaged), ("unsupported", unsupported)):
        session.result("session.set_slot", {"track": 0, "scene": 0, "type": "audio", "source": invalid})
        refused("a %s source is refused" % name, {"track": 0, "scene": 0, "name": "refused-%s" % name})
    session.result("session.set_slot", {"track": 0, "scene": 0, "type": "audio", "source": source})


def drive_seams(binary, recorder, source):
    """The exhaustion and failure cases, each in an instance whose environment sets one deterministic seam.

    The decoded-size seam is the point of the review: this 16-bit mono source is 2 bytes a frame on disk and
    8 bytes a frame once decoded (stereo float), so a budget between the two refuses it while the ENCODED file
    is under the budget - the case the old input-size check let through to a full decode."""
    file_bytes = os.path.getsize(source)
    frames = decoded_frames(source)
    decoded_bytes = frames * 8
    budget = (file_bytes + decoded_bytes) // 2
    recorder.check("the over-limit fixture is under the budget encoded and over it decoded",
                   file_bytes < budget < decoded_bytes,
                   "file=%d decoded=%d budget=%d" % (file_bytes, decoded_bytes, budget))
    instance, session, _ = start(binary, {"ZENE_TEST_LIBRARY_SAVE_MAX_DECODED_BYTES": str(budget)})
    try:
        populate(session, source, recorder)
        clips = os.path.join(samples_root(session), "Clips")
        before = files_in(clips)
        reply = session.call("browser.save_clip", {"track": 0, "scene": 0, "name": "over-decoded"})
        error = reply.get("error") or {}
        recorder.check("an over-decoded source is refused by the decoded-size bound",
                       reply.get("ok") is False and error.get("kind") == "refused"
                       and "decodes to" in (error.get("message") or ""), "%r" % (reply,))
        recorder.check("the refusal names the decoded width, not the file's size",
                       str(frames) in (error.get("message") or ""), "%r" % (error.get("message"),))
        recorder.check("the over-decoded save wrote nothing", files_in(clips) == before, "%r" % (files_in(clips),))
        session.call("control.quit")
    finally:
        instance.close()

    for seam, value, message in (("ZENE_TEST_LIBRARY_SAVE_ALLOC_FAULT", "1", "ran out of memory"),
                                 ("ZENE_TEST_LIBRARY_SAVE_WRITE_FAULT", "short-write", "failed"),
                                 ("ZENE_TEST_LIBRARY_SAVE_WRITE_FAULT", "before-commit", "not completed")):
        instance, session, _ = start(binary, {seam: value})
        try:
            populate(session, source, recorder)
            clips = os.path.join(samples_root(session), "Clips")
            os.makedirs(clips, exist_ok=True)
            keep = os.path.join(clips, "failure.wav")
            with open(keep, "wb") as handle:
                handle.write(b"keep me")
            before = files_in(clips)
            reply = session.call("browser.save_clip", {"track": 0, "scene": 0, "name": "failure", "overwrite": True})
            error = reply.get("error") or {}
            recorder.check("%s is a typed refusal" % seam, reply.get("ok") is False
                           and error.get("kind") == "refused" and message in (error.get("message") or ""),
                           "%r" % (reply,))
            recorder.check("%s leaves the destination untouched" % seam,
                           open(keep, "rb").read() == b"keep me", keep)
            recorder.check("%s leaves no partial file or temporary" % seam, files_in(clips) == before,
                           "%r" % (files_in(clips),))
            session.call("control.quit")
        finally:
            instance.close()


def drive_reopen(session, recorder, workdir, source, path):
    """Save, reopen, browse, and reuse the entry in another audio cell with its content intact."""
    project = os.path.join(workdir, "library-cell.mmp")
    recorder.check("the project saves", "error" not in session.result("project.save", {"path": project}), project)
    recorder.check("a new project starts empty", "error" not in session.result("project.new"), "")
    opened = session.result("project.open", {"path": project})
    recorder.check("the project reopens", "error" not in opened, "%r" % opened)
    reply, items = query(session, "kick.wav")
    recorder.check("after the reopen the entry is still browsable",
                   "error" not in reply and len(items) == 1 and items[0].get("path") == path, "%r" % reply)
    recorder.check("the reopened cell still points at its source",
                   slot(session, 0, 0).get("source") == source, "%r" % slot(session, 0, 0).get("source"))

    insert = session.result("session.set_slot", {"track": 0, "scene": 1, "type": "audio",
                                                "source": path, "name": "kick (library)"})
    recorder.check("the saved entry goes into another audio cell", "error" not in insert, "%r" % insert)
    reused = slot(session, 0, 1)
    recorder.check("the other cell reads back the library file as its source", reused.get("source") == path,
                   "%r" % reused.get("source"))
    worst = compare(source, reused.get("source", "")) if reused.get("source") else None
    recorder.check("the reused cell's audio is the source content, sample for sample",
                   worst is not None and worst <= FRAME_TOLERANCE, "worst=%r" % worst)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    with tempfile.TemporaryDirectory(prefix="q050-273-1-") as workdir:
        source = os.path.join(workdir, "kick-source.wav")
        write_tone(source)
        instance, session, transcript = start(argv[1])
        try:
            populate(session, source, recorder)
            path = drive_save(session, recorder, source)
            drive_refusals(session, recorder, workdir, source)
            drive_reopen(session, recorder, workdir, source, path)
            session.call("control.quit")
        finally:
            instance.close()
        drive_seams(os.path.abspath(argv[1]), recorder, source)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("a launcher cell in the library over the control socket")
        return 1
    H.ok("a launcher cell in the library over the control socket (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
