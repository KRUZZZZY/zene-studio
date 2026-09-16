#!/usr/bin/env python3
"""RAW control-surface transcript for STRUCTURAL undo (0.3.0, feature row 75,
task #664): track add / remove / move and effect add / remove are journalled, and
ONE control.undo restores them - INCLUDING a deleted track WITH ITS CLIPS.

The acceptance evidence for undo robustness, produced by driving the REAL `zene`
binary headless and printing every request and reply verbatim.

THE DEFECT THIS FILE EXISTS FOR is not "undo does not work". It is narrower and
worse: `TrackContainer::removeTrack` forgets a POINTER, and the destruction path
beside it is what loses the music - `~Track` deletes every clip (and `~Clip` the
notes) and only THEN calls removeTrack(). So a journal checkpoint taken at the
container restores a track with an EMPTY CLIP LIST: the track comes back and the
music does not. Every assertion about restored music below is therefore made
against the CLIPS' CONTENT, read back after the undo - never against "a track with
that name exists again", which a lossy undo also satisfies.

The checks, in order (each prints its own name and its evidence): the fixture
(MEASURED against the default song, not assumed); `track.remove` taking the track
AND its clip and notes (the control); `control.undo` returning the track WITH its
clip and the same notes key by key; ... at the index it was removed from and under
the SAME persisted `trk-<n>`; the row-51 interaction MEASURED and printed -
`clip-<n>` is the index-derived ordinal of a clip in the WHOLE song, so a delete
shifts every later clip's id and the undo shifts them back; `control.redo`
re-doing the deletion (the inverse is a PAIR); `track.move` - a reorder, ONE undo
back, the redo forward, an out-of-range index REFUSED, a no-op move recording
nothing; `plugin.unload` + `control.undo` - the device back AT ITS INDEX with its
parameter restored, and the redo removing it again; the byte accounting - a delete
GROWS retained_bytes, where a step charging 0 bytes would print no growth; the A16
records; and the refusals, typed and changing nothing.

Usage: QT_QPA_PLATFORM=offscreen python3 control-undo-structural.py <zene>, through
the shared harness - no second launch path. Exit 0 only when every assertion held;
77 when the build has no loadable effect, so the DEVICE half cannot run (a test that
cannot prove its claim must not report Passed).
"""

import os
import sys

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results

CLIP_TICKS = 768  # one bar
NOTES = ((60, 0, 192, 100), (64, 192, 192, 90), (67, 384, 384, 127))

def note_signature(clip):
    """A clip's notes, comparable across a delete/undo: what a lossy undo loses."""
    return sorted((n.get("key"), n.get("position"), n.get("length"), n.get("velocity"))
                  for n in (clip.get("notes") or []))

def clip_signature(clip):
    """The clip's OWN state - position, length and its note content."""
    return (clip.get("position"), clip.get("length"), clip.get("note_count"),
            note_signature(clip))

def roll(session, clip_id):
    return session.result("roll.get_state", {"clip": clip_id})

def arrangement(session):
    return session.result("arrangement.get_state")

def find_track(state, track_id):
    for entry in state.get("tracks") or []:
        if entry.get("id") == track_id:
            return entry
    return None

def clips_of(state, track_id):
    return list((find_track(state, track_id) or {}).get("clips") or [])

def clip_entry(state, clip_id):
    return next((e for e in (state.get("clips") or []) if e.get("id") == clip_id), None)

def order_of(state):
    return [t.get("id") for t in (state.get("tracks") or [])]

def devices_of(session, target):
    """The device ids in `target`'s chain, read out of dsp.get_state's wrapper
    ({chains:[...]}) - once, so no check can mistake "no devices" for "no chain"."""
    state = session.result("dsp.get_state", {"target": target})
    for chain in state.get("chains") or []:
        if chain.get("id") == target:
            return [d.get("id") for d in (chain.get("devices") or [])], chain
    return [], None

def build_clip(session, track, position, notes):
    """One clip of `notes` on `track`; returns (clip_id, its measured signature)."""
    clip_id = session.result("clip.add", {"track": track, "position": position,
                                          "length": CLIP_TICKS}).get("clip")
    if not clip_id:
        return None, None
    for key, offset, length, velocity in notes:
        session.result("note.add", {"clip": clip_id, "key": key, "position": offset,
                                    "length": length, "velocity": velocity})
    return clip_id, clip_signature(roll(session, clip_id))

def build_fixture(session, instance, transcript):
    """Three instrument tracks, the middle one carrying the notes. The session is
    NOT empty - a fresh instance holds the default song - so the fixture is
    measured against that baseline rather than assuming an empty song."""
    baseline = arrangement(session)
    tracks = []
    for name in ("Drums", "Bass", "Pad"):
        track = session.result("track.add", {"type": "instrument", "name": name}).get("track")
        if not track:
            H.fail("track.add returned no track id for %s" % name, instance, transcript)
        tracks.append(track)
    clips = {}
    for index, track in enumerate(tracks):
        notes = NOTES if index == 1 else ((48, 0, CLIP_TICKS // 2, 80),)
        clip_id, signature = build_clip(session, track, index * CLIP_TICKS, notes)
        if clip_id is None:
            H.fail("clip.add returned no clip id on %s" % track, instance, transcript)
        clips[track] = (clip_id, signature)
    return {"tracks": tracks, "clips": clips,
            "baseline_tracks": len(baseline.get("tracks") or []),
            "baseline_clips": len(baseline.get("clips") or [])}

def carries_notes(state, clip_id, expected):
    """Does `clip_id` exist in `state` and hold `expected` notes?"""
    entry = clip_entry(state, clip_id)
    return entry is not None and entry.get("note_count") == expected

def check_fixture(session, recorder, fixture):
    state = arrangement(session)
    grew = (len(state.get("tracks") or []) - fixture["baseline_tracks"],
            len(state.get("clips") or []) - fixture["baseline_clips"])
    recorder.check("the fixture added three tracks, each with one clip", grew == (3, 3),
                   "grew by %r; the session held %d track(s)/%d clip(s)"
                   % (grew, fixture["baseline_tracks"], fixture["baseline_clips"]))
    for track, (clip_id, _signature) in fixture["clips"].items():
        expected = len(NOTES) if track == fixture["tracks"][1] else 1
        recorder.check("track %s's clip %s carries its notes" % (track, clip_id),
                       carries_notes(state, clip_id, expected),
                       "clip=%r" % clip_entry(state, clip_id))

def check_delete_takes_the_music(session, recorder, fixture):
    """The control for the undo below: the clip and the notes really go away.

    Asserted by TRACK and not by the cached clip id: `clip-<n>` is an index-derived
    ordinal of the whole song, so after this delete the id the victim's clip had
    now names a DIFFERENT clip (the next check measures exactly that). Asserting on
    the stale id would fail on a correct delete.
    """
    victim = fixture["tracks"][1]
    removed = session.result("track.remove", {"track": victim})
    after = arrangement(session)
    recorder.check("track.remove really removed the track",
                   removed.get("removed") == victim and find_track(after, victim) is None,
                   "removed=%r tracks=%s" % (removed.get("removed"), order_of(after)))
    left = clips_of(after, victim)
    recorder.check("...and its CLIP and NOTES went with it (the control for the undo)",
                   left == [],
                   "clips still on %s = %r (the id %r now names %r)"
                   % (victim, left, fixture["clips"][victim][0],
                      clip_entry(after, fixture["clips"][victim][0])))

def check_undo_returns_the_music(session, recorder, fixture, deleted):
    depth_before = session.result("control.undo_depth")
    undone = session.result("control.undo")
    state = arrangement(session)
    recorder.check("control.undo reports it unwound something",
                   undone.get("undone") is not False and undone.get("error") is None,
                   "undo=%r" % undone)
    recorder.check("the deleted track is back",
                   find_track(state, deleted["victim"]) is not None,
                   "tracks=%s" % order_of(state))
    # THE CHECK THIS FILE EXISTS FOR: the clips, read back by the id the
    # arrangement reports NOW - not by an id cached before the delete.
    restored = clips_of(state, deleted["victim"])
    recorder.check("the restored track has its CLIP back, not an empty track",
                   len(restored) == 1,
                   "clips of %s = %r (an empty list is the lossy undo this row is about)"
                   % (deleted["victim"], restored))
    if restored:
        rolled = roll(session, restored[0])
        expected_clip = fixture["clips"][deleted["victim"]][1]
        expected_notes = sorted((k, p, l, v) for k, p, l, v in NOTES)
        recorder.check("...with the same position, length and note COUNT",
                       clip_signature(rolled) == expected_clip,
                       "got %r expected %r" % (clip_signature(rolled), expected_clip))
        recorder.check("...and every note came back: key, position, length and velocity",
                       note_signature(rolled) == expected_notes,
                       "notes=%r expected=%r" % (note_signature(rolled), expected_notes))
    after_depth = session.result("control.undo_depth").get("depth")
    recorder.check("the undo cost exactly one step",
                   depth_before.get("depth") is not None
                   and after_depth == depth_before.get("depth") - 1,
                   "depth before=%r after=%r" % (depth_before.get("depth"), after_depth))

def clip_is_intact(session, state, track_id, expected):
    """Is `track_id`'s one clip present AND the clip it was?"""
    clips = clips_of(state, track_id)
    return len(clips) == 1 and clip_signature(roll(session, clips[0])) == expected


def check_the_index_comes_back(session, recorder, fixture):
    """The index it was removed from, and the id it kept: a restored track that was
    APPENDED would satisfy "the track is back" and still be wrong."""
    victim = fixture["tracks"][0]
    before_order = order_of(arrangement(session))
    session.result("track.remove", {"track": victim})
    session.result("control.undo")
    state = arrangement(session)
    recorder.check("the deleted track comes back AT ITS INDEX, not appended",
                   order_of(state) == before_order,
                   "before=%s after=%s" % (before_order, order_of(state)))
    recorder.check("...and it keeps the SAME trk-<n> (the id is persisted, not re-derived)",
                   order_of(state).count(victim) == 1,
                   "victim=%s after=%s" % (victim, order_of(state)))
    recorder.check("...and the whole of its clip came back with it",
                   clip_is_intact(session, state, victim, fixture["clips"][victim][1]),
                   "restored=%r expected=%r" % (clips_of(state, victim),
                                                fixture["clips"][victim][1]))


def ordinal_moved_down_by_one(was, now):
    """True when `now` is `was` with every clip ordinal one lower - row 51's rule."""
    return (len(was) == len(now) == 1
            and int(now[0].split("-")[1]) == int(was[0].split("-")[1]) - 1)


def check_clip_ids_are_index_derived(session, recorder, fixture):
    """ROW 51, MEASURED: clip-<n> is the ordinal of the clip in the WHOLE song, so
    removing a track pulls every later clip's id down by one - a delete is VISIBLE
    in ids that name other clips, and the undo puts them back. Printed, not
    asserted away: it is the documented limit, and it is why a client must re-read
    the arrangement after an undo instead of caching a clip id across one."""
    victim = fixture["tracks"][0]
    others = fixture["tracks"][1:]
    before = {t: clips_of(arrangement(session), t) for t in others}
    session.result("track.remove", {"track": victim})
    mid = {t: clips_of(arrangement(session), t) for t in others}
    session.result("control.undo")
    state = arrangement(session)
    after = {t: clips_of(state, t) for t in others}
    shifted = {t: (before[t], mid[t]) for t in others if before[t] != mid[t]}
    print("row 51 (index-derived clip ids): %d of %d later tracks' clip ids moved on the delete"
          % (len(shifted), len(others)))
    for track, (was, now) in shifted.items():
        print("    %s: %s -> %s (back to %s after the undo)" % (track, was, now, after[track]))
    recorder.check("the index rule is exactly what moved them: removing track 0 shifts every "
                   "later clip's ordinal down by one",
                   all(ordinal_moved_down_by_one(was, now) for was, now in shifted.values()),
                   "shifted=%r" % (shifted,))
    recorder.check("the undo RESTORES those ids too (they are a function of the arrangement, "
                   "so a client must re-read them rather than cache one)",
                   after == before, "before=%r after=%r" % (before, after))
    recorder.check("the clip id the deleted track's own clip got back is the pre-delete one",
                   clips_of(state, victim) == [fixture["clips"][victim][0]],
                   "got=%r expected=%r" % (clips_of(state, victim),
                                           [fixture["clips"][victim][0]]))


def check_redo_removes_it_again(session, recorder, deleted):
    redone = session.result("control.redo")
    state = arrangement(session)
    recorder.check("control.redo removes the restored track again",
                   find_track(state, deleted["victim"]) is None
                   and clips_of(state, deleted["victim"]) == [],
                   "redo=%r tracks=%s" % (redone, order_of(state)))
    session.result("control.undo")

def check_track_move(session, recorder, fixture):
    """Every expectation is computed from the order the song ACTUALLY has - a fresh
    instance already holds the default song's tracks, so "index 0" is not "the
    first track this fixture made"."""
    def now():
        return order_of(arrangement(session))

    before = now()
    moved_track, rest = before[-1], before[:-1]
    want = [moved_track] + rest
    session.result("track.move", {"track": moved_track, "index": 0})
    recorder.check("track.move puts the track at the requested index", now() == want,
                   "order=%s expected=%s" % (now(), want))
    session.result("control.undo")
    recorder.check("ONE control.undo puts the arrangement's order back", now() == before,
                   "order=%s expected=%s" % (now(), before))
    session.result("control.redo")
    recorder.check("...and the redo re-applies the reorder", now() == want, "order=%s" % now())
    session.result("control.undo")
    recorder.check("...and the second undo is back at the original order", now() == before,
                   "order=%s" % now())

    count = len(before)
    refused = session.typed_error("track.move", {"track": before[0], "index": count})
    recorder.check("an out-of-range index is REFUSED, not clamped",
                   refused.get("kind") in ("refused", "invalid_args"),
                   "index=%d of %d error=%r" % (count, count, refused))
    recorder.check("...and the refusal changed nothing", now() == before, "order=%s" % now())

    # A move to the index a track is already at changes nothing and records
    # nothing: a step for a no-op would make one Ctrl+Z do nothing at all.
    here = before[2] if len(before) > 2 else before[0]
    same = session.result("track.move", {"track": here, "index": now().index(here)})
    recorder.check("moving a track to where it already is is a no-op, not a change",
                   same.get("moved") is False, "result=%r" % same)
    recorder.check("...and it left the order alone", now() == before, "order=%s" % now())

def pick_effect(session):
    """The first loadable effect this build offers, or None (then the DEVICE half
    cannot run and reports Skipped rather than Passed)."""
    listing = session.result("plugin.list", {"kind": "effect", "loadable_only": True})
    devices = listing.get("devices") or []
    print("plugin.list: %d device(s) in this build, %d loadable"
          % (listing.get("total", 0), len(devices)))
    return devices[0].get("id") if devices else None

def a_different_value(baseline):
    """A value clearly different from `baseline`, so "it came back" is measurable
    on a device whose defaults are the only thing a fresh instance would carry."""
    return 0.75 if baseline is not None and abs(float(baseline) - 0.75) < 1e-9 else 0.25


def check_effect_removal_is_undoable(session, recorder, fixture):
    """The effect half. False - reported, never passed - when this build offers no
    loadable effect to drive it with (the modules are separate build targets)."""
    device = pick_effect(session)
    if device is None:
        return False
    target = fixture["tracks"][0]
    effect = session.result("plugin.load", {"target": target, "device": device}).get("id")
    if not effect:
        H.fail("plugin.load returned no instance id")
    # A non-default parameter: "the device is back" is not "an empty device with
    # the same name is back".
    param = (session.result("plugin.param_get", {"target": target, "plugin": effect,
                                                "index": 0}).get("parameter") or {})
    name = param.get("name")
    changed = a_different_value(param.get("value"))
    session.result("plugin.param_set", {"target": target, "plugin": effect,
                                        "name": name, "value": changed})
    read_back = (session.result("plugin.param_get", {"target": target, "plugin": effect,
                                                    "name": name}).get("parameter") or {})
    recorder.check("the effect's parameter really was changed before the removal (%s)" % name,
                   read_back.get("value") is not None
                   and abs(float(read_back.get("value")) - changed) < 1e-6,
                   "value=%r expected=%r" % (read_back.get("value"), changed))

    removed = session.result("plugin.unload", {"target": target, "plugin": effect})
    recorder.check("plugin.unload removed the device, and records itself as reversible",
                   removed.get("removed") == effect and removed.get("reversible") is True,
                   "result=%r" % removed)
    ids, _chain = devices_of(session, target)
    recorder.check("...and the chain no longer holds it", effect not in ids,
                   "chain devices=%s" % ids)

    session.result("control.undo")
    ids, _chain = devices_of(session, target)
    # The restored device is the SAME device at the SAME index with its settings
    # back - that is the A16 row's claim ("RE-INSTANTIATES the device through the
    # chain's own instantiate path and puts the settings back, AT the index it
    # was removed from"). Its fx-<n> id is a NEW one, and that is the contract,
    # not a shortfall: the captured state document is a payload, Effect::
    # loadSettings deliberately ignores the id of an element that is not a
    # document element (src/core/Effect.cpp, ProjectIds::isDocumentElement -
    # SPEC-stable-ids.md R5 "a copy payload carries no identity"), and the
    # retired id can never be reborn (R3). The check this replaced asserted
    # ids == [effect] - an id PREDICTION, measured as
    # "chain ids=['fx-40'] (expected ['fx-39'])".
    restored = ids[0] if len(ids) == 1 else None
    recorder.check("control.undo RE-INSTANTIATES the removed device at its index",
                   restored is not None and restored != effect, "chain ids=%s" % ids)
    if restored:
        value = (session.result("plugin.param_get", {"target": target, "plugin": restored,
                                                    "name": name}).get("parameter") or {})
        recorder.check("...with its parameter restored, not a default device of the same name",
                       value.get("value") is not None
                       and abs(float(value.get("value")) - changed) < 1e-6,
                       "param=%s got=%r expected=%r" % (name, value.get("value"), changed))
    session.result("control.redo")
    ids, _chain = devices_of(session, target)
    recorder.check("the redo removes it again", effect not in ids, "chain ids=%s" % ids)
    session.result("control.undo")
    return True

def check_the_payload_is_counted(session, recorder, fixture):
    victim = fixture["tracks"][2]
    before = session.result("control.undo_depth")
    session.result("track.remove", {"track": victim})
    after = session.result("control.undo_depth")
    grew = (after.get("retained_bytes") or 0) - (before.get("retained_bytes") or 0)
    recorder.check("the deleted track's captured document is COUNTED in retained_bytes",
                   grew > 0,
                   "retained_bytes %r -> %r (a step that charged 0 bytes would print 0 here, "
                   "and the byte budget would never apply to the largest steps on the stack)"
                   % (before.get("retained_bytes"), after.get("retained_bytes")))
    recorder.check("...and the depth grew by exactly one step for one delete",
                   (after.get("depth") or 0) == (before.get("depth") or 0) + 1,
                   "depth %r -> %r" % (before.get("depth"), after.get("depth")))
    session.result("control.undo")
    recorder.check("the undo of that step gives the bytes back",
                   (session.result("control.undo_depth").get("retained_bytes") or 0)
                   == (before.get("retained_bytes") or 0),
                   "retained_bytes=%r expected=%r"
                   % (session.result("control.undo_depth").get("retained_bytes"),
                      before.get("retained_bytes")))

def check_transaction_records(session, recorder, fixture, device_ran):
    records = session.result("control.transactions")
    by_command = {}
    for record in records.get("transactions") or []:
        by_command.setdefault(record.get("command"), []).append(record)
    print("\n---- the structural commands control.transactions reports ----")
    for op in ("track.remove", "track.move", "plugin.unload", "plugin.load"):
        for record in by_command.get(op) or []:
            print("%-16s class=%s reversible=%s\n                 mechanism=%s"
                  % (op, record.get("class"), record.get("reversible"),
                     record.get("mechanism")))

    def a_reversible_record(op):
        """A reversible record for `op`, if any was left - a command may leave SEVERAL
        (track.move's own no-op is one), so this is 'any', not 'last'."""
        for record in by_command.get(op) or []:
            if record.get("reversible") is True and (record.get("mechanism") or "") != "":
                return record
        return None

    for op in ("track.remove", "track.move"):
        recorder.check("the A16 record holds a reversible row for %s" % op,
                       a_reversible_record(op) is not None,
                       "records=%r" % (by_command.get(op),))
    # The device row is only asserted when the device half ran: a check that
    # cannot be made must not be reported as one that held.
    if device_ran:
        recorder.check("the A16 record holds a reversible row for plugin.unload",
                       a_reversible_record("plugin.unload") is not None,
                       "records=%r" % (by_command.get("plugin.unload"),))
    else:
        print("device half did not run (no loadable effect in this build): the "
              "plugin.unload A16 row was NOT asserted - reported, never passed")
    return by_command

def check_refusals(session, recorder, fixture):
    """The typed refusals: nothing removed, nothing moved, nothing undone."""
    missing = session.typed_error("track.remove", {"track": "trk-99999"})
    recorder.check("removing a track that is not there is typed, not silent",
                   missing.get("kind") == "not_found", "error=%r" % missing)
    absent = session.typed_error("track.move", {"track": "trk-99999", "index": 0})
    recorder.check("moving a track that is not there is typed too",
                   absent.get("kind") == "not_found", "error=%r" % absent)
    unloaded = session.typed_error("plugin.unload", {"target": fixture["tracks"][0],
                                                     "plugin": "fx-999"})
    recorder.check("unloading a device that is not there is typed",
                   unloaded.get("kind") in ("not_found", "invalid_args"),
                   "error=%r" % unloaded)

def check_quit(session, instance, recorder):
    session.result("control.quit")
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    if code != 0:
        instance.dump_log()
    recorder.check("control.quit stops the instance", exited and code == 0,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))

def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    wanted = None
    with H.start_instance(argv[1], workingdir=None,
                          extra_env=plugin_env(argv[1])) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        print("instance: %s" % argv[1])
        print("socket:   %s" % instance.socket_path)
        session.result("control.version")
        fixture = build_fixture(session, instance, transcript)
        check_fixture(session, recorder, fixture)
        check_delete_takes_the_music(session, recorder, fixture)
        check_undo_returns_the_music(session, recorder, fixture,
                                     {"victim": fixture["tracks"][1]})
        check_redo_removes_it_again(session, recorder, {"victim": fixture["tracks"][1]})
        check_the_index_comes_back(session, recorder, fixture)
        check_clip_ids_are_index_derived(session, recorder, fixture)
        check_track_move(session, recorder, fixture)
        wanted = check_effect_removal_is_undoable(session, recorder, fixture)
        check_the_payload_is_counted(session, recorder, fixture)
        check_transaction_records(session, recorder, fixture, bool(wanted))
        check_refusals(session, recorder, fixture)
        check_quit(session, instance, recorder)
    report_results(recorder)
    transcript.dump()
    if recorder.problems:
        print("")
        recorder.problems.report("structural undo control-surface transcript")
        return 1
    if not wanted:
        H.ok("no loadable effect in this build: the DEVICE half did not run - "
             "reported, never silently passed")
    H.ok("structural undo control-surface transcript (every check held)")
    return 0

def plugin_env(binary):
    """PluginFactory searches <dir of the binary>/plugins, and the built-in effect
    modules are separate build targets that land there - name it explicitly so the
    DEVICE half of this proof can run."""
    plugin_dir = os.path.join(os.path.dirname(os.path.abspath(binary)), "plugins")
    return {"LMMS_PLUGIN_DIR": plugin_dir} if os.path.isdir(plugin_dir) else None



if __name__ == "__main__":
    sys.exit(main(sys.argv))
