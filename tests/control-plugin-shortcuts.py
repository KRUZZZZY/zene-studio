#!/usr/bin/env python3
"""Q050-055.2 stage 2: the keyboard's plug-in actions from the agent's side, across a real restart.

The GUI test (tests/src/gui/PluginShortcutsTest.cpp) presses the keys. This transcript proves the same registered
commands over the socket in a real process: shortcuts.bind_plugin binds a chosen plug-in, plugin.selected_load loads it
onto window.select's track only and reverses through control.undo, an incompatible target is refused typed, and the
binding is read back by a SECOND process started from the config the first one saved.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-plugin-shortcuts.py <zene>
Exit 0 only when every check held.
"""

import sys

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results

KEY = "Ctrl+Alt+A"


def kind(reply):
    return (reply.get("error") or {}).get("kind")


def device_named(session, name):
    for entry in session.result("plugin.list", {"loadable_only": True}).get("devices") or []:
        if entry.get("name") == name:
            return entry.get("id")
    return None



def chain_size(session, track):
    state = session.result("dsp.get_state", {"target": track})
    return sum(chain.get("count", 0) for chain in state.get("chains") or [])


def bound_row(session):
    for entry in session.result("shortcuts.list", {"scope": "arranger"}).get("bindings") or []:
        if entry.get("command") == "plugin.selected_load":
            return entry
    return {}


def first_process(session, recorder):
    amplifier = device_named(session, "amplifier")
    recorder.check("the catalogue has the amplifier", amplifier is not None, "amplifier=%r" % amplifier)
    first = session.result("track.add", {"type": "instrument", "name": "First"}).get("track")
    second = session.result("track.add", {"type": "instrument", "name": "Second"}).get("track")
    automation = session.result("track.add", {"type": "automation", "name": "Auto"}).get("track")

    nothing = session.call("plugin.selected_load", {"device": amplifier})
    recorder.check("with nothing selected the load is refused, typed", kind(nothing) == "refused", "reply=%r" % nothing)

    bound = session.call("shortcuts.bind_plugin", {"scope": "arranger", "device": amplifier, "keys": KEY})
    recorder.check("shortcuts.bind_plugin binds the chosen plug-in", bound.get("ok") is True, "reply=%r" % bound)
    recorder.check("the binding is listed with its key and command", bound_row(session).get("keys") == KEY,
                   "row=%r" % bound_row(session))

    session.call("window.select", {"track": first})
    before = (chain_size(session, first), chain_size(session, second))
    loaded = session.call("plugin.selected_load", {"device": amplifier})
    result = loaded.get("result") or {}
    recorder.check("the load answers as plugin.load does, on the selected track",
                   loaded.get("ok") is True and result.get("target") == first and result.get("kind") == "effect"
                   and result.get("plugin") == "amplifier", "reply=%r" % loaded)
    recorder.check("only the selected track gained the device",
                   (chain_size(session, first), chain_size(session, second)) == (before[0] + 1, before[1]),
                   "before=%r first=%r second=%r" % (before, chain_size(session, first), chain_size(session, second)))
    undone = session.call("control.undo")
    recorder.check("one control.undo takes the load back", undone.get("ok") is True and chain_size(session, first) == before[0],
                   "reply=%r" % undone)

    session.call("window.select", {"track": automation})
    refused = session.call("plugin.selected_load", {"device": amplifier})
    recorder.check("an automation track refuses, typed", refused.get("ok") is False and kind(refused) in ("refused", "invalid_args"),
                   "reply=%r" % refused)
    # This instance has no display: the editor action says so, typed, as plugin.editor_open does (the GUI test opens it).
    session.call("window.select", {"track": second})
    opened = session.call("plugin.selected_editor_open")
    recorder.check("the editor action needs a display and says so", kind(opened) == "requires", "reply=%r" % opened)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    with H.start_instance(argv[1], workingdir=None) as first:
        H.wait_for_socket(first)
        client = H.connect(first)
        transcript = H.Transcript()
        H.wait_ready(first, client, transcript)
        session = Session(client, transcript)
        first_process(session, recorder)
        session.call("control.quit")
        exited, code, _ = first.wait_for_exit(60.0)
        recorder.check("instance A exited cleanly, saving its config", exited and code == 0, "exited=%r code=%r" % (exited, code))
        with open(first.config_path, encoding="utf-8") as handle:
            saved = handle.read()

    second = H.Instance(argv[1], workingdir=None)
    with open(second.config_path, "w", encoding="utf-8") as handle:
        handle.write(saved)
    second.spawn()
    with second:
        H.wait_for_socket(second)
        client = H.connect(second)
        transcript = H.Transcript()
        H.wait_ready(second, client, transcript)
        session = Session(client, transcript)
        recorder.check("the restarted instance keeps the plug-in's key", bound_row(session).get("keys") == KEY,
                       "row=%r" % bound_row(session))
        track = session.result("track.add", {"type": "instrument", "name": "After"}).get("track")
        session.call("window.select", {"track": track})
        before = chain_size(session, track)
        device = bound_row(session).get("id", "").split(" ▸ ")[-1]
        loaded = session.call("plugin.selected_load", {"device": device})
        recorder.check("the stored key still names the device after the restart",
                       loaded.get("ok") is True and chain_size(session, track) == before + 1, "reply=%r" % loaded)
        session.call("control.quit")
        second.wait_for_exit(60.0)

    report_results(recorder)
    if recorder.problems:
        recorder.problems.report("plug-in shortcuts driver")
        return 1
    H.ok("plug-in shortcuts driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
