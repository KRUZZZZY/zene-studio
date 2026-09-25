#!/usr/bin/env python3
"""BUG-DRYRUN-UNDO: a `dry_run` preview must leave the journal EXACTLY as it
found it, so the undo that follows it reaches the last REAL edit.

Reproduced 2026-09-25 (`track.add` -> `clip.add` -> `clip.move {position: 96}` ->
`clip.delete {dry_run: true}` -> `control.undo`): the preview correctly changed
nothing, and then `control.undo` answered `irreversible: cannot undo
'clip.delete' ... [dry_run preview: nothing was changed]` while the `clip.move`
the caller meant to take back stayed in place.

The cause was that the preview's handler still described a `__transaction` and
the registry recorded it: the record sat on TOP of the last real step, and
control.undo refuses the top record rather than silently undoing something older
(that refusal is the contract - the bug was that a no-op was recorded as a step
at all). The fix is at the one seam every dry run shares:
ControlRegistry::runHandler keys on the result's own `dry_run` field and records
nothing for a preview, so neither the transaction list nor the document's
provenance learns about a call that changed nothing.

This test drives the REAL binary over --control-socket and asserts all three
facts: the preview reports no change, the transaction list is unchanged by it,
and the undo that follows takes back the clip.move (position 96 -> 0). Red
before the fix: the undo reply is `irreversible` and the position stays 96.

Usage: QT_QPA_PLATFORM=offscreen python3 control-dry-run-journal.py <zene>
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402


class Session:
    """One request id at a time, with every reply kept in the transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 100

    def call(self, command, args=None):
        self.last_id += 1
        return self.client.call(self.last_id, command, args or {}, transcript=self.transcript)

    def ok(self, command, args=None):
        return H.ok_result(self.call(command, args), self.last_id)


def plugin_env(binary):
    plugin_dir = os.path.join(os.path.dirname(os.path.abspath(binary)), "plugins")
    return {"LMMS_PLUGIN_DIR": plugin_dir} if os.path.isdir(plugin_dir) else None


def recorded_commands(session):
    report = session.ok("control.transactions")
    return [entry.get("command") for entry in report.get("transactions", [])]


def clip_positions(session, clip):
    arrangement = session.ok("arrangement.get_state")
    return [c.get("position") for c in arrangement.get("clips", []) if c.get("id") == clip]


def move_a_clip_and_preview_its_deletion(session):
    """The five reported calls up to and including the preview."""
    track = session.ok("track.add", {"name": "Dry-run QA", "type": "instrument"})["track"]
    clip = session.ok("clip.add", {"track": track, "position": 0, "length": 192})["clip"]
    session.ok("clip.move", {"clip": clip, "position": 96})
    before = recorded_commands(session)
    preview = session.ok("clip.delete", {"clip": clip, "dry_run": True})
    return clip, before, preview


def check_the_preview_changed_nothing(session, clip, preview, problems):
    if preview.get("dry_run") is not True:
        problems.add("clip.delete did not report a dry run: %r" % preview)
    if preview.get("deleted") != clip:
        problems.add("the preview named %r, not the clip %r" % (preview.get("deleted"), clip))
    positions = clip_positions(session, clip)
    if positions != [96]:
        problems.add("the dry run CHANGED the session: clip %s is at %r" % (clip, positions))


def check_the_journal_is_untouched(session, before, problems):
    after = recorded_commands(session)
    if after != before:
        problems.add("the dry run wrote a journal record: %r -> %r" % (before, after))


def check_the_undo_reaches_the_move(session, clip, problems):
    undo = session.ok("control.undo")
    if not undo.get("undone") or undo.get("undone_command") != "clip.move":
        problems.add("control.undo after a dry run undid %r, not the clip.move"
                     % (undo.get("undone_command"),))
    positions = clip_positions(session, clip)
    if positions != [0]:
        problems.add("the undo of clip.move did not restore the position: %r" % positions)


def check_the_instance(binary, problems):
    transcript = H.Transcript()
    with H.start_instance(binary, workingdir=None, extra_env=plugin_env(binary)) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        print("instance: %s" % binary)
        session = Session(client, transcript)
        session.ok("control.version")
        clip, before, preview = move_a_clip_and_preview_its_deletion(session)
        check_the_preview_changed_nothing(session, clip, preview, problems)
        check_the_journal_is_untouched(session, before, problems)
        check_the_undo_reaches_the_move(session, clip, problems)
        if instance.alive():
            session.ok("control.quit", {"save": False})
    transcript.dump()


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    problems = H.Problems()
    check_the_instance(argv[1], problems)
    return H.finish([("dry_run leaves the journal alone", not problems, problems.items)])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
