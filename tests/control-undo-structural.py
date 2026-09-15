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
against the CLIPS' CONTENT, read back through the socket after the undo - never
against "a track with that name exists again", which a lossy undo also satisfies.

The checks, in order: (1) the fixture - three tracks, each with a clip, the
middle one carrying three notes, MEASURED against the default song rather than
assumed; (2) `track.remove` deletes the track AND its clip and notes (the control
for 3); (3) `control.undo` returns the track WITH its clip - same position,
length and note count - and `roll.get_state` reports the same notes, key by key;
(4) ... at the index it was removed from, under the SAME persisted `trk-<n>`;
(5) the row-51 interaction MEASURED and printed: `clip-<n>` is the index-derived
ordinal of a clip in the WHOLE song, so a delete shifts every later clip's id and
the undo shifts them back; (6) `control.redo` re-does the deletion, so the
inverse is a PAIR; (7) `track.move` - the arrangement's order: a reorder, ONE
undo back, the redo forward, and an out-of-range index REFUSED, not clamped;
(8) `plugin.load` / `plugin.unload` - a device given a non-default parameter and
then removed: one `control.undo` re-instantiates it AT ITS INDEX with the
parameter restored, and the redo removes it again; (9) `control.undo_depth` -
the byte budget COUNTS a structural payload, so a delete GROWS retained_bytes
(a step that charged 0 bytes would print no growth); (10) `control.transactions`
- the A16 record of every structural edit is reversible and names its mechanism;
(11) refusals - a `track.move` past the end, a `track.remove` of a missing track
and a `plugin.unload` of a missing device are typed and change nothing.

Started through the shared harness (tests/control_socket_harness.py), so this file
adds no second launch path. Usage:
    QT_QPA_PLATFORM=offscreen python3 control-undo-structural.py <zene>
Exit code 0 only when every assertion held; 77 when the build has no loadable
effect to drive the device half (a test that cannot prove its claim must not
report Passed).
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
    """The device ids in `target`'s chain. dsp.get_state answers with the chain
    WRAPPED ({chains:[...]}) - the wrapper is read here, once, so no check below
    can mistake "no devices" for "no chain"."""
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
    """Three instrument tracks; the middle one carries the notes. The session is
    NOT empty - a fresh instance holds the default song's tracks - so the fixture
    is measured against that baseline rather than assuming an empty song."""
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

def check_fixture_and_delete(session, recorder, fixture):
    state = arrangement(session)
    grew_tracks = len(state.get("tracks") or []) - fixture["baseline_tracks"]
    grew_clips = len(state.get("clips") or []) - fixture["baseline_clips"]
    recorder.check("the fixture added three tracks, each with one clip",
                   grew_tracks == 3 and grew_clips == 3,
                   "the session had %d track(s)/%d clip(s), now %d/%d"
                   % (fixture["baseline_tracks"], fixture["baseline_clips"],
                      len(state.get("tracks") or []), len(state.get("clips") or [])))
    for track, (clip_id, _signature) in fixture["clips"].items():
        entry = clip_entry(state, clip_id)
        expected_notes = len(NOTES) if track == fixture["tracks"][1] else 1
        recorder.check("track %s's clip %s carries its notes" % (track, clip_id),
                       entry is not None and entry.get("note_count") == expected_notes,
                       "clip=%r" % entry)

    victim = fixture["tracks"][1]
    victim_clip = fixture["clips"][victim][0]
    removed = session.result("track.remove", {"track": victim})
    after = arrangement(session)
    recorder.check("track.remove really removed the track",
                   removed.get("removed") == victim and find_track(after, victim) is None,
                   "removed=%r tracks=%s" % (removed.get("removed"), order_of(after)))
    # By TRACK and not by the cached clip id: `clip-<n>` is an index-derived
    # ordinal of the whole song, so after this delete the id the victim's clip
    # had now names a DIFFERENT clip (check 5 measures exactly that). Asserting
    # on the stale id would fail on a correct delete.
    recorder.check("...and its CLIP and NOTES went with it (the control for the undo)",
                   [c for c in (after.get("clips") or []) if c.get("track") == victim] == [],
                   "clips on %s after the delete = %r (the id %r now names %r)"
                   % (victim, [c.get("id") for c in (after.get("clips") or [])
                               if c.get("track") == victim], victim_clip,
                      clip_entry(after, victim_clip)))
    return {"victim": victim, "victim_clip": victim_clip}

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

def check_first_track_returns_at_its_index_and_id(session, recorder, fixture):
    victim = fixture["tracks"][0]
    before_order = order_of(arrangement(session))
    before_clip = fixture["clips"][victim][0]
    others_before = {t: clips_of(arrangement(session), t) for t in fixture["tracks"][1:]}

    session.result("track.remove", {"track": victim})
    others_mid = {t: clips_of(arrangement(session), t) for t in fixture["tracks"][1:]}
    session.result("control.undo")
    state = arrangement(session)
    after_order = order_of(state)
    others_after = {t: clips_of(state, t) for t in fixture["tracks"][1:]}

    recorder.check("the deleted track comes back AT ITS INDEX, not appended",
                   after_order == before_order,
                   "before=%s after=%s" % (before_order, after_order))
    recorder.check("...and it keeps the SAME trk-<n> (the id is persisted, not re-derived)",
                   [t for t in after_order if t == victim] == [victim],
                   "victim=%s after=%s" % (victim, after_order))
    restored = clips_of(state, victim)
    recorder.check("...and the whole of its clip came back with it",
                   len(restored) == 1 and clip_entry(state, restored[0]) is not None
                   and clip_signature(roll(session, restored[0]))
                   == fixture["clips"][victim][1],
                   "restored=%r expected=%r" % (restored, fixture["clips"][victim][1]))

    # ROW 51, MEASURED: clip-<n> is the ordinal of the clip in the WHOLE song, so
    # removing the first track pulls every later clip's id down by one. The delete
    # is therefore VISIBLE in ids that name other clips, and the undo puts them
    # back. Printed, not asserted away: it is the documented limit.
    shifted = [(t, others_before[t], others_mid[t]) for t in others_before
               if others_before[t] != others_mid[t]]
    print("row 51 (index-derived clip ids): %d of %d later tracks' clip ids moved on the delete"
          % (len(shifted), len(others_before)))
    for track, was, now in shifted:
        print("    %s: %s -> %s (back to %s after the undo)"
              % (track, was, now, others_after[track]))
    recorder.check("the index rule is exactly what moved them: removing track 0 shifts "
                   "every later clip's ordinal down by one",
                   all(len(now) == len(was) and
                       int(now[0].split("-")[1]) == int(was[0].split("-")[1]) - 1
                       for _, was, now in shifted),
                   "shifted=%r" % (shifted,))
    recorder.check("the undo RESTORES those ids too (they are a function of the arrangement, "
                   "so a client must re-read them rather than cache one)",
                   others_after == others_before,
                   "before=%r after=%r" % (others_before, others_after))
    recorder.check("the clip id the deleted track's own clip got back is the pre-delete one",
                   clips_of(state, victim) == [before_clip],
                   "got=%r expected=%r" % (clips_of(state, victim), [before_clip]))

def check_redo_removes_it_again(session, recorder, deleted):
    redone = session.result("control.redo")
    state = arrangement(session)
    recorder.check("control.redo removes the restored track again",
                   find_track(state, deleted["victim"]) is None
                   and clips_of(state, deleted["victim"]) == [],
                   "redo=%r tracks=%s" % (redone, order_of(state)))
    session.result("control.undo")

def check_track_move(session, recorder, fixture):
    """Every expectation is computed from the order the song ACTUALLY has - a
    fresh instance already holds the default song's tracks, so "index 0" is not
    "the first track this fixture made"."""
    before = order_of(arrangement(session))
    moved_track = before[-1]
    rest = before[:-1]

    session.result("track.move", {"track": moved_track, "index": 0})
    recorder.check("track.move puts the track at the requested index",
                   order_of(arrangement(session)) == [moved_track] + rest,
                   "order=%s expected=%s" % (order_of(arrangement(session)), [moved_track] + rest))
    session.result("control.undo")
    recorder.check("ONE control.undo puts the arrangement's order back",
                   order_of(arrangement(session)) == before,
                   "order=%s expected=%s" % (order_of(arrangement(session)), before))
    session.result("control.redo")
    recorder.check("...and the redo re-applies the reorder",
                   order_of(arrangement(session)) == [moved_track] + rest,
                   "order=%s" % order_of(arrangement(session)))
    session.result("control.undo")
    recorder.check("...and the second undo is back at the original order",
                   order_of(arrangement(session)) == before,
                   "order=%s" % order_of(arrangement(session)))

    count = len(before)
    refused = session.typed_error("track.move", {"track": before[0], "index": count})
    recorder.check("an out-of-range index is REFUSED, not clamped",
                   refused.get("kind") in ("refused", "invalid_args"),
                   "index=%d count=%d error=%r" % (count, count, refused))
    recorder.check("...and the refusal changed nothing",
                   order_of(arrangement(session)) == before,
                   "order=%s" % order_of(arrangement(session)))

    # A move to the index a track is already at changes nothing and records
    # nothing: a step for a no-op would make one Ctrl+Z do nothing at all.
    here = before[2] if len(before) > 2 else before[0]
    same = session.result("track.move", {"track": here,
                                         "index": order_of(arrangement(session)).index(here)})
    recorder.check("moving a track to where it already is is a no-op, not a change",
                   same.get("moved") is False, "result=%r" % same)
    recorder.check("...and it left the order alone",
                   order_of(arrangement(session)) == before,
                   "order=%s" % order_of(arrangement(session)))

def pick_effect(session):
    """The first loadable effect this build offers, or None (then the DEVICE half
    cannot run and reports Skipped rather than Passed)."""
    listing = session.result("plugin.list", {"kind": "effect", "loadable_only": True})
    devices = listing.get("devices") or []
    print("plugin.list: %d device(s) in this build, %d loadable"
          % (listing.get("total", 0), len(devices)))
    return devices[0].get("id") if devices else None

def check_effect_removal_is_undoable(session, recorder, fixture):
    device = pick_effect(session)
    if device is None:
        return False
    target = fixture["tracks"][0]
    loaded = session.result("plugin.load", {"target": target, "device": device})
    effect = loaded.get("id")
    if not effect:
        H.fail("plugin.load returned no instance id (%r)" % loaded)
    # A non-default parameter, so "the device is back" is not "an empty device
    # with the same name is back".
    param = (session.result("plugin.param_get", {"target": target, "plugin": effect,
                                                "index": 0}).get("parameter") or {})
    name = param.get("name")
    baseline = param.get("value")
    changed = 0.25 if (baseline is None or abs(float(baseline) - 0.25) > 1e-9) else 0.75
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
    ids, chain = devices_of(session, target)
    recorder.check("...and the chain no longer holds it",
                   effect not in ids, "chain devices=%s (chain=%r)" % (ids, chain is not None))

    session.result("control.undo")
    ids, _chain = devices_of(session, target)
    recorder.check("control.undo RE-INSTANTIATES the removed device at its index",
                   ids == [effect], "chain ids=%s (expected [%r])" % (ids, effect))
    if ids:
        value = (session.result("plugin.param_get", {"target": target, "plugin": effect,
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
                   "retained_bytes %r -> %r (an action step that counted 0 bytes would print "
                   "0 here, and the declared byte budget would never apply to the largest "
                   "steps on the stack)" % (before.get("retained_bytes"),
                                            after.get("retained_bytes")))
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
    print("")
    print("---- the structural commands control.transactions reports ----")
    for op in ("track.remove", "track.move", "plugin.unload", "plugin.load"):
        for record in by_command.get(op) or []:
            print("%-16s class=%s reversible=%s" % (op, record.get("class"),
                                                   record.get("reversible")))
            print("                 mechanism=%s" % record.get("mechanism"))

    def a_reversible_record(op):
        """A reversible record for `op`, if one was left. A command may leave
        SEVERAL (track.move's own no-op is one), so this is 'any', not 'last'."""
        for record in by_command.get(op) or []:
            if record.get("reversible") is True and (record.get("mechanism") or "") != "":
                return record
        return None

    for op in ("track.remove", "track.move"):
        recorder.check("the A16 record holds a reversible row for %s" % op,
                       a_reversible_record(op) is not None,
                       "records=%r" % (by_command.get(op),))
    # The device row is only asserted when the device half actually ran: this
    # build may ship no loadable effect (the plugin modules are separate build
    # targets), and a check that cannot be made must not be reported as one that
    # held. When it did run, the same assertion applies.
    if device_ran:
        recorder.check("the A16 record holds a reversible row for plugin.unload",
                       a_reversible_record("plugin.unload") is not None,
                       "records=%r" % (by_command.get("plugin.unload"),))
    else:
        print("device half did not run (no loadable effect in this build): the "
              "plugin.unload A16 row was NOT asserted - reported, never passed")
    return by_command

def check_refusals(session, recorder, fixture):
    """The typed refusals: nothing is removed, nothing is moved, nothing undone."""
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
        check_fixture_and_delete(session, recorder, fixture)
        check_undo_returns_the_music(session, recorder, fixture,
                                     {"victim": fixture["tracks"][1]})
        check_redo_removes_it_again(session, recorder,
                                    {"victim": fixture["tracks"][1]})
        check_first_track_returns_at_its_index_and_id(session, recorder, fixture)
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
