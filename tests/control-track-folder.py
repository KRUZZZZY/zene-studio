#!/usr/bin/env python3
"""RAW control-surface transcript for folder tracks (SPEC A16; owner items 3+20+21).

This is the acceptance evidence that a folder track - a track that HOLDS other
tracks, with a group mode and a routing mode - is drivable over the control
socket by an agent, produced by driving the REAL `zene` binary headless and
printing every request and every reply verbatim, so a reader can see exactly what
the engine answered. Nothing here is asserted from the reply alone: the
membership and the mode are asserted from the file the engine WROTE, and the sum
is asserted from the audio the engine RENDERED.

What it drives, in order:

  1. `track.add` type=folder         a folder is a real track type, and every
                                     read reports it as one;
  2. `track.set_folder`              membership, reported by track.list /
                                     track.get_state / folder_get_state, and one
                                     `control.undo` re-parents the child back;
  3. `track.set_routing true`        routing mode takes a mixer channel and every
                                     child's OWN mixer channel points at it;
  4. `track.set_pinned` +
     `track.folder_set_collapsed`    the two flags, and their undo;
  5. `track.visibility_set_save` +
     `_apply` + `_list`              a named set hides every non-member;
  6. `project.save` / `project.open` membership, the mode, the flags and the set
                                     survive a REAL round trip - the in-memory
                                     model is moved elsewhere first, so what comes
                                     back can only have come from the file;
  7. THE SUM, MEASURED               in routing mode the folder's own fader at 0
                                     silences the render (the children's audio is
                                     summed through the folder's channel, not
                                     merely claimed to be); at 1 it sounds; and
                                     in group mode the folder owns NO channel, so
                                     nothing about it can silence anything;
  8. refusals                       a non-folder target, a self-parent, an empty
                                     routing folder, an unknown set - all typed;
  9. `control.transactions`         the A16 record every edit left.

The instance is started with the documented headless recipe through the shared
harness (tests/control_socket_harness.py) and the level measurements come from
tests/freeze_bounce_evidence.py, so this file adds no second launch path and no
second WAV reader.

Usage: QT_QPA_PLATFORM=offscreen python3 control-track-folder.py <zene>
Exit code 0 only when every assertion held; 77 when the build offers no loadable
instrument (the audibility half cannot run, and a test that cannot prove its
claim must not report Passed).
"""

import os
import sys

import control_socket_harness as H
from freeze_bounce_evidence import SILENT_DBFS, Recorder, Session, load_audible_instrument, \
    render_measure, report_results

CLIP_TICKS = 768              # one bar
AUDIBLE_MARGIN_DB = 12.0     # above this a render counts as "there is audio to sum"
SUM_TOLERANCE_DB = 3.0       # the sum re-associates: this is a bound, not identity
FOLDER_IDS = ("track.set_folder", "track.folder_set_collapsed", "track.set_routing",
              "track.set_pinned", "track.visibility_set_save", "track.visibility_set_apply",
              "track.visibility_set_remove")


def make_track(session, kind, name):
    return session.result("track.add", {"type": kind, "name": name}).get("track")


def build_audible_track(session, instance, transcript, name, position):
    """One instrument track with one clip of one note and an instrument that sounds."""
    track = make_track(session, "instrument", name)
    if not track:
        H.fail("track.add returned no track id for %s" % name, instance, transcript)
    if not load_audible_instrument(session, track):
        return None
    clip = session.result("clip.add", {"track": track, "position": position,
                                       "length": CLIP_TICKS})
    if not clip.get("clip"):
        H.fail("clip.add returned no clip id (%r)" % clip, instance, transcript)
    session.result("note.add", {"clip": clip.get("clip"), "key": 60, "position": 0,
                                "length": CLIP_TICKS // 2, "velocity": 120})
    return {"track": track, "clip": clip.get("clip")}


def build_fixture(session, instance, transcript):
    """The fixture: one folder and two audible instrument tracks beside it."""
    folder = make_track(session, "folder", "Strings")
    if not folder:
        H.fail("track.add type=folder returned no track id", instance, transcript)
    first = build_audible_track(session, instance, transcript, "Violins", 0)
    second = build_audible_track(session, instance, transcript, "Cellos", CLIP_TICKS)
    if first is None or second is None:
        return None
    return {"folder": folder, "first": first["track"], "second": second["track"],
            "tracks": [first["track"], second["track"]]}


def track_state(session, track):
    return session.result("track.get_state", {"track": track})


def folder_state(session, folder):
    return session.result("track.folder_get_state", {"track": folder})


def check_folder_is_a_track_type(session, recorder, fixture):
    """`track.add type=folder` makes a real container, and the reads say so."""
    folder = fixture["folder"]
    state = track_state(session, folder)
    recorder.check("track.add type=folder creates a track whose type is folder",
                   state.get("type") == "folder", "state=%r" % state)
    recorder.check("a folder that holds nothing reports child_count 0 and no channel",
                   state.get("child_count") == 0 and state.get("mixer_channel") == -1,
                   "state=%r" % state)
    recorder.check("the group mode is the default",
                   state.get("folder_mode") == "group", "folder_mode=%r" % state.get("folder_mode"))

    # The flat arrangement view carries the relation as a FIELD on every entry -
    # never as a nested array, so no existing client of the flat list breaks.
    arrangement = session.result("arrangement.get_state")
    listed = [t for t in (arrangement.get("tracks") or []) if t.get("id") == folder]
    recorder.check("arrangement.get_state reports the folder flat, with its own state",
                   len(listed) == 1 and listed[0].get("type") == "folder"
                   and listed[0].get("folder") == "", "listed=%r" % listed)


def check_membership(session, recorder, fixture):
    """track.set_folder: the relation, read from three commands and undone once."""
    folder = fixture["folder"]
    first = fixture["first"]
    for child in fixture["tracks"]:
        session.result("track.set_folder", {"track": child, "folder": folder})
    state = folder_state(session, folder)
    recorder.check("track.set_folder puts both tracks in the folder",
                   state.get("child_count") == 2
                   and sorted(c.get("track") for c in state.get("children") or [])
                   == sorted(fixture["tracks"]), "state=%s" % state)

    listed = {t.get("id"): t for t in (session.result("track.list").get("tracks") or [])}
    recorder.check("track.list names the parent of each child by trk-<n>",
                   listed.get(first, {}).get("folder") == folder,
                   "entry=%r" % listed.get(first))
    recorder.check("track.get_state names the parent too",
                   track_state(session, first).get("folder") == folder,
                   "state=%r" % track_state(session, first))

    # SPEC A16: one undo re-parents the child back to the container root.
    session.result("control.undo")
    recorder.check("one control.undo takes the child back out of the folder",
                   track_state(session, first).get("folder") == "",
                   "state=%r" % track_state(session, first))
    session.result("track.set_folder", {"track": first, "folder": folder})


def check_routing_wiring(session, recorder, fixture):
    """Routing mode: one channel of the folder's own, and every child on it."""
    folder = fixture["folder"]
    set_mode = session.result("track.set_routing", {"track": folder, "routing": True})
    channel = set_mode.get("mixer_channel")
    recorder.check("track.set_routing gives the folder a mixer channel of its own",
                   set_mode.get("mode") == "routing" and channel is not None and channel >= 0,
                   "reply=%s" % set_mode)
    children = (folder_state(session, folder).get("children") or [])
    recorder.check("every child's own mixer channel is the folder's channel",
                   children and all(c.get("mixer_channel") == channel for c in children),
                   "children=%s channel=%r" % (children, channel))
    mixer = session.result("mixer.get_state")
    held = [c for c in (mixer.get("channels") or []) if c.get("id") == "ch-%d" % channel]
    recorder.check("the folder's channel is a real mixer channel",
                   len(held) == 1, "held=%r" % held)
    return channel


def check_flags(session, recorder, fixture):
    """Pinning and the collapse flag: written, read back, and undone once each."""
    folder = fixture["folder"]
    pinned = session.result("track.set_pinned", {"track": folder, "pinned": True})
    collapsed = session.result("track.folder_set_collapsed", {"track": folder,
                                                              "collapsed": True})
    recorder.check("track.set_pinned writes and reports the pin",
                   pinned.get("pinned") is True, "reply=%s" % pinned)
    recorder.check("track.folder_set_collapsed writes and reports the collapse",
                   collapsed.get("collapsed") is True, "reply=%s" % collapsed)
    session.result("control.undo")
    session.result("control.undo")
    state = folder_state(session, folder)
    recorder.check("one undo per flag takes each back off (a live Track checkpoint)",
                   state.get("collapsed") is False and state.get("pinned") is False,
                   "state=%s" % state)
    session.result("track.set_pinned", {"track": folder, "pinned": True})
    session.result("track.folder_set_collapsed", {"track": folder, "collapsed": True})


def check_visibility_sets(session, recorder, fixture):
    """A named set: saved, applied, and it hides every non-member."""
    folder = fixture["folder"]
    saved = session.result("track.visibility_set_save",
                           {"name": "Strings", "tracks": fixture["tracks"]})
    recorder.check("track.visibility_set_save records the members",
                   saved.get("track_count") == 2 and saved.get("replaced") is False,
                   "reply=%s" % saved)
    applied = session.result("track.visibility_set_apply", {"name": "Strings"})
    recorder.check("applying the set shows its members and hides the rest",
                   applied.get("shown") == 2 and applied.get("hidden") == 1
                   and applied.get("active") == "Strings", "reply=%s" % applied)
    listed = {t.get("id"): t for t in (session.result("track.list").get("tracks") or [])}
    recorder.check("the visible flags read back per track",
                   listed.get(fixture["first"], {}).get("visible") is True
                   and listed.get(folder, {}).get("visible") is False,
                   "first=%r folder=%r" % (listed.get(fixture["first"]), listed.get(folder)))
    sets = session.result("track.visibility_set_list")
    recorder.check("the set is listed with its members and as the active one",
                   sets.get("count") == 1 and sets.get("active") == "Strings",
                   "reply=%s" % sets)


def check_refusals(session, recorder, fixture):
    """Every refusal is typed - never a silent no-op."""
    folder = fixture["folder"]
    not_a_folder = session.typed_error("track.set_folder", {"track": fixture["first"],
                                                            "folder": fixture["first"]})
    self_parent = session.typed_error("track.set_folder", {"track": folder, "folder": folder})
    not_a_folder_mode = session.typed_error("track.set_routing", {"track": fixture["first"],
                                                                  "routing": True})
    empty = make_track(session, "folder", "Empty Folder")
    empty_mode = session.typed_error("track.set_routing", {"track": empty, "routing": True})
    unknown_set = session.typed_error("track.visibility_set_apply", {"name": "No Such Set"})
    unknown_track = session.typed_error("track.folder_get_state", {"track": "trk-99999"})
    recorder.check("a target that is not a folder is refused, typed",
                   bool(not_a_folder.get("message")), "%r" % not_a_folder)
    recorder.check("a folder cannot hold itself, typed",
                   bool(self_parent.get("message")), "%r" % self_parent)
    recorder.check("routing mode on a non-folder is refused, typed",
                   bool(not_a_folder_mode.get("message")), "%r" % not_a_folder_mode)
    recorder.check("routing mode on a folder with no children is refused, typed",
                   bool(empty_mode.get("message")), "%r" % empty_mode)
    recorder.check("an unknown visibility set is refused, typed",
                   bool(unknown_set.get("message")), "%r" % unknown_set)
    recorder.check("an unknown track id is refused, typed",
                   bool(unknown_track.get("message")), "%r" % unknown_track)


def move_the_model_away(session, fixture):
    """Undo everything the file carries, so only the FILE can bring it back."""
    session.result("track.set_folder", {"track": fixture["first"], "folder": ""})
    session.result("track.set_routing", {"track": fixture["folder"], "routing": False})
    session.result("track.set_pinned", {"track": fixture["folder"], "pinned": False})
    session.result("track.folder_set_collapsed", {"track": fixture["folder"],
                                                 "collapsed": False})
    # the second child is still IN the folder; the first is already at the root
    session.result("track.set_folder", {"track": fixture["second"], "folder": ""})


def check_save_and_reopen(session, recorder, fixture, outdir):
    """THE ROUND TRIP: what comes back can only have come from the saved file."""
    path = os.path.join(outdir, "folder-tracks.mmp")
    saved = session.result("project.save", {"path": path})
    recorder.check("project.save wrote the folder session",
                   saved.get("file") == path and os.path.exists(path),
                   "file=%r" % saved.get("file"))

    move_the_model_away(session, fixture)
    recorder.check("the model no longer holds the folder's state",
                   folder_state(session, fixture["folder"]).get("child_count") == 0,
                   "state=%s" % folder_state(session, fixture["folder"]))

    reopened = session.result("project.open", {"path": path})
    recorder.check("project.open reloads the session", reopened.get("file") == path,
                   "file=%r" % reopened.get("file"))
    state = folder_state(session, fixture["folder"])
    check_relation_survives(session, recorder, fixture, state)
    check_sets_survive(session, recorder, fixture)


def check_relation_survives(session, recorder, fixture, state):
    """Membership, the mode, the two flags and the child bindings, from the file."""
    children = state.get("children") or []
    channel = state.get("mixer_channel")
    recorder.check("the MEMBERSHIP survives the round trip",
                   state.get("child_count") == 2
                   and sorted(c.get("track") for c in children) == sorted(fixture["tracks"]),
                   "state=%s" % state)
    recorder.check("the MODE survives the round trip",
                   state.get("mode") == "routing" and channel is not None and channel >= 0,
                   "state=%s" % state)
    recorder.check("the pinned and collapsed flags survive the round trip",
                   state.get("pinned") is True and state.get("collapsed") is True,
                   "state=%s" % state)
    recorder.check("the children are still bound to the folder's own channel",
                   children and all(c.get("mixer_channel") == channel for c in children),
                   "state=%s" % state)


def check_sets_survive(session, recorder, fixture):
    """The named set and the visible flags it wrote, from the file."""
    sets = session.result("track.visibility_set_list")
    recorder.check("the named visibility set survives the round trip",
                   sets.get("count") == 1 and sets.get("active") == "Strings",
                   "reply=%s" % sets)
    listed = {t.get("id"): t for t in (session.result("track.list").get("tracks") or [])}
    recorder.check("the visible flags the set wrote survive the round trip",
                   listed.get(fixture["first"], {}).get("visible") is True
                   and listed.get(fixture["folder"], {}).get("visible") is False,
                   "first=%r folder=%r" % (listed.get(fixture["first"]),
                                           listed.get(fixture["folder"])))


def check_routing_sums(session, recorder, fixture, outdir):
    """THE SUM, MEASURED: the children's audio really goes through the folder."""
    folder = fixture["folder"]
    channel = folder_state(session, folder).get("mixer_channel")
    if channel is None or channel < 0:
        recorder.check("the reloaded folder owns a channel to measure", False,
                       "folder_state=%s" % folder_state(session, folder))
        return None
    summed, frames, rendered = render_measure(session, os.path.join(outdir, "routing.wav"))
    recorder.check("a routing folder's session renders audio at all",
                   rendered.get("path") is not None and summed is not None
                   and summed > SILENT_DBFS + AUDIBLE_MARGIN_DB,
                   "%r frames=%r from %r" % (summed, frames, rendered.get("path")))
    if summed is None:
        return None

    session.result("mixer.set_volume", {"channel": "ch-%d" % channel, "volume": 0.0})
    muted, frames, rendered = render_measure(session, os.path.join(outdir, "folder-muted.wav"))
    recorder.check("the FOLDER's own fader silences the children's sum",
                   muted is not None and muted <= SILENT_DBFS,
                   "the folder channel at 0 rendered %r dBFS (%r frames)" % (muted, frames))

    session.result("mixer.set_volume", {"channel": "ch-%d" % channel, "volume": 1.0})
    back, frames, rendered = render_measure(session, os.path.join(outdir, "folder-back.wav"))
    recorder.check("the same fader back at unity sounds again, within %.1f dB"
                   % SUM_TOLERANCE_DB,
                   back is not None and abs(back - summed) <= SUM_TOLERANCE_DB,
                   "%r dBFS against %r dBFS" % (back, summed))

    session.result("track.set_routing", {"track": folder, "routing": False})
    state = folder_state(session, folder)
    recorder.check("group mode releases the folder's channel entirely",
                   state.get("mixer_channel") == -1,
                   "state=%s" % state)
    plain, frames, rendered = render_measure(session, os.path.join(outdir, "group.wav"))
    recorder.check("NEGATIVE CONTROL: with the mode off there is no folder channel to "
                   "silence, and the session sounds",
                   plain is not None and plain > SILENT_DBFS + AUDIBLE_MARGIN_DB,
                   "%r dBFS" % (plain,))
    return summed


def check_transactions(session, recorder):
    report = session.result("control.transactions")
    records = [r for r in report.get("transactions", [])
               if str(r.get("command", "")) in FOLDER_IDS]
    print("")
    print("---- the folder commands control.transactions reports ----")
    for record in records:
        print("%-32s class=%s reversible=%s" % (record.get("command"), record.get("class"),
                                                record.get("reversible")))
        print("             mechanism=%s" % record.get("mechanism"))
    classes = {r.get("class") for r in records}
    recorder.check("every folder command left an A16 record, all reversible",
                   records and all(r.get("reversible") is True for r in records),
                   "%d records: %s" % (len(records), sorted(classes)))
    recorder.check("the two flag verbs are true_inverse (a live Track checkpoint)",
                   all(r.get("class") == "true_inverse"
                       for r in records
                       if r.get("command") in ("track.set_pinned", "track.folder_set_collapsed")),
                   "classes=%s" % sorted(classes))


def check_quit(session, instance, recorder):
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        recorder.check("control.quit answered", False, "%r" % reply)
        return
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    recorder.check("control.quit stops the instance", exited and code == 0,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))


def run_checks(session, instance, recorder, transcript, outdir):
    fixture = build_fixture(session, instance, transcript)
    if fixture is None:
        return None
    check_folder_is_a_track_type(session, recorder, fixture)
    check_membership(session, recorder, fixture)
    check_routing_wiring(session, recorder, fixture)
    check_flags(session, recorder, fixture)
    check_visibility_sets(session, recorder, fixture)
    check_refusals(session, recorder, fixture)
    check_transactions(session, recorder)
    check_save_and_reopen(session, recorder, fixture, outdir)
    check_routing_sums(session, recorder, fixture, outdir)
    check_quit(session, instance, recorder)
    return fixture


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    fixture = None
    with H.start_instance(argv[1], workingdir=None) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        print("instance: %s" % argv[1])
        print("socket:   %s" % instance.socket_path)
        session.result("control.version")
        fixture = run_checks(session, instance, recorder, transcript, instance.tmp)

    report_results(recorder)
    transcript.dump()
    if fixture is None:
        H.ok("no audible instrument in this build: Skipped, never Passed")
        return 77
    if recorder.problems:
        print("")
        recorder.problems.report("folder tracks control-surface transcript")
        return 1
    H.ok("folder tracks control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
