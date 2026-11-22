#!/usr/bin/env python3
"""Q050-055.1 stage 1: panel-scoped shortcuts survive a real RESTART, across two processes.

The GUI test (tests/src/gui/PanelShortcutsTest.cpp) proves the criteria inside one process. This
transcript is the restart leg the criteria ask for in the strongest form: the SAME `zene` binary in
TWO real processes, the second started against the config file the first one wrote, so "survives
restart" is measured, not argued.

  1. In instance A: bind one key (Ctrl+Alt+J) to a different action in each of the four contexts -
     arranger, note editor, automation editor, Perform - and one same-context clash in the note
     editor, resolved through shortcuts.set's force (the same path the shortcuts page's form takes).
  2. Instance A exits normally, so its config file is the one a user would have on disk.
  3. Instance B starts against THAT config file and reports the bindings from its own registry:
     the same four contexts keep the same key, the resolved clash is still resolved and the loser
     still unbound, the OTHER contexts' bindings of that key are untouched, and the same-scope
     conflict count is zero. A fresh write in B still refuses another same-context clash, so the
     scoping rule survived the restart too.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-panel-shortcuts.py <zene>
Exit 0 only when every check held.
"""

import json
import os
import sys

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results

SHARED = "Ctrl+Alt+J"
CLASH = "Ctrl+Alt+K"

# scope -> (action id, key) bound before the restart, in the order the checks read them.
BINDINGS = {
    "arranger": ("Arranger \u25b8 Knife Tool", SHARED),
    "note_editor": ("Note Editor \u25b8 Erase Tool", SHARED),
    "automation_editor": ("Automation Editor \u25b8 Erase Mode", SHARED),
    "perform": ("Perform \u25b8 Stop All", SHARED),
}
# The clash's two sides, in the note editor only.
CLASH_WINNER = "Note Editor \u25b8 Audition Tool"
CLASH_LOSER = "Note Editor \u25b8 Mute Tool"
# The same key held by ANOTHER context: the resolution must not touch it.
ARRANGER_OTHER = "Arranger \u25b8 Select Tool"


def kind(reply):
    return (reply.get("error") or {}).get("kind")


def rows(session, scope=None):
    args = {"scope": scope} if scope else {}
    return session.result("shortcuts.list", args).get("bindings") or []


def binding(session, scope, action_id):
    for entry in rows(session, scope):
        if entry.get("id") == action_id:
            return entry
    return {}


def bind(session, recorder, scope, action_id, keys, force=False):
    args = {"id": action_id, "scope": scope, "keys": keys}
    if force:
        args["force"] = True
    reply = session.call("shortcuts.set", args)
    recorder.check("instance A binds %s in %s" % (action_id, scope),
                   reply.get("ok") is True and binding(session, scope, action_id).get("keys") == keys,
                   "reply=%r" % reply)
    return reply


def write_the_bindings(session, recorder):
    for scope, (action_id, keys) in BINDINGS.items():
        bind(session, recorder, scope, action_id, keys)
    # The same key in a second context, then the same-context clash and its resolution.
    bind(session, recorder, "arranger", ARRANGER_OTHER, CLASH)
    bind(session, recorder, "note_editor", CLASH_LOSER, CLASH)
    refused = session.call("shortcuts.set", {"id": CLASH_WINNER, "scope": "note_editor", "keys": CLASH})
    recorder.check("a same-context clash is refused, typed, in instance A",
                   kind(refused) == "refused", "reply=%r" % refused)
    bind(session, recorder, "note_editor", CLASH_WINNER, CLASH, force=True)
    recorder.check("force moved the key and unbound the other in its own context",
                   not binding(session, "note_editor", CLASH_LOSER).get("keys"),
                   "loser=%r" % binding(session, "note_editor", CLASH_LOSER))
    listed = session.result("shortcuts.list")
    recorder.check("no same-scope conflict remains, and the key lives in two contexts",
                   listed.get("conflict_count") == 0
                   and binding(session, "note_editor", CLASH_WINNER).get("keys") == CLASH
                   and binding(session, "arranger", ARRANGER_OTHER).get("keys") == CLASH,
                   "conflict_count=%r" % listed.get("conflict_count"))


def read_them_back(session, recorder):
    """Instance B: everything the first process saved is still there, in its own context."""
    listed = session.result("shortcuts.list")
    scopes = listed.get("scopes") or []
    recorder.check("the restarted instance offers all four panel scopes",
                   scopes == ["arranger", "note_editor", "automation_editor", "perform"], "scopes=%r" % scopes)
    for scope, (action_id, keys) in BINDINGS.items():
        entry = binding(session, scope, action_id)
        recorder.check("%s kept %s after the restart" % (action_id, keys),
                       entry.get("keys") == keys and entry.get("scope") == scope, "entry=%r" % entry)
    winner = binding(session, "note_editor", CLASH_WINNER)
    loser = binding(session, "note_editor", CLASH_LOSER)
    recorder.check("the resolved clash survived: the winner holds the key, the loser stays unbound",
                   winner.get("keys") == CLASH and not loser.get("keys"),
                   "winner=%r loser=%r" % (winner, loser))
    other = binding(session, "arranger", ARRANGER_OTHER)
    recorder.check("the resolution did not change another context's binding of the same key",
                   other.get("keys") == CLASH, "arranger=%r" % other)
    recorder.check("the restarted instance reports no same-scope conflict",
                   listed.get("conflict_count") == 0, "conflict_count=%r" % listed.get("conflict_count"))
    # The RULE survived too, not only the data: another same-context clash is still refused.
    again = session.call("shortcuts.set", {"id": "Note Editor \u25b8 Draw Tool", "scope": "note_editor",
                                           "keys": CLASH})
    recorder.check("a new same-context clash is still refused after the restart",
                   kind(again) == "refused", "reply=%r" % again)
    # ... while the same key in a THIRD context is still fine, which is the point of a scoped map.
    fresh = session.call("shortcuts.set", {"id": "Automation Editor \u25b8 Draw Mode",
                                           "scope": "automation_editor", "keys": CLASH})
    recorder.check("a third context may take the same key after the restart",
                   fresh.get("ok") is True, "reply=%r" % fresh)


def config_carries_the_panels(text, recorder):
    """The file on disk is what makes the restart leg meaningful: read it as text."""
    recorder.check("the panel map is written to the config file the next start reads",
                   "panels" in text and "note_editor" in text, "config=%r" % text[-400:])


def start_against(binary, config_text):
    """A second instance started from the config the FIRST one saved.

    The harness gives every instance its own temp directory and removes it on close, so the saved
    file is copied into the second instance's own world first - the config BYTES are the first
    run's, which is what "the next start reads what the last one wrote" means.
    """
    instance = H.Instance(binary, workingdir=None)
    with open(instance.config_path, "w", encoding="utf-8") as handle:
        handle.write(config_text)
    instance.spawn()
    return instance


def run_session(binary, recorder):
    with H.start_instance(binary, workingdir=None) as first:
        H.wait_for_socket(first)
        client = H.connect(first)
        H.wait_ready(first, client, H.Transcript())
        session = Session(client, H.Transcript())
        write_the_bindings(session, recorder)
        # A normal quit saves the config; the process is gone before the second one reads it.
        session.call("control.quit")
        exited, code, _ = first.wait_for_exit(60.0)
        recorder.check("instance A exited cleanly, saving its config", exited and code == 0,
                       "exited=%r code=%r" % (exited, code))
        with open(first.config_path, encoding="utf-8") as handle:
            saved = handle.read()
        config_carries_the_panels(saved, recorder)

    with start_against(binary, saved) as second:
        H.wait_for_socket(second)
        client = H.connect(second)
        transcript = H.Transcript()
        H.wait_ready(second, client, transcript)
        session = Session(client, transcript)
        print("SECOND-LOG", second.read_log()[-3000:])
        read_them_back(session, recorder)
        session.call("control.quit")
        second.wait_for_exit(60.0)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    run_session(argv[1], recorder)
    report_results(recorder)
    if recorder.problems:
        recorder.problems.report("panel-scoped shortcuts restart driver")
        return 1
    H.ok("panel-scoped shortcuts restart driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
