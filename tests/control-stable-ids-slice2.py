#!/usr/bin/env python3
"""Live acceptance evidence for SPEC-stable-ids.md slice 2: the ids live in the DOCUMENT.

Slice 1 shipped a persistent trk-<n>; every other family was index-derived, so an
id named an OBJECT only for as long as nothing before it moved. Slice 2 writes
clip-<n>, note-<n>, ch-<n> and fx-<n> into the project document and reads them back
on load, and reclassifies dev-<n> as what it always was: an index into the build's
plugin.list catalogue, never a project identity.

Driven through the merged harness against the REAL binary over the control socket,
printing the raw request/response transcript for each claim, in order:

  1. the fixture: two instrument tracks, one clip each, two notes in the first
     clip, one extra mixer channel, one effect loaded from the catalogue;
  2. the five views the ids live in: arrangement.get_state (trk-/clip-),
     roll.get_state (note-), mixer.get_state (ch-), dsp.get_state (fx-),
     plugin.list (dev-);
  3. project.save, then the in-memory model is MOVED (clip.move), then
     project.open - so what comes back can only have come from the FILE;
  4. the ids after the round trip are IDENTICAL, family by family, and the file
     itself carries the `id` attribute on each element the contract names;
  5. THE REGRESSION: with a clip on each of two tracks, clip.delete on the first
     track's clip leaves every OTHER track's clip ids unchanged (index-derived
     ids shifted down by one), and control.undo restores the SAME clip id;
  5b. THE OTHER HALF (row 75): track.remove on the first track and control.undo
     restore the track WITH THE SAME clip- and note- ids it had before the
     delete - the track is re-loaded from its checkpoint, so this is the ids
     coming out of the document, and the case an index-derived clip-<n> made
     lossy (the restored clip answered clip-0 where it had been clip-1);
  6. control.id_contract: registered, both schemas declared, exactly the six
     families trk-/clip-/note-/ch-/fx-/dev- with their persistence;
  7. the same file in a FRESH instance yields the same ids (bounded: one open).

Usage: QT_QPA_PLATFORM=offscreen python3 control-stable-ids-slice2.py <zene-binary>
Exit code 0 only when every check held. 77 is the documented skip: the build ships
no loadable effect, so the fx-<n> family cannot be exercised (ctest
SKIP_RETURN_CODE 77).
"""

import json
import os
import re
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from control_socket_harness import (  # noqa: E402
    PING_TIMEOUT, READY_TIMEOUT, Transcript, connect, fail, ok, ok_result,
    start_instance, wait_ready,
)

USAGE = __doc__

# The documented skip: this build lists no loadable effect, so there is no
# fx-<n> instance to persist and the acceptance criterion cannot be exercised.
SKIP_RETURN_CODE = 77

# The six id families the frozen contract names, each with the persistence its
# control.id_contract entry must declare. dev- is not a document identity at all.
PERSISTENCE = (
    ("trk-", "persistent"), ("clip-", "persistent"), ("note-", "persistent"),
    ("ch-", "persistent"), ("fx-", "persistent"), ("dev-", "catalogue"),
)

# The element each persistent family's `id` attribute is written on, as the
# contract names them. Read from the FILE, not from a reply.
DOCUMENT_ELEMENTS = (
    ("track", r"<track\b[^>]*\bid=\""),
    ("clip", r"<(midiclip|sampleclip|patternclip|automationclip)\b[^>]*\bid=\""),
    ("note", r"<note\b[^>]*\bid=\""),
    ("mixer channel", r"<mixerchannel\b[^>]*\bid=\""),
    ("effect", r"<effect\b[^>]*\bid=\""),
)

CLIP_TICKS = 768
NOTE_TICKS = 96
FIRST_CLIP_TICK = 1000
SECOND_CLIP_TICK = 2000
MOVED_TICK = 5000


class Session:
    """One running instance, its harness client, the transcript and the request ids.

    Every call goes through control_socket_harness.Client: this is not a second
    client and not a second launch path, only the running request id and the
    readiness handshake the harness already owns.
    """

    def __init__(self, binary, transcript, label):
        self.instance = start_instance(binary)
        self.client = connect(self.instance)
        wait_ready(self.instance, self.client, transcript, seconds=READY_TIMEOUT,
                   ping_timeout=PING_TIMEOUT)
        self.transcript = transcript
        self.request_id = 0
        print("\n---- %s ----" % label)

    def raw(self, command, args=None, timeout=PING_TIMEOUT):
        """The reply exactly as the socket returned it (no assertion)."""
        self.request_id += 1
        return self.client.call(self.request_id, command, args, timeout=timeout,
                                transcript=self.transcript)

    def result(self, command, args=None, timeout=PING_TIMEOUT):
        """The reply's result; an ok=false reply is FATAL here, never a check."""
        reply = self.raw(command, args, timeout)
        try:
            return ok_result(reply, self.request_id)
        except AssertionError as error:
            fail("%s did not answer ok: %s" % (command, error),
                 self.instance, self.transcript)

    def close(self):
        try:
            self.raw("control.quit", {"save": False})
        except Exception as error:  # noqa: BLE001 - the reply may not arrive before exit
            print("control.quit: %s" % error)
        self.instance.close()


def answered_id(value, prefix):
    """True when a reply carried an id of the expected family."""
    return isinstance(value, str) and value.startswith(prefix)


def ids_in(session, command, key, args=None):
    """The ids one view reports, from the raw JSON of the view's own reply."""
    return [entry.get("id") for entry in (session.result(command, args).get(key) or [])]


def clip_entries(session):
    """Every clip as arrangement.get_state reports it (id, track, position)."""
    return session.result("arrangement.get_state").get("clips", [])


def effect_ids(session):
    """Every fx-<n> dsp.get_state reports, chain by chain."""
    ids = []
    for chain in session.result("dsp.get_state").get("chains", []):
        ids.extend(device.get("id") for device in chain.get("devices", []))
    return ids


def clips_by_track(clips):
    """{trk-<n>: sorted clip ids}: a clip entry carries the track it lives on."""
    grouped = {}
    for entry in clips:
        grouped.setdefault(entry.get("track"), []).append(entry.get("id"))
    return {track: sorted(ids) for track, ids in grouped.items()}


def id_view(session):
    """The ids the five views report, one sorted list per family, plus the clips.

    Every family is read from the raw JSON of the view that owns it - never from
    an id captured before the operation the check is about.
    """
    clips = clip_entries(session)
    view = {
        "trk-": sorted(ids_in(session, "track.list", "tracks")),
        "clip-": sorted(entry.get("id") for entry in clips),
        "note-": sorted(note for entry in clips
                        for note in ids_in(session, "roll.get_state", "notes",
                                           {"clip": entry.get("id")})),
        "ch-": sorted(ids_in(session, "mixer.get_state", "channels")),
        "fx-": sorted(effect_ids(session)),
        "dev-": sorted(ids_in(session, "plugin.list", "devices")),
    }
    return view, clips


def compare(before, after, what, failures):
    """Every family's ids, element for element."""
    for prefix, _ in PERSISTENCE:
        if before.get(prefix) != after.get(prefix):
            failures.append("the %s ids changed %s: %r -> %r"
                            % (prefix, what, before.get(prefix), after.get(prefix)))


def loadable_effect(session):
    """The catalogue entry plugin.load is asked for, or None for the skip.

    A built-in effect first: it needs no hosted-format world and no UI, which is
    what a headless instance has. Any other loadable effect is the fallback.
    """
    listed = session.result("plugin.list")
    devices = [entry for entry in listed.get("devices", [])
               if entry.get("kind") == "effect" and entry.get("loadable") is True]
    builtin = [entry for entry in devices if entry.get("format") == "builtin"]
    print("plugin.list: total=%s loadable=%s loadable effect(s)=%s built-in=%r"
          % (listed.get("total"), listed.get("loadable_count"), len(devices),
             [entry.get("id") for entry in builtin]))
    if not devices:
        return None
    return builtin[0] if builtin else devices[0]


def build_fixture(session, effect):
    """Two tracks, one clip each, two notes in the first clip, one extra channel,
    one effect. Returns the ids every later check addresses.

    A fixture id that is missing or of the wrong family is fatal: the contract
    cannot be tested at all without a fixture, so this is not a check.
    """
    first = session.result("track.add", {"type": "instrument", "name": "Ids A"}).get("track")
    second = session.result("track.add", {"type": "instrument", "name": "Ids B"}).get("track")
    if not answered_id(first, "trk-") or not answered_id(second, "trk-"):
        fail("track.add did not answer two trk-<n> ids: %r, %r" % (first, second),
             session.instance, session.transcript)

    clip_a = session.result("clip.add", {"track": first, "position": FIRST_CLIP_TICK,
                                         "length": CLIP_TICKS}).get("clip")
    clip_b = session.result("clip.add", {"track": second, "position": SECOND_CLIP_TICK,
                                         "length": CLIP_TICKS}).get("clip")
    if not answered_id(clip_a, "clip-") or not answered_id(clip_b, "clip-"):
        fail("clip.add did not answer two clip-<n> ids: %r, %r" % (clip_a, clip_b),
             session.instance, session.transcript)

    for key in (60, 64):
        note = session.result("note.add", {"clip": clip_a, "key": key, "position": 0,
                                           "length": NOTE_TICKS, "velocity": 100}).get("note")
        if not answered_id(note, "note-"):
            fail("note.add did not answer a note-<n> id: %r" % note,
                 session.instance, session.transcript)

    channel = session.result("mixer.add_channel").get("channel")
    if not answered_id(channel, "ch-"):
        fail("mixer.add_channel did not answer a ch-<n> id: %r" % channel,
             session.instance, session.transcript)

    loaded = session.result("plugin.load", {"target": first, "device": effect.get("id")})
    if loaded.get("kind") != "effect" or not answered_id(loaded.get("id"), "fx-"):
        fail("plugin.load did not answer an fx-<n> effect id: %r" % (loaded,),
             session.instance, session.transcript)
    print("fixture: %s + %s, clips %s/%s, channel %s, %s -> %s"
          % (first, second, clip_a, clip_b, channel, effect.get("id"), loaded.get("id")))
    return {"first": first, "second": second, "clip_a": clip_a, "clip_b": clip_b,
            "channel": channel, "device": effect.get("id"), "effect": loaded.get("id")}


def check_document(path, device_id, failures):
    """The other half of the claim: the ids are IN the file.

    Read from the saved document itself - the element each family owns must carry
    an `id` attribute, and a catalogue selector must not be in the project at all.
    """
    with open(path, "r", errors="replace") as handle:
        text = handle.read()
    if not text.strip():
        failures.append("project.save wrote an empty file")
        return
    for label, pattern in DOCUMENT_ELEMENTS:
        found = re.search(pattern, text)
        print("file: id attribute on a %-13s element: %s"
              % (label, ("yes (%s)" % found.group(0)[:70]) if found else "NO"))
        if not found:
            failures.append("the saved file carries no id attribute on a %s element" % label)
    if device_id and device_id in text:
        failures.append("%s is a catalogue selector and was written into the project document"
                        % device_id)
    print("file: the catalogue selector %s is in the document: %s (dev- anywhere: %s)"
          % (device_id, device_id in text, "dev-" in text))


def check_round_trip(session, saved, fixture, failures):
    """THE CORE: the ids before project.save are the ids after project.open.

    The in-memory model is MOVED between the two reads, so the post-open state
    can only have come from the file - a round trip that changed nothing would
    only prove that nothing had moved. Returns the pre-save view.
    """
    before, _ = id_view(session)
    print("ids before the round trip: %s" % json.dumps(before, sort_keys=True))

    saved_reply = session.result("project.save", {"path": saved})
    print("project.save answered: %s" % json.dumps(saved_reply, sort_keys=True))
    if not os.path.exists(saved):
        failures.append("project.save did not write %s" % saved)
        return before

    moved = session.result("clip.move", {"clip": fixture["clip_b"], "position": MOVED_TICK})
    print("clip.move %s -> %d answered: %s"
          % (fixture["clip_b"], MOVED_TICK, json.dumps(moved, sort_keys=True)))
    if moved.get("position") != MOVED_TICK:
        failures.append("clip.move did not move %s to %d: %r"
                        % (fixture["clip_b"], MOVED_TICK, moved))

    check_document(saved, fixture["device"], failures)

    opened = session.result("project.open", {"path": saved})
    print("project.open answered: %s" % json.dumps(opened, sort_keys=True))
    if opened.get("file") != saved:
        failures.append("project.open answered %r, not %r" % (opened.get("file"), saved))
    if opened.get("error_count") != 0:
        failures.append("project.open reported %r load error(s)" % opened.get("error_count"))

    after, clips = id_view(session)
    print("ids after  the round trip: %s" % json.dumps(after, sort_keys=True))
    compare(before, after, "across project.save + project.open", failures)

    positions = {entry.get("id"): entry.get("position") for entry in clips}
    if positions.get(fixture["clip_b"]) != SECOND_CLIP_TICK:
        failures.append("the clip moved in memory to %d came back at %r, so the post-open "
                        "ids did not come from the file"
                        % (MOVED_TICK, positions.get(fixture["clip_b"])))
    return before


def check_delete_does_not_renumber(session, first, failures):
    """THE REGRESSION THIS SLICE FIXES: an id survives a sibling's delete.

    With index-derived ids every clip after the deleted one shifted down by one,
    so addressing a track after the delete answered a DIFFERENT clip id - and an
    undo could bring the clip back under a different id still.
    """
    before = clips_by_track(clip_entries(session))
    doomed = (before.get(first) or [None])[0]
    if doomed is None:
        failures.append("no clip is on %s to delete: %r" % (first, before))
        return

    deleted = session.result("clip.delete", {"clip": doomed})
    print("clip.delete %s answered: %s" % (doomed, json.dumps(deleted, sort_keys=True)))
    if deleted.get("deleted") != doomed:
        failures.append("clip.delete reported deleting %r, not %r"
                        % (deleted.get("deleted"), doomed))

    during = clips_by_track(clip_entries(session))
    for track, ids in before.items():
        if track != first and during.get(track) != ids:
            failures.append("deleting a clip on %s renumbered %s: %r -> %r (index-derived ids)"
                            % (first, track, ids, during.get(track)))

    undone = session.result("control.undo")
    print("control.undo answered: %s" % json.dumps(undone, sort_keys=True))
    if undone.get("undone") is not True:
        failures.append("control.undo did not undo the clip.delete: %r" % (undone,))

    restored = clips_by_track(clip_entries(session))
    if restored.get(first) != before.get(first):
        failures.append("control.undo restored %s with different clip ids: %r -> %r"
                        % (first, before.get(first), restored.get(first)))
    print("clips by track: before %s, during the delete %s, after the undo %s"
          % (json.dumps(before, sort_keys=True), json.dumps(during, sort_keys=True),
             json.dumps(restored, sort_keys=True)))


def check_track_undo_keeps_ids(session, fixture, failures):
    """THE SECOND HALF OF THE REGRESSION, the one row 75 measured (SPEC R5).

    A track is deleted WHOLE - clips and notes with it - and control.undo
    restores it by RE-LOADING its element: Track::loadTrack deletes every clip
    and re-creates it, so nothing of the clip survives in memory and every id
    the restored track reports can only have come from the checkpoint.

    Measured on the merged tip before this slice: the track came back as its
    trk-<n> (slice 1 was already persistent) while its clip came back as clip-0
    where it had been clip-1, because a clip-<n> was the clip's ARRANGEMENT
    ORDINAL and the deletion had renumbered the space - so an id a caller had
    cached before the delete addressed a different clip after the undo.
    """
    first = fixture["first"]
    before_clips = clips_by_track(clip_entries(session))
    before_notes = {clip: sorted(ids_in(session, "roll.get_state", "notes", {"clip": clip}))
                    for clip in before_clips.get(first, [])}
    before_tracks = sorted(ids_in(session, "track.list", "tracks"))

    removed = session.result("track.remove", {"track": first})
    print("track.remove %s answered: %s" % (first, json.dumps(removed, sort_keys=True)))
    during = sorted(ids_in(session, "track.list", "tracks"))
    if first in during:
        failures.append("track.remove did not remove %s: %r" % (first, during))

    undone = session.result("control.undo")
    print("control.undo (the deleted track) answered: %s" % json.dumps(undone, sort_keys=True))
    if undone.get("undone") is not True:
        failures.append("control.undo did not undo the track.remove: %r" % (undone,))

    after_tracks = sorted(ids_in(session, "track.list", "tracks"))
    after_clips = clips_by_track(clip_entries(session))
    if after_tracks != before_tracks:
        failures.append("control.undo restored the track list %r, not %r"
                        % (after_tracks, before_tracks))
    if after_clips.get(first) != before_clips.get(first):
        failures.append("control.undo restored %s with different clip ids: %r -> %r - the "
                        "restored track re-loads its clips, so an index-derived clip-<n> is "
                        "exactly why row 75's undo was lossy"
                        % (first, before_clips.get(first), after_clips.get(first)))
    for clip, ids in before_notes.items():
        now = sorted(ids_in(session, "roll.get_state", "notes", {"clip": clip}))
        if now != ids:
            failures.append("control.undo restored %s with different note ids: %r -> %r"
                            % (clip, ids, now))
    print("ids across the track undo: tracks %s -> %s -> %s, clips on %s %r -> %r, notes %s"
          % (json.dumps(before_tracks), json.dumps(during), json.dumps(after_tracks), first,
             before_clips.get(first), after_clips.get(first), json.dumps(before_notes)))


def check_id_contract(session, failures):
    """control.id_contract: registered, both schemas declared, six families.

    The per-family count is cross-checked against the same views' own raw JSON
    for the five families that live in the project. The dev- family's count is
    printed beside the catalogue size instead: the contract calls it a project
    count and dev- has no project objects, so the two are not asserted equal.
    """
    described = {entry.get("id"): entry
                 for entry in session.result("control.commands_list").get("commands", [])}
    declared = described.get("control.id_contract")
    if declared is None:
        failures.append("control.id_contract is not registered: control.commands_list "
                        "does not carry it")
        return
    if declared.get("group") != "control":
        failures.append("control.id_contract declares group %r, not 'control'"
                        % declared.get("group"))
    for schema in ("args_schema", "result_schema"):
        if not declared.get(schema):
            failures.append("control.id_contract declares no %s" % schema)
    print("control.commands_list: control.id_contract requires=%r mutating=%r args=%s result=%s"
          % (declared.get("requires"), declared.get("mutating"),
             json.dumps(declared.get("args_schema"), sort_keys=True),
             json.dumps(declared.get("result_schema"), sort_keys=True)))

    reply = session.raw("control.id_contract")
    if reply.get("ok") is not True:
        failures.append("control.id_contract answered %r" % (reply,))
        return
    result = reply.get("result") or {}
    print("control.id_contract result: %s" % json.dumps(result, sort_keys=True))

    by_prefix = {family.get("prefix"): family for family in result.get("families") or []}
    if sorted(by_prefix) != sorted(prefix for prefix, _ in PERSISTENCE):
        failures.append("control.id_contract declares %r, not the six prefixes %r"
                        % (sorted(by_prefix), sorted(prefix for prefix, _ in PERSISTENCE)))

    view, _ = id_view(session)
    for prefix, persistence in PERSISTENCE:
        family = by_prefix.get(prefix)
        if family is None:
            continue
        if family.get("persistence") != persistence:
            failures.append("control.id_contract declares %s persistence %r, not %r"
                            % (prefix, family.get("persistence"), persistence))
        if not family.get("form"):
            failures.append("control.id_contract declares no form for %s" % prefix)
        if not family.get("document"):
            failures.append("control.id_contract declares no document line for %s" % prefix)
        if prefix == "dev-":
            print("dev- family count: id_contract=%r catalogue(plugin.list)=%r"
                  % (family.get("count"), len(view.get("dev-") or [])))
        elif family.get("count") != len(view.get(prefix) or []):
            failures.append("control.id_contract counts %r %s object(s), the views report %r"
                            % (family.get("count"), prefix, len(view.get(prefix) or [])))
    for field in ("count", "persistent", "index_derived"):
        if not isinstance(result.get(field), int):
            failures.append("control.id_contract declares no integer %s: %r"
                            % (field, result.get(field)))
    print("control.id_contract summary: count=%r persistent=%r index_derived=%r"
          % (result.get("count"), result.get("persistent"), result.get("index_derived")))


def check_fresh_instance(binary, saved, before, failures):
    """The same file in a FRESH process: the ids come from the document.

    Bounded: one more instance and one open. A file read by the instance that
    wrote it could still be memory; a second process has only the file.
    """
    transcript = Transcript()
    session = Session(binary, transcript, "instance 2: the same file, a fresh instance")
    try:
        opened = session.result("project.open", {"path": saved})
        print("fresh project.open answered: %s" % json.dumps(opened, sort_keys=True))
        if opened.get("file") != saved:
            failures.append("the fresh instance did not open %s: %r" % (saved, opened))
        after, _ = id_view(session)
        print("ids in the fresh instance: %s" % json.dumps(after, sort_keys=True))
        compare(before, after, "in a fresh instance", failures)
    finally:
        session.close()
        transcript.dump()


def parse_args(argv):
    """argv[1] is the zene binary. Returns its absolute path, or None for usage."""
    if len(argv) < 2:
        print(USAGE)
        return None
    binary = os.path.abspath(argv[1])
    if not os.path.exists(binary):
        print(USAGE)
        return None
    return binary


def report(failures):
    """Exit code: 0 only when every check passed."""
    if failures:
        print("\n=== FAIL ===")
        for item in failures:
            print("  - %s" % item)
        return 1
    ok("stable ids slice 2: clip-/note-/ch-/fx- persist through save+open, a delete "
       "renumbers nothing, dev- is a catalogue selector")
    return 0


def main():
    binary = parse_args(sys.argv)
    if binary is None:
        return 2

    tmp = tempfile.mkdtemp(prefix="zids2-", dir="/tmp")
    failures = []
    try:
        saved = os.path.join(tmp, "stable-ids-slice2.mmp")
        transcript = Transcript()
        session = Session(binary, transcript, "instance 1: fixture, round trip, regression, contract")
        try:
            print("instance: %s\nsocket:   %s" % (binary, session.instance.socket_path))
            session.result("control.version")
            effect = loadable_effect(session)
            if effect is None:
                print("\nSKIP: this build ships no loadable effect (plugin.list reports none), "
                      "so the fx-<n> family cannot be exercised")
                print("SKIP_RETURN_CODE %d" % SKIP_RETURN_CODE)
                return SKIP_RETURN_CODE
            fixture = build_fixture(session, effect)
            before = check_round_trip(session, saved, fixture, failures)
            check_delete_does_not_renumber(session, fixture["first"], failures)
            check_track_undo_keeps_ids(session, fixture, failures)
            check_id_contract(session, failures)
        finally:
            session.close()
            transcript.dump()

        # ---- the same file, a fresh process (the ids can only come from the file)
        check_fresh_instance(binary, saved, before, failures)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    return report(failures)


if __name__ == "__main__":
    sys.exit(main())
