#!/usr/bin/env python3
"""The import-lifetime regressions: a DAWproject import must not crash the
instance that performed it, nor the next command after it.

Three independent sightings of ONE defect (2026-09-25, free-form hunt):
`dawproject.export -> dawproject.read -> dawproject.import ->
interchange.smf_convention` (socket reset), `dawproject.export ->
dawproject.import -> control.undo` (no reply at all), and the same chain with a
`project.open` first. All three died with SIGSEGV in
`~InstrumentTrackView()` (src/gui/tracks/InstrumentTrackView.cpp:178), from a
DEFERRED `deleteLater` one event-loop iteration after the import deleted the
Song's tracks: `applyDawProjectModel()` and the import's own undo
(`restoreSession`) called `Song::clearAllTracks()` without the announcement every
other wholesale replacement in this tree makes first
(`TrackContainer::aboutToClearTracks()` -> `TrackContainerView::removeAllTrackViews()`,
the fix docs/UNDO-RELEASE-CONFIG.md records for the journal-restore door).

This file drives the REAL binary over --control-socket, because the defect is a
process death: the load-bearing red is the harness's own "no response line /
connection reset" (the process gone, exit -11), and the green is a reply to the
next request plus an exit code of 0 at the end. Assertions are made on replies
and on the session read back - a chain that answers but leaves the music in a
state nobody asked for would still fail here.

Usage: QT_QPA_PLATFORM=offscreen python3 control-import-lifetime.py <zene>
Exit code 0 only when every check passed. The instance's own death is detected
by the harness's bounded read (Blocked), which `H.finish` reports as a failure,
never as a pass.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402


class Session:
    """One request id at a time, with the transcript kept for the evidence."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.next_id = 100

    def call(self, command, args=None):
        self.next_id += 1
        return self.client.call(self.next_id, command, args or {}, transcript=self.transcript)

    def ok(self, command, args=None):
        return H.ok_result(self.call(command, args), self.next_id)

    def error(self, kind, command, args=None):
        return H.typed_error(self.call(command, args), self.next_id, kind)


def plugin_env(binary):
    plugin_dir = os.path.join(os.path.dirname(os.path.abspath(binary)), "plugins")
    return {"LMMS_PLUGIN_DIR": plugin_dir} if os.path.isdir(plugin_dir) else None


def alive_check(instance, problems, where):
    """A crash is the bug; a live instance with the socket still answering is not."""
    if not instance.alive():
        problems.add("the instance DIED (exit %r) %s" % (instance.process.returncode, where))
        return False
    return True


def import_then_a_query(session, instance, problems, out_dir, tag):
    """export -> read -> import -> a query. The query is what died first."""
    path = os.path.join(out_dir, "chain-%s.dawproject" % tag)
    exported = session.ok("dawproject.export", {"path": path, "overwrite": True})
    if not exported.get("bytes"):
        problems.add("%s: dawproject.export wrote nothing (%r)" % (tag, exported))
        return None
    session.ok("dawproject.read", {"path": path})
    imported = session.ok("dawproject.import", {"path": path})
    # The query AFTER the import: an unrelated, read-only command, which is the
    # one that found the socket reset in the reported sighting. It must answer,
    # and it must answer with the convention the build implements.
    convention = session.ok("interchange.smf_convention")
    if not convention.get("ticks_per_quarter"):
        problems.add("%s: interchange.smf_convention answered %r" % (tag, convention))
    if not alive_check(instance, problems, "after the import and the query (%s)" % tag):
        return None
    return imported


def check_import_then_query(session, instance, problems, out_dir):
    state_before = session.ok("arrangement.get_state")
    imported = import_then_a_query(session, instance, problems, out_dir, "verify")
    if imported is None:
        return None
    state_after = session.ok("arrangement.get_state")
    # The import really did replace the session (otherwise the crash would not
    # have been reachable and this regression proves nothing).
    if state_after.get("track_count") != imported.get("track_count"):
        problems.add("the import's reply and the session disagree: %r vs %r"
                     % (imported.get("track_count"), state_after.get("track_count")))
    if state_before.get("track_count") == state_after.get("track_count") \
            and state_before.get("clips") == state_after.get("clips"):
        print("note: the imported file reproduced the session exactly, so the replace "
              "was a no-op for the track list (the views still had to be torn down)")
    return imported


def track_ids(state):
    return sorted(track.get("id") for track in state.get("tracks", []))


def check_import_then_undo(session, instance, problems, out_dir):
    """export -> import -> control.undo must ANSWER, and must not crash."""
    path = os.path.join(out_dir, "undo.dawproject")
    session.ok("dawproject.export", {"path": path, "overwrite": True})
    before = session.ok("arrangement.get_state")
    tracks_before = track_ids(before)
    session.ok("dawproject.import", {"path": path})
    reply = session.call("control.undo")
    if not alive_check(instance, problems, "at the undo of the import"):
        return
    if not reply.get("ok"):
        # An honest refusal is an acceptable answer for an undo that cannot be
        # taken (the brief allows it); a SILENT death is not, and that is what
        # this check exists for.
        error = reply.get("error") or {}
        if not error.get("kind") or not error.get("message"):
            problems.add("the import's undo refused without a typed reason: %r" % reply)
        return
    undone = H.ok_result(reply, reply.get("id"))
    if not undone.get("undone"):
        problems.add("control.undo reported undone=%r for the import" % undone.get("undone"))
    after = session.ok("arrangement.get_state")
    tracks_after = track_ids(after)
    if tracks_after != tracks_before:
        problems.add("the undo of the import did not put the session back: %r != %r"
                     % (tracks_after, tracks_before))


def check_project_open_variant(session, instance, problems, out_dir):
    """The third sighting: the same chain, entered through project.open."""
    project = os.path.join(out_dir, "opened.mmp")
    session.ok("project.save", {"path": project})
    session.ok("project.open", {"path": project})
    import_then_a_query(session, instance, problems, out_dir, "opened")
    session.ok("project.get_state")


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    transcript = H.Transcript()
    problems = H.Problems()
    out_dir = None
    with H.start_instance(argv[1], workingdir=None, extra_env=plugin_env(argv[1])) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        out_dir = instance.tmp
        session = Session(client, transcript)
        print("instance: %s" % argv[1])
        print("socket:   %s" % instance.socket_path)
        session.ok("control.version")
        check_import_then_query(session, instance, problems, out_dir)
        check_import_then_undo(session, instance, problems, out_dir)
        check_project_open_variant(session, instance, problems, out_dir)
        # The instance must also still be able to shut down cleanly: a crash
        # parked in the event loop would take this with it.
        if instance.alive():
            session.ok("control.quit", {"save": False})
            exited, code, waited = instance.wait_for_exit(30)
            if not exited or code != 0:
                problems.add("control.quit did not stop the instance cleanly: exited=%r "
                             "code=%r after %.1fs" % (exited, code, waited))
    transcript.dump()
    results = [("import lifetime (three chains)", not problems, problems.items)]
    return H.finish(results)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
