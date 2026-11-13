#!/usr/bin/env python3
"""Control-socket coverage for named track playlists (track versions), Q050-275.

COVERAGE, NOT A REGRESSION. The track.version_* verbs
(src/core/ControlCommandsTrackVersions.cpp) are driven end to end through
--control-socket, and every answer is read back from the application itself.

WHAT IT ASSERTS:

  * two named sets, A and B, hold different clips; switching between them
    replaces the track's clips with the named set's clips (positions and lengths
    read off clip.list), and the set that was playing is kept as it was left;
  * project.save -> project.open -> track.version_list reports the same two
    names and the same active name, the clips match the active set, the saved
    document carries the <track-versions element, and switching after the reload
    still reaches the stored set;
  * control.undo / control.redo restore the name list, the active name and the
    clips for a save, a switch and a remove of the playing set (which keeps
    the clips and clears the active name);
  * the typed error kinds for invalid paths: blank save name -> invalid_args,
    unknown name -> not_found, switch to the playing name -> refused,
    remove of an unknown name -> not_found; a refused call changes nothing;
  * a project with no versions writes no <track-versions element, saved, reopened
    and saved again.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-track-playlists.py <zene-binary>

Exit codes: 0 every check held; 1 a check failed (the app log is printed);
2 cannot run (no binary at the path).
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

#: The track the checks build on.
TRACK_NAME = "Playlists"

#: The two named sets, as (start tick, length in ticks) pairs. A plays one
#: 96-tick clip at tick 0; B plays a 192-tick clip at 192 and a 288-tick clip
#: at 384. Distinct lengths let the checks tell the sets apart by identity.
SET_A = [(0, 96)]
SET_B = [(192, 192), (384, 288)]

#: The element the project writes only while the track holds versions.
VERSIONS_ELEMENT = "<track-versions"


# ---------------------------------------------------------------------------
# reading the wire
# ---------------------------------------------------------------------------


class Session:
    """One request per command, with the reply kept for the transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 0

    def call(self, command, args=None):
        self.last_id += 1
        return self.client.call(self.last_id, command, args, transcript=self.transcript)

    def result(self, command, args=None):
        """The reply's result, or {'error': ...} so a failed call is visible."""
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}


class Recorder:
    """Collects the named checks and their evidence, so a failure names the bytes."""

    def __init__(self):
        self.results = []

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))

    def problems(self):
        return [(name, evidence) for name, passed, evidence in self.results if not passed]


def read_project_xml(path):
    """The project's XML as text. `project.save` writes .mmpz as Qt's qCompress
    output (a big-endian length, then zlib), and anything else as plain XML."""
    import zlib

    with open(path, "rb") as handle:
        blob = handle.read()
    try:
        return zlib.decompress(blob[4:]).decode("utf-8", "replace")
    except zlib.error:
        return blob.decode("utf-8", "replace")


def save_and_read(session, path):
    """`project.save` to `path`, then the file's own bytes: (result, XML text)."""
    saved = session.result("project.save", {"path": path})
    text = read_project_xml(path) if os.path.exists(path) else ""
    return saved, text


def track_clips(session, track):
    """The track's clips as a sorted list of (start, length), read off clip.list."""
    listed = session.result("clip.list", {"track": track})
    return sorted((clip.get("start"), clip.get("length")) for clip in listed.get("clips") or [])


def version_state(session, track):
    """(active name, sorted version names) off track.version_list."""
    listed = session.result("track.version_list", {"track": track})
    return listed.get("active"), listed.get("versions")


def replace_clips(session, track, clips):
    """Delete every clip on the track, then add `clips` as (start, length) pairs."""
    for clip in session.result("clip.list", {"track": track}).get("clips") or []:
        session.result("clip.delete", {"clip": clip.get("clip")})
    for start, length in clips:
        session.result("clip.add", {"track": track, "position": start, "length": length})


def error_kind(session, command, args):
    """The typed error kind of a refused call, or 'ok' when the call succeeded."""
    reply = session.call(command, args)
    if reply.get("ok") is True:
        return "ok"
    return (reply.get("error") or {}).get("kind")


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


def check_no_versions_no_element(session, instance, recorder):
    """A fresh project with no versions: save, open, save writes no element."""
    first_path = os.path.join(instance.workspace, "playlists-fresh.mmpz")
    second_path = os.path.join(instance.workspace, "playlists-fresh-2.mmpz")
    saved, first = save_and_read(session, first_path)
    opened = session.result("project.open", {"path": first_path})
    _, second = save_and_read(session, second_path)
    recorder.check("a project with no versions writes no <track-versions> element",
                   saved.get("saved") is True and bool(opened.get("file"))
                   and bool(first) and bool(second)
                   and VERSIONS_ELEMENT not in first and VERSIONS_ELEMENT not in second,
                   "saved=%r opened=%r first-bytes=%d second-bytes=%d has-element=%r/%r"
                   % (saved.get("saved"), opened.get("file"), len(first), len(second),
                      VERSIONS_ELEMENT in first, VERSIONS_ELEMENT in second))


def build_two_sets(session, track, recorder):
    """Author set B, save it, copy it into A, then author A and save A again.

    track.version_save copies the live clips into the set that is playing
    (saveVersion), so a second name must be saved while the live clips are the
    right ones for the set being left: B first, then A is saved from B's clips
    and rebuilt, then saved again."""
    replace_clips(session, track, SET_B)
    session.result("track.version_save", {"track": track, "name": "B"})
    session.result("track.version_save", {"track": track, "name": "A"})
    replace_clips(session, track, SET_A)
    saved = session.result("track.version_save", {"track": track, "name": "A"})
    active, versions = version_state(session, track)
    recorder.check("two named sets exist and A is the one playing",
                   versions == ["A", "B"] and active == "A" and saved.get("active") == "A",
                   "versions=%r active=%r" % (versions, active))


def check_switching(session, track, recorder):
    """Switch to B, then A, then B: each switch brings the named set's clips."""
    want = {"B": SET_B, "A": SET_A, "B_again": SET_B}
    for label in ("B", "A", "B_again"):
        name = "B" if label.startswith("B") else "A"
        session.result("track.version_switch", {"track": track, "name": name})
        active, _ = version_state(session, track)
        clips = track_clips(session, track)
        recorder.check("switching to %s brings its clips and makes it the one playing" % name,
                       active == name and clips == sorted(want[label]),
                       "active=%r clips=%r want=%r" % (active, clips, sorted(want[label])))


def check_save_reload(session, instance, track, recorder):
    """project.save -> project.open: the sets and the active name come back."""
    path = os.path.join(instance.workspace, "playlists-proof.mmpz")
    saved, text = save_and_read(session, path)
    opened = session.result("project.open", {"path": path})
    active, versions = version_state(session, track)
    clips = track_clips(session, track)
    recorder.check("the saved document carries the track versions element",
                   saved.get("saved") is True and VERSIONS_ELEMENT in text,
                   "saved=%r element-present=%r" % (saved.get("saved"), VERSIONS_ELEMENT in text))
    recorder.check("after the reload the two names and the active name come back",
                   bool(opened.get("file")) and versions == ["A", "B"] and active == "B",
                   "opened=%r versions=%r active=%r" % (opened.get("file"), versions, active))
    recorder.check("after the reload the clips are the active set's clips",
                   clips == sorted(SET_B), "clips=%r want=%r" % (clips, sorted(SET_B)))

    session.result("track.version_switch", {"track": track, "name": "A"})
    active, _ = version_state(session, track)
    clips = track_clips(session, track)
    recorder.check("after the reload a switch to A still reaches the stored set",
                   active == "A" and clips == sorted(SET_A),
                   "active=%r clips=%r" % (active, clips))
    session.result("track.version_switch", {"track": track, "name": "B"})
    active, _ = version_state(session, track)
    clips = track_clips(session, track)
    recorder.check("and a switch back to B after that reload reaches B",
                   active == "B" and clips == sorted(SET_B),
                   "active=%r clips=%r" % (active, clips))


def check_undo_redo(session, track, recorder):
    """Each mutating verb is one undo step: undo restores, redo re-applies."""
    # save a third name C while B is playing: the name list grows, C plays
    session.result("track.version_save", {"track": track, "name": "C"})
    before_undo = version_state(session, track)
    undone = session.result("control.undo")
    after_undo = version_state(session, track)
    clips_after_undo = track_clips(session, track)
    recorder.check("undo of a version_save removes the name and restores the playing name",
                   before_undo == ("C", ["A", "B", "C"])
                   and after_undo == ("B", ["A", "B"]) and clips_after_undo == sorted(SET_B),
                   "undone=%r before=%r after=%r clips=%r"
                   % (undone.get("undone"), before_undo, after_undo, clips_after_undo))
    session.result("control.redo")
    after_redo = version_state(session, track)
    recorder.check("redo of a version_save restores the name and the playing name",
                   after_redo == ("C", ["A", "B", "C"]), "after redo=%r" % (after_redo,))

    # switch from C to A: undo puts C back, redo applies the switch again
    session.result("track.version_switch", {"track": track, "name": "A"})
    switched = version_state(session, track)
    session.result("control.undo")
    undone_state = version_state(session, track)
    undone_clips = track_clips(session, track)
    recorder.check("undo of a version_switch restores the previous name and its clips",
                   switched == ("A", ["A", "B", "C"]) and undone_state == ("C", ["A", "B", "C"])
                   and undone_clips == sorted(SET_B),
                   "switched=%r undone=%r clips=%r" % (switched, undone_state, undone_clips))
    session.result("control.redo")
    redone_state = version_state(session, track)
    redone_clips = track_clips(session, track)
    recorder.check("redo of a version_switch re-applies the switch",
                   redone_state == ("A", ["A", "B", "C"]) and redone_clips == sorted(SET_A),
                   "redone=%r clips=%r" % (redone_state, redone_clips))

    # remove the playing set A: the clips stay, the active name clears; undo brings A back
    session.result("track.version_remove", {"track": track, "name": "A"})
    removed = version_state(session, track)
    removed_clips = track_clips(session, track)
    recorder.check("removing the playing set clears the active name and keeps the clips",
                   removed == ("", ["B", "C"]) and removed_clips == sorted(SET_A),
                   "removed=%r clips=%r" % (removed, removed_clips))
    session.result("control.undo")
    restored = version_state(session, track)
    restored_clips = track_clips(session, track)
    recorder.check("undo of a version_remove brings the removed set back as the playing set",
                   restored == ("A", ["A", "B", "C"]) and restored_clips == sorted(SET_A),
                   "restored=%r clips=%r" % (restored, restored_clips))


def check_invalid_paths(session, track, recorder):
    """Typed error kinds for invalid paths; a refused call changes nothing."""
    before = version_state(session, track)
    kinds = {
        "blank save name": error_kind(session, "track.version_save", {"track": track, "name": "  "}),
        "switch to an unknown name": error_kind(session, "track.version_switch",
                                                {"track": track, "name": "nope"}),
        "switch to the playing name": error_kind(session, "track.version_switch",
                                                 {"track": track, "name": "A"}),
        "remove an unknown name": error_kind(session, "track.version_remove",
                                             {"track": track, "name": "nope"}),
    }
    want = {"blank save name": "invalid_args", "switch to an unknown name": "not_found",
            "switch to the playing name": "refused", "remove an unknown name": "not_found"}
    recorder.check("invalid paths answer with their typed error kinds",
                   kinds == want, "kinds=%r want=%r" % (kinds, want))
    recorder.check("the refused calls leave the names and the playing name as they were",
                   version_state(session, track) == before,
                   "before=%r after=%r" % (before, version_state(session, track)))


def check_quit(session, instance, recorder):
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        recorder.check("control.quit answered", False, "%r" % reply)
        return
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    recorder.check("control.quit stops the instance", exited and code == 0,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))


def report(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-62s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)
    problems = recorder.problems()
    if problems:
        print("")
        print("FAIL: the track playlist coverage has %d failed check(s)" % len(problems))
        return 1
    print("")
    print("PASS: %d checks, every one read back from the application" % len(recorder.results))
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    if not os.path.exists(argv[1]):
        print("cannot run: no binary at %s" % argv[1])
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    with H.start_instance(argv[1]) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        print("instance: %s" % argv[1])
        print("socket:   %s" % instance.socket_path)
        print("workspace: %s" % instance.workspace)

        session.result("control.version")
        check_no_versions_no_element(session, instance, recorder)
        track = session.result("track.add", {"type": "sample", "name": TRACK_NAME}).get("track")
        build_two_sets(session, track, recorder)
        check_switching(session, track, recorder)
        check_save_reload(session, instance, track, recorder)
        check_undo_redo(session, track, recorder)
        check_invalid_paths(session, track, recorder)
        check_quit(session, instance, recorder)
    transcript.dump()
    code = report(recorder)
    if code == 0:
        H.ok("the track playlists hold through switch, reload, undo and the error kinds")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
