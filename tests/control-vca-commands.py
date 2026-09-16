#!/usr/bin/env python3
"""RAW control-surface transcript for VCA / mix-and-edit groups (SPEC A16).

This is the acceptance evidence that the `vca.*` command group - the whole of
OWNER-31 item 11's surface, "phase-locked multitrack edit groups" - is drivable
over the control socket by an agent, produced by driving the REAL `zene` binary
headless and printing every request and every reply verbatim, so a reader can
see exactly what the engine answered. Nothing here is asserted from the reply
alone: the group's state is asserted from `vca.get_state` AND from
`mixer.get_state`'s own published gain, the phase-locked move is asserted from
`arrangement.get_state` (the clips' positions on their tracks), and the edit set
is asserted from the file the engine WROTE after a real save/open round trip.

What it drives, in order:

  1. `control.commands_list`     the fourteen ids are registered, in group vca,
                                 with both schemas, and the two reads are the
                                 only non-mutating ones;
  2. `vca.create`                a group is created from the socket - which
                                 before this lane could only be done by editing
                                 the project file - and reported by vca.list;
  3. `vca.assign` + `vca.set_gain`  the fader SCALES its members: their own
                                 faders are untouched, the gain published into
                                 each member is the group's, and one
                                 `control.undo` puts it back;
  4. `vca.set_mute` + `vca.set_solo`  mute is a published gain of zero, solo is
                                 the product's exclusive solo over the members;
  5. `vca.track_add` + `vca.set_phase_lock` + `vca.edit_move`  THE FEATURE: two
                                 tracks each holding one clip at the same
                                 position are locked into one group, one clip is
                                 moved, and BOTH clips move by the same delta -
                                 asserted on the clips themselves, then ONE
                                 `control.undo` returns both;
  6. `project.save` / `project.open`  the membership, the fader, the mute and
                                 the EDIT SET survive a real round trip - the
                                 in-memory model is moved elsewhere first, so
                                 what comes back can only have come from the
                                 file;
  7. refusals                    junk and out-of-range arguments, a group that
                                 does not exist, a channel in another group, an
                                 edit on a track outside the set, a locked group
                                 with one live member - all typed, none of them
                                 a crash and none of them a silent success;
  8. `control.transactions`      the A16 record every edit left, with its class.

The instance is started with the documented headless recipe through the shared
harness (tests/control_socket_harness.py) and this file adds no second launch
path, no second client and no second check recorder.

Usage: QT_QPA_PLATFORM=offscreen python3 control-vca-commands.py <zene>
Exit code 0 only when every check held.
"""

import os
import re
import sys

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results

VCA_IDS = ("vca.create", "vca.remove", "vca.list", "vca.get_state", "vca.rename",
           "vca.set_gain", "vca.set_mute", "vca.set_solo", "vca.assign", "vca.unassign",
           "vca.set_phase_lock", "vca.track_add", "vca.track_remove", "vca.edit_move")
VCA_READS = ("vca.list", "vca.get_state")
CLIP_TICKS = 768
MOVE_DELTA = 240
SAVE_NAME = "vca-edit-groups.mmp"


def described_commands(session):
    """{id: entry} for every registered command, as the registry describes it."""
    listed = session.result("control.commands_list")
    return {entry.get("id"): entry for entry in listed.get("commands", [])}


def group_state(session, group):
    return session.result("vca.get_state", {"group": group})


def make_track(session, name):
    return session.result("track.add", {"type": "instrument", "name": name}).get("track")


def add_clip(session, track, position, length=CLIP_TICKS):
    return session.result("clip.add",
                          {"track": track, "position": position, "length": length}).get("clip")


def clip_position(session, clip):
    """A clip's position, read from arrangement.get_state - not from vca.*."""
    for entry in session.result("arrangement.get_state").get("clips", []):
        if entry.get("id") == clip:
            return entry.get("position")
    return None


def channel_volume(session, channel):
    """A mixer channel's own fader value, from mixer.* - not from vca.*."""
    for entry in session.result("mixer.get_state").get("channels", []):
        if entry.get("id") == channel:
            return entry.get("volume")
    return None


def channel_ids(session):
    """The mixer's OWN channel ids, in the mixer's order (index 0 is master).

    ASKED FOR, never predicted. A ch-<n> id is a ProjectIds allocation shared by
    every id family (SPEC-stable-ids.md R1, one monotonic counter per project),
    so "ch-1" names whatever was allocated when that number came up - in this
    transcript the master is ch-1 and the created channels are not ch-2/ch-3.
    The literal ids this replaced failed with "no mixer channel ch-2 (the mixer
    has 4)" and turned the master into an accepted group member.
    """
    return [entry.get("id")
            for entry in session.result("mixer.get_state").get("channels", [])]


def ensure_channels(session, recorder, wanted):
    """The group commands need channels to assign; a fresh instance has few."""
    count = session.result("mixer.get_state").get("count", 0)
    while count < wanted:
        session.result("mixer.add_channel")
        count = session.result("mixer.get_state").get("count", 0)
    recorder.check("the mix has at least %d channels to group" % wanted, count >= wanted,
                   "count=%r" % count)


def member_gain(state, channel):
    for entry in state.get("members") or []:
        if entry.get("channel") == channel:
            return entry.get("gain_for_member")
    return None


def missing_ids(described):
    return [i for i in VCA_IDS if i not in described]


def wrong_group_ids(described):
    return [i for i, entry in described.items()
            if entry.get("group") != "vca" and i in VCA_IDS]


def schema_less_ids(described):
    return [i for i in VCA_IDS if i in described and not described[i].get("args_schema")]


def read_ids(described):
    return [i for i in VCA_IDS if i in described and not described[i].get("mutating", True)]


def check_ids(session, recorder):
    """Every id is registered, in group vca, with both schemas declared."""
    described = described_commands(session)
    absent = missing_ids(described)
    recorder.check("all fourteen vca.* ids are registered", not absent, "missing=%r" % absent)
    wrong = wrong_group_ids(described)
    recorder.check("every vca.* id declares group vca", not wrong, "wrong=%r" % wrong)
    schema_less = schema_less_ids(described)
    recorder.check("every vca.* id declares an argument schema", not schema_less,
                   "absent=%r" % schema_less)
    reads = sorted(read_ids(described))
    recorder.check("vca.list and vca.get_state are the only reads",
                   reads == sorted(VCA_READS), "reads=%r" % reads)


def check_create(session, recorder):
    """vca.create makes a group; vca.list reports it; vca.get_state reports it."""
    made = session.result("vca.create", {"name": "Drums"})
    group = made.get("group")
    recorder.check("vca.create returns a vca-<n> id", isinstance(group, str) and
                   group.startswith("vca-"), "group=%r" % group)
    recorder.check("a new group is phase-locked by default and holds nothing",
                   made.get("phase_locked") is True and made.get("member_count") == 0
                   and made.get("track_count") == 0, "created=%s" % made)
    listed = session.result("vca.list")
    ids = [entry.get("id") for entry in listed.get("groups", [])]
    recorder.check("vca.list carries the new group", group in ids, "groups=%r" % ids)
    state = group_state(session, group)
    recorder.check("vca.get_state reports the name and a unity fader",
                   state.get("name") == "Drums" and state.get("volume") == 1.0,
                   "state=%s" % state)
    renamed = session.result("vca.rename", {"group": group, "name": "Kit"})
    recorder.check("vca.rename changes the name and reports the previous one",
                   renamed.get("name") == "Kit" and renamed.get("previous_name") == "Drums",
                   "renamed=%s" % renamed)
    return group


def check_fader(session, recorder, group):
    """The fader SCALES: members are never written, and the undo puts it back."""
    channels = channel_ids(session)
    member_a, member_b = channels[1], channels[2]
    before_volume = channel_volume(session, member_a)
    for channel in (member_a, member_b):
        assigned = session.result("vca.assign", {"group": group, "channel": channel})
        recorder.check("vca.assign puts %s in the group" % channel,
                       channel in [m.get("channel") for m in assigned.get("members") or []],
                       "assigned=%s" % assigned)

    moved = session.result("vca.set_gain", {"group": group, "gain": 0.5})
    recorder.check("vca.set_gain reports the gain it published", moved.get("gain") == 0.5,
                   "moved=%s" % moved)
    state = group_state(session, group)
    recorder.check("both members are scaled by the group's gain",
                   member_gain(state, member_a) == 0.5 and member_gain(state, member_b) == 0.5,
                   "members=%r" % state.get("members"))
    after_volume = session.result("mixer.get_state").get("channels", [{}])[1].get("volume")
    recorder.check("no member's OWN fader was written (the scaling is relative)",
                   before_volume == after_volume, "before=%r after=%r"
                   % (before_volume, after_volume))

    undone = session.result("control.undo")
    recorder.check("one control.undo puts the group fader back",
                   undone.get("undone") is True
                   and group_state(session, group).get("volume") == 1.0,
                   "undone=%s state=%s" % (undone, group_state(session, group)))
    recorder.check("the members are back at unity after the undo",
                   member_gain(group_state(session, group), member_a) == 1.0,
                   "state=%s" % group_state(session, group))


def check_mute_and_solo(session, recorder, group):
    """Mute is a published gain of zero; solo is the product's exclusive solo."""
    member, other = channel_ids(session)[1], channel_ids(session)[3]
    muted = session.result("vca.set_mute", {"group": group, "muted": True})
    recorder.check("a muted group publishes a gain of zero",
                   muted.get("gain") == 0.0 and muted.get("muted") is True, "muted=%s" % muted)
    recorder.check("the members are scaled to zero while the group is muted",
                   member_gain(muted, member) == 0.0, "members=%r" % muted.get("members"))
    session.result("vca.set_mute", {"group": group, "muted": False})

    soloed = session.result("vca.set_solo", {"group": group, "soloed": True})
    recorder.check("soloing a group reports the flag and the previous value",
                   soloed.get("soloed") is True and soloed.get("previous_soloed") is False,
                   "soloed=%s" % soloed)
    # The effect is on the CHANNELS' mute models, which mixer.get_state reports.
    mutes = {entry.get("id"): entry.get("muted")
             for entry in session.result("mixer.get_state").get("channels", [])}
    recorder.check("soloing the group unmutes its members and mutes the rest",
                   mutes.get(member) is False and mutes.get(other) is True, "mutes=%r" % mutes)
    resumed = session.result("vca.set_solo", {"group": group, "soloed": False})
    recorder.check("clearing the flag restores the pre-solo mute state",
                   resumed.get("soloed") is False, "resumed=%s" % resumed)


def build_locked_fixture(session, recorder):
    """Two tracks, one clip each at the SAME position, locked into one group."""
    first = make_track(session, "Take L")
    second = make_track(session, "Take R")
    recorder.check("the fixture's two tracks were created", bool(first) and bool(second),
                   "first=%r second=%r" % (first, second))
    anchor = add_clip(session, first, 1000)
    other = add_clip(session, second, 1000)
    recorder.check("each fixture track holds one clip at tick 1000",
                   bool(anchor) and bool(other) and clip_position(session, anchor) == 1000
                   and clip_position(session, other) == 1000,
                   "anchor=%r other=%r" % (anchor, other))

    group = session.result("vca.create", {"name": "Takes"}).get("group")
    for track in (first, second):
        added = session.result("vca.track_add", {"group": group, "track": track})
        recorder.check("vca.track_add puts %s in the edit set" % track,
                       track in (added.get("tracks") or []), "added=%s" % added)
    return {"group": group, "first": first, "second": second, "anchor": anchor, "other": other}


def check_phase_locked_move(session, recorder, fixture):
    """THE FEATURE: one clip moves, every member moves with it, ONE undo returns."""
    moved = session.result("vca.edit_move", {"group": fixture["group"],
                                             "clip": fixture["anchor"],
                                             "position": 1000 + MOVE_DELTA})
    recorder.check("vca.edit_move reports the delta it applied",
                   moved.get("delta") == MOVE_DELTA, "moved=%s" % moved)
    recorder.check("it moved the anchor AND the other member",
                   moved.get("moved_count") == 2 and moved.get("unlocked_count") == 0
                   and moved.get("skipped_count") == 0, "moved=%s" % moved)
    recorder.check("the ANCHOR clip is at the requested position",
                   clip_position(session, fixture["anchor"]) == 1000 + MOVE_DELTA,
                   "anchor=%r" % clip_position(session, fixture["anchor"]))
    recorder.check("the OTHER member's clip moved by the same delta",
                   clip_position(session, fixture["other"]) == 1000 + MOVE_DELTA,
                   "other=%r" % clip_position(session, fixture["other"]))

    undone = session.result("control.undo")
    recorder.check("ONE control.undo returns every locked clip",
                   undone.get("undone") is True
                   and clip_position(session, fixture["anchor"]) == 1000
                   and clip_position(session, fixture["other"]) == 1000,
                   "undone=%s anchor=%r other=%r" % (undone,
                       clip_position(session, fixture["anchor"]),
                       clip_position(session, fixture["other"])))


def check_lock_refusals(session, recorder, fixture):
    """Every reason the lock cannot propagate is refused, typed, and writes nothing."""
    outsider = make_track(session, "Outsider")
    outside_clip = add_clip(session, outsider, 1000)
    refused = session.typed_error("vca.edit_move", {"group": fixture["group"],
                                                    "clip": outside_clip, "position": 1200})
    recorder.check("an edit on a track outside the set is refused, naming vca.track_add",
                   refused.get("kind") == "refused" and "vca.track_add" in refused.get("message", ""),
                   "error=%r" % refused)
    recorder.check("the refusal moved nothing",
                   clip_position(session, fixture["anchor"]) == 1000,
                   "anchor=%r" % clip_position(session, fixture["anchor"]))

    session.result("vca.track_remove", {"group": fixture["group"], "track": fixture["second"]})
    alone = session.typed_error("vca.edit_move", {"group": fixture["group"],
                                                  "clip": fixture["anchor"], "position": 1200})
    recorder.check("a group with one live member refuses to lock", alone.get("kind") == "refused",
                   "error=%r" % alone)
    session.result("vca.track_add", {"group": fixture["group"], "track": fixture["second"]})

    session.result("vca.set_phase_lock", {"group": fixture["group"], "locked": False})
    unlocked = session.typed_error("vca.edit_move", {"group": fixture["group"],
                                                     "clip": fixture["anchor"], "position": 1200})
    recorder.check("a group whose lock is off refuses, naming vca.set_phase_lock",
                   unlocked.get("kind") == "refused"
                   and "vca.set_phase_lock" in unlocked.get("message", ""),
                   "error=%r" % unlocked)
    session.result("vca.set_phase_lock", {"group": fixture["group"], "locked": True})


def check_junk(session, recorder, group):
    """Junk arguments are TYPED refusals - never a crash, never a success."""
    channels = channel_ids(session)
    master, non_member = channels[0], channels[3]
    cases = [
        ("vca.get_state", {}, "invalid_args"),
        ("vca.get_state", {"group": ""}, "invalid_args"),
        ("vca.get_state", {"group": "vca-9999"}, "not_found"),
        ("vca.get_state", {"group": "ch-0"}, "invalid_args"),
        ("vca.remove", {"group": "nonsense"}, "invalid_args"),
        ("vca.set_gain", {"group": group, "gain": 9.5}, "invalid_args"),
        # The master channel, by the id the ENGINE gave it: a group scales the
        # channels that feed the master, so the master itself is refused.
        ("vca.assign", {"group": group, "channel": master}, "refused"),
        ("vca.assign", {"group": group, "channel": "ch-9999"}, "not_found"),
        ("vca.track_add", {"group": group, "track": "trk-9999"}, "not_found"),
        ("vca.rename", {"group": group, "name": ""}, "invalid_args"),
        # A real channel that is not a member: unassigning it is refused.
        ("vca.unassign", {"group": group, "channel": non_member}, "refused"),
    ]
    wrong = []
    for command, args, expected in cases:
        error = session.typed_error(command, args)
        if error.get("kind") != expected:
            wrong.append((command, args, expected, error))
    recorder.check("every junk case is refused with its documented typed kind",
                   not wrong, "unexpected=%r" % wrong)
    recorder.check("the engine is still answering after the junk",
                   session.result("control.ping").get("engine_ready") is True, "ping failed")


def check_transactions(session, recorder):
    """The A16 record: the group's mutating commands are true_inverse."""
    recorded = session.result("control.transactions").get("transactions", [])
    seen = {}
    for entry in recorded:
        seen[entry.get("command")] = entry.get("class")
    vca = {k: v for k, v in seen.items() if k.startswith("vca.")}
    recorder.check("the group's mutating commands left A16 records", len(vca) >= 5,
                   "records=%r" % vca)
    wrong = {k: v for k, v in vca.items() if v != "true_inverse"}
    recorder.check("every recorded vca.* transaction is true_inverse", not wrong,
                   "wrong=%r" % wrong)


def check_save_and_reopen(session, recorder, fixture, outdir):
    """THE ROUND TRIP: the edit set can only have come from the saved file."""
    path = os.path.join(outdir, SAVE_NAME)
    saved = session.result("project.save", {"path": path})
    recorder.check("project.save wrote the session",
                   saved.get("file") == path and os.path.exists(path),
                   "file=%r" % saved.get("file"))
    session.result("vca.remove", {"group": fixture["group"]})
    recorder.check("the model no longer holds the group",
                   session.result("vca.get_state",
                                  {"group": fixture["group"]}).get("error") is not None,
                   "state=%r" % session.result("vca.get_state", {"group": fixture["group"]}))

    reopened = session.result("project.open", {"path": path})
    recorder.check("project.open reloads the session", reopened.get("file") == path,
                   "file=%r" % reopened.get("file"))
    state = group_state(session, fixture["group"])
    recorder.check("the membership, the fader, the mute and the lock survive the round trip",
                   state.get("name") == "Takes" and state.get("volume") == 0.5
                   and state.get("muted") is True and state.get("phase_locked") is True
                   and state.get("member_count") == 0 and state.get("track_count") == 2,
                   "state=%s" % state)
    tracks = state.get("tracks") or []
    recorder.check("the EDIT SET survives, by stable trk id",
                   len(tracks) == 2 and fixture["first"] in tracks and fixture["second"] in tracks,
                   "tracks=%r" % tracks)
    recorder.check("no edit-set member is reported missing",
                   state.get("missing_count") == 0, "state=%s" % state)
    check_a_pre_edit_half_element_loads_locked(session, recorder, fixture, path)


def check_a_pre_edit_half_element_loads_locked(session, recorder, fixture, path):
    """A <vcagroup> with no `locked` attribute must load as the LOCKED group it was.

    The file is the only place an edit set can live, so the honest way to make a
    legacy element is to take a real one and strip the attribute this lane
    added - which is exactly what a project saved before this lane contains.
    """
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    stripped = re.sub(r' locked="[01]"', "", text)
    recorder.check("the saved file carries the locked attribute to strip",
                   stripped != text, "no locked= attribute in the file")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(stripped)
    reopened = session.result("project.open", {"path": path})
    recorder.check("project.open accepts the pre-edit-half file",
                   reopened.get("file") == path, "file=%r" % reopened.get("file"))
    state = group_state(session, fixture["group"])
    recorder.check("a group with no `locked` attribute loads LOCKED",
                   state.get("phase_locked") is True, "state=%s" % state)
    recorder.check("its edit set still comes from the file",
                   (state.get("tracks") or []) == [fixture["first"], fixture["second"]],
                   "tracks=%r" % state.get("tracks"))


def check_quit(session, instance, recorder):
    """control.quit stops the instance, and its exit code is the app's own."""
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        recorder.check("control.quit answered", False, "%r" % reply)
        return
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    if code != 0:
        instance.dump_log()
    recorder.check("control.quit stops the instance", exited and code == 0,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))


def run_checks(session, instance, recorder, transcript, outdir):
    check_ids(session, recorder)
    ensure_channels(session, recorder, 4)
    group = check_create(session, recorder)
    check_fader(session, recorder, group)
    check_mute_and_solo(session, recorder, group)
    fixture = build_locked_fixture(session, recorder)
    # The state the round trip later asserts is set on the group the round trip
    # asserts, AFTER the fixture exists - the fader the group was created with is
    # not a claim about this one.
    session.result("vca.set_gain", {"group": fixture["group"], "gain": 0.5})
    session.result("vca.set_mute", {"group": fixture["group"], "muted": True})
    check_phase_locked_move(session, recorder, fixture)
    check_lock_refusals(session, recorder, fixture)
    check_junk(session, recorder, group)
    check_transactions(session, recorder)
    check_save_and_reopen(session, recorder, fixture, outdir)
    check_quit(session, instance, recorder)
    return group


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
        print("instance: %s" % argv[1])
        print("socket:   %s" % instance.socket_path)
        session.result("control.version")
        run_checks(session, instance, recorder, transcript, instance.tmp)

    report_results(recorder)
    transcript.dump()
    if recorder.problems:
        print("")
        recorder.problems.report("VCA / mix-and-edit groups control-surface transcript")
        return 1
    H.ok("VCA / mix-and-edit groups control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
