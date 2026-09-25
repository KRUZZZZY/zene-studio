#!/usr/bin/env python3
"""BUG-SAVE-01: `project.save` must record the session state a successful save
records, so the state the reply claims and the state the session reports agree.

Reproduced 2026-09-25: a save replied `saved: true` while `project.get_state`
went on reporting `modified: true` and an EMPTY `file`, and a subsequent
`control.quit {save: true}` then refused ("needs a project file"). The bare
`project.save {}` refused too, for the same reason: it resolves its target from
the project's own file name, which the save had never recorded.

The cause is a divergence between the two save paths: the GUI's save
(Song::guiSaveProjectAs) writes the file and then clears the flags, while the
control verb called Song::saveProjectFile() - which writes the DOCUMENT and
nothing else - and skipped the second half. The fix gives both paths one
implementation (Song::noteProjectSaved) and calls it from the control verb.

This test drives the REAL binary over --control-socket: it saves to a path,
asserts file+modified, edits, saves through the BARE form, asserts file+modified
again, and then requires `control.quit {save: true}` to answer and the instance
to reach exit 0. Red before the fix: `project.get_state` still says
modified:true with an empty file, the bare save refuses invalid_args ("no 'path'
given and the session has no project file yet") and the quit refuses.

Usage: QT_QPA_PLATFORM=offscreen python3 control-save-state.py <zene>
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402


class Session:
    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 200

    def call(self, command, args=None):
        self.last_id += 1
        return self.client.call(self.last_id, command, args or {}, transcript=self.transcript)

    def ok(self, command, args=None):
        return H.ok_result(self.call(command, args), self.last_id)


def plugin_env(binary):
    plugin_dir = os.path.join(os.path.dirname(os.path.abspath(binary)), "plugins")
    return {"LMMS_PLUGIN_DIR": plugin_dir} if os.path.isdir(plugin_dir) else None


def check_the_session_state(session, expected_file, problems, when):
    state = session.ok("project.get_state")
    if state.get("file") != expected_file:
        problems.add("%s: the session has file=%r, not %r"
                     % (when, state.get("file"), expected_file))
    if state.get("modified") is not False:
        problems.add("%s: the session is still modified: %r" % (when, state))


def edit_the_session(session):
    """A real edit, so 'modified' has to come back and the next save has work."""
    track = session.ok("track.add", {"name": "Save QA", "type": "instrument"})["track"]
    session.ok("track.rename", {"track": track, "name": "Save QA renamed"})
    return track


def check_the_path_save_records_it(session, path, problems):
    saved = session.ok("project.save", {"path": path})
    if saved.get("saved") is not True:
        problems.add("project.save did not report saved=true: %r" % saved)
    check_the_session_state(session, path, problems, "after project.save {path}")


def check_the_bare_save_records_it(session, path, problems):
    if not os.path.isfile(path):
        problems.add("the first save reported success but %s is not a file" % path)
        return
    bare = session.ok("project.save")
    if bare.get("saved") is not True:
        problems.add("the bare project.save did not save: %r" % bare)
    elif bare.get("file") != path:
        problems.add("the bare save wrote %r, not the file the session names (%r)"
                     % (bare.get("file"), path))
    check_the_session_state(session, path, problems, "after the bare project.save")


def check_quit_with_save(session, instance, problems):
    quoted = session.call("control.quit", {"save": True})
    if not quoted.get("ok"):
        problems.add("control.quit {save:true} refused after a successful save: %r" % quoted)
        return
    result = H.ok_result(quoted, session.last_id)
    if result.get("save_requested") is not True:
        problems.add("the quit did not report the save it was asked for: %r" % result)
    exited, code, waited = instance.wait_for_exit(60)
    if not exited or code != 0:
        problems.add("the instance did not stop cleanly after quitting with a save: "
                     "exited=%r code=%r after %.1fs" % (exited, code, waited))


def check_the_instance(binary, problems):
    transcript = H.Transcript()
    path = None
    with H.start_instance(binary, workingdir=None, extra_env=plugin_env(binary)) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        print("instance: %s" % binary)
        session = Session(client, transcript)
        session.ok("control.version")
        path = os.path.join(instance.tmp, "save-state.mmp")
        check_the_path_save_records_it(session, path, problems)
        edit_the_session(session)
        if session.ok("project.get_state").get("modified") is not True:
            problems.add("an edit after the save left the session unmodified, so the check "
                         "would be vacuous")
        check_the_bare_save_records_it(session, path, problems)
        edit_the_session(session)
        check_quit_with_save(session, instance, problems)
    transcript.dump()


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    problems = H.Problems()
    check_the_instance(argv[1], problems)
    return H.finish([("project.save records the session state", not problems, problems.items)])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
