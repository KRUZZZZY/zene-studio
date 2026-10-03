#!/usr/bin/env python3
"""The 0.5.0 L13 customisation surface, driven end to end over the control socket.

Starts the REAL `zene` binary headless (its own throwaway config) and proves:

  1. shortcuts.*: every menu action is listed with its keys; a key can be bound, a clash is
     refused unless forced (which moves the key), one control.undo restores, a set exports to a
     file and imports back, and reset gives the defaults back (WF-110/111);
  2. window.layout_*: a layout saves the view, applies again after a switch, is listed and
     removed, and one undo brings a removed layout back (WF-091);
  3. window.detach / attach: a panel floats in its own window and comes back (WF-092);
  4. window.header_*: a header button hides, a command button is added, undo restores (WF-116);
  5. macro.*: a macro saves, runs (acknowledged, then polled) and adds what its steps add; a
     step naming no command or a macro is refused (WF-114); a macro is recorded from what is done (ED-056);
  6. arrangement.zoom / zoom_back / fit_tracks / set_tool and window.tool_menu (WF-094/095/108);
  7. session.pattern_list names the patterns a launcher cell can play (WF-054);
  8. the Lua sandbox refuses the customisation writers (they change the user's own config).

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-customisation.py <zene>
Exit 0 only when every check held.
"""

import os
import sys
import time

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results

ODD_KEYS = "Ctrl+Alt+Shift+F9"


def kind(reply):
    return (reply.get("error") or {}).get("kind")


def binding(session, action_id):
    for entry in session.result("shortcuts.list", {"query": action_id}).get("bindings") or []:
        if entry.get("id") == action_id:
            return entry
    return {}


def check_shortcuts(session, recorder, tmp):
    bindings = session.result("shortcuts.list").get("bindings") or []
    recorder.check("shortcuts.list lists the menu actions", len(bindings) > 20, "count=%d" % len(bindings))
    unbound = [b["id"] for b in bindings if not b.get("keys")]
    if len(unbound) < 2:
        recorder.check("at least two unbound actions to bind", False, "unbound=%r" % unbound)
        return
    first, second = unbound[0], unbound[1]
    set_reply = session.call("shortcuts.set", {"id": first, "keys": ODD_KEYS})
    recorder.check("shortcuts.set binds a key", set_reply.get("ok") and binding(session, first).get("keys") == ODD_KEYS,
                   "reply=%r" % set_reply)
    clash = session.call("shortcuts.set", {"id": second, "keys": ODD_KEYS})
    recorder.check("a clash is refused, typed", kind(clash) == "refused", "reply=%r" % clash)
    forced = session.call("shortcuts.set", {"id": second, "keys": ODD_KEYS, "force": True})
    recorder.check("force moves the key and unbinds the other",
                   forced.get("ok") and binding(session, second).get("keys") == ODD_KEYS
                   and not binding(session, first).get("keys"), "reply=%r" % forced)
    session.result("control.undo")
    recorder.check("one control.undo restores the bindings before the forced move",
                   binding(session, first).get("keys") == ODD_KEYS and not binding(session, second).get("keys"),
                   "first=%r second=%r" % (binding(session, first), binding(session, second)))
    path = os.path.join(tmp, "shortcuts.json")
    exported = session.call("shortcuts.export", {"file": path})
    session.result("shortcuts.reset")
    reset_ok = not binding(session, first).get("keys")
    imported = session.call("shortcuts.import", {"file": path})
    recorder.check("export, reset and import round-trip the set",
                   exported.get("ok") and os.path.exists(path) and reset_ok and imported.get("ok")
                   and binding(session, first).get("keys") == ODD_KEYS, "export=%r import=%r" % (exported, imported))
    bad = session.call("shortcuts.set", {"id": "No ▸ Such action", "keys": "Ctrl+1"})
    recorder.check("an unknown action is not_found", kind(bad) == "not_found", "reply=%r" % bad)
    session.result("shortcuts.reset")


def check_layouts(session, recorder):
    session.result("window.set_view", {"view": "mix"})
    saved = session.call("window.layout_save", {"name": "l13-mix"})
    session.result("window.set_view", {"view": "arrange"})
    applied = session.result("window.layout_apply", {"name": "l13-mix"})
    listed = session.result("window.layout_list").get("layouts") or []
    recorder.check("a saved layout applies again and is listed",
                   saved.get("ok") and applied.get("view") == "mix" and "l13-mix" in listed,
                   "saved=%r applied=%r listed=%r" % (saved, applied.get("view"), listed))
    session.result("window.layout_remove", {"name": "l13-mix"})
    gone = "l13-mix" not in (session.result("window.layout_list").get("layouts") or [])
    session.result("control.undo")
    back = "l13-mix" in (session.result("window.layout_list").get("layouts") or [])
    recorder.check("removing a layout is one undo step", gone and back, "gone=%r back=%r" % (gone, back))
    missing = session.call("window.layout_apply", {"name": "no-such-layout"})
    recorder.check("an unknown layout is not_found", kind(missing) == "not_found", "reply=%r" % missing)


def floating(session, panel_id):
    for entry in session.result("window.get_state").get("panels") or []:
        if entry.get("id") == panel_id:
            return entry.get("floating")
    return None


def check_floating(session, recorder):
    detached = session.result("window.detach", {"panel": "browser"})
    out = floating(session, "browser")
    attached = session.result("window.attach", {"panel": "browser"})
    recorder.check("a panel floats in its own window and comes back",
                   detached.get("floating") is True and out is True and attached.get("floating") is False
                   and floating(session, "browser") is False, "detach=%r attach=%r" % (detached, attached))
    unknown = session.call("window.detach", {"panel": "nope"})
    recorder.check("an unknown panel is not_found", kind(unknown) == "not_found", "reply=%r" % unknown)


def header_button(session, name):
    for entry in session.result("window.header_get").get("buttons") or []:
        if entry.get("name") == name:
            return entry
    return {}


def check_header(session, recorder):
    names = [b.get("name") for b in session.result("window.header_get").get("buttons") or []]
    recorder.check("window.header_get lists the header's buttons", "metronome" in names, "names=%r" % names)
    session.result("window.header_set", {"hidden": ["metronome"], "extra": [{"command": "transport.stop", "label": "Halt"}]})
    hidden = header_button(session, "metronome").get("visible") is False
    extra = header_button(session, "extra_transport.stop").get("command") == "transport.stop"
    session.result("control.undo")
    restored = header_button(session, "metronome").get("visible") is True and not header_button(session, "extra_transport.stop")
    recorder.check("a header button hides, a command button is added, one undo restores both",
                   hidden and extra and restored, "hidden=%r extra=%r restored=%r" % (hidden, extra, restored))
    bad = session.call("window.header_set", {"extra": [{"command": "no.such_command"}]})
    recorder.check("a header button for no command is not_found", kind(bad) == "not_found", "reply=%r" % bad)
    # WF-116: the header menu's Remove Command Button sends the list without that button.
    two = [{"command": "transport.stop", "label": "Halt"}, {"command": "transport.play", "label": "Go"}]
    session.result("window.header_set", {"extra": two})
    session.result("window.header_set", {"extra": two[1:]})
    removed = not header_button(session, "extra_transport.stop") and bool(header_button(session, "extra_transport.play"))
    recorder.check("an added header button is removed again", removed,
                   "buttons=%r" % session.result("window.header_get").get("buttons"))
    session.result("window.header_set", {"extra": []})


def track_count(session):
    return session.result("track.list").get("count") or 0


def check_macros(session, recorder):
    before = track_count(session)
    steps = [{"command": "track.add", "args": {"type": "instrument", "name": "macro one"}},
             {"command": "track.add", "args": {"type": "sample", "name": "macro two"}}]
    saved = session.call("macro.save", {"name": "two tracks", "steps": steps})
    ran = session.result("macro.run", {"name": "two tracks"})
    status = {}
    for _ in range(50):
        status = session.result("macro.status")
        if status.get("state") == "done":
            break
        time.sleep(0.1)
    recorder.check("a macro saves, runs and adds what its steps add",
                   saved.get("ok") and ran.get("queued") == 2 and status.get("stopped_at") == -1
                   and track_count(session) == before + 2, "save=%r run=%r status=%r" % (saved, ran, status))
    unknown = session.call("macro.save", {"name": "bad", "steps": [{"command": "no.such_command"}]})
    nested = session.call("macro.save", {"name": "nest", "steps": [{"command": "macro.run", "args": {"name": "x"}}]})
    recorder.check("a step naming no command, or a macro, is refused",
                   kind(unknown) == "invalid_args" and kind(nested) == "invalid_args", "unknown=%r nested=%r" % (unknown, nested))
    listed = [m.get("name") for m in session.result("macro.list").get("macros") or []]
    session.result("macro.remove", {"name": "two tracks"})
    recorder.check("macro.list lists it and macro.remove forgets it",
                   "two tracks" in listed and not session.result("macro.list").get("macros"), "listed=%r" % listed)
    # 0.5.0 ED-056: what is done while recording becomes the macro's steps (not the undo in between).
    session.result("macro.record_start", {"name": "recorded"})
    session.result("track.add", {"type": "instrument", "name": "recorded one"})
    session.result("control.undo")
    session.result("track.add", {"type": "sample", "name": "recorded two"})
    stopped = session.call("macro.record_stop")
    recorded = [m for m in session.result("macro.list").get("macros") or [] if m.get("name") == "recorded"]
    steps = recorded[0].get("steps") if recorded else []
    empty = session.call("macro.record_stop")
    recorder.check("a recorded macro keeps what was done, not the undo",
                   stopped.get("ok") and len(steps) == 2 and all(s.get("command") == "track.add" for s in steps)
                   and kind(empty) == "refused", "stop=%r steps=%r empty=%r" % (stopped, steps, empty))
    session.result("macro.remove", {"name": "recorded"})


def check_arranger_view(session, recorder):
    zoomed = session.result("arrangement.zoom", {"pixels_per_bar": 64})
    stepped = session.result("arrangement.zoom", {"step": 6})
    back = session.result("arrangement.zoom_back")
    again = session.result("arrangement.zoom_back")
    recorder.check("zoom_back goes back, and twice toggles",
                   back.get("zoom") == zoomed.get("zoom") and again.get("zoom") == stepped.get("zoom"),
                   "zoomed=%r stepped=%r back=%r again=%r" % (zoomed, stepped, back, again))
    fit = session.call("arrangement.zoom", {"fit": "song"})
    recorder.check("fit song answers a zoom", fit.get("ok") and (fit.get("result") or {}).get("pixels_per_bar", 0) > 0,
                   "reply=%r" % fit)
    tracks = session.call("arrangement.fit_tracks")
    heights = (tracks.get("result") or {}).get("heights") or []
    recorder.check("fit_tracks gives every track one height",
                   tracks.get("ok") and len(heights) > 0 and len(set(heights)) == 1, "reply=%r" % tracks)
    session.result("control.undo")
    tool = session.result("arrangement.set_tool", {"tool": "select"})
    menu = session.result("window.tool_menu")
    # The arranger's seven tools and the note editor's seven (paint and mute included since the 0.5.0 review).
    recorder.check("set_tool and the tool menu",
                   tool.get("tool") == "select" and menu.get("shown") is False and len(menu.get("entries") or []) == 14,
                   "tool=%r menu=%r" % (tool, menu))
    tools = [session.result("arrangement.set_tool", {"tool": t}).get("tool") for t in ("audition", "zoom", "smart", "range")]
    recorder.check("the overlay's tools are chosen like the editor's (ED-008/009/010/011)",
                   tools == ["audition", "zoom", "smart", "range"], "tools=%r" % tools)
    zoomed_span = session.result("arrangement.zoom", {"start": 0, "end": 768})
    recorder.check("the zoom tool's span fills the width", zoomed_span.get("pixels_per_bar", 0) > 0, "reply=%r" % zoomed_span)
    session.result("arrangement.set_tool", {"tool": "draw"})


def check_nudge_and_navigation(session, recorder):
    track = session.result("track.add", {"type": "instrument", "name": "nudge"}).get("track")
    first = session.result("clip.add", {"track": track, "position": 0}).get("clip")
    second = session.result("clip.add", {"track": track, "position": 768}).get("clip")
    session.result("window.select", {"clip": first})
    nudged = session.result("arrangement.nudge", {"direction": "right", "amount": 48})
    selected = session.result("arrangement.select_clip", {"direction": "next"})
    state = session.result("window.select")
    recorder.check("nudge moves the selected clip, next clip selects the one after it (ED-024)",
                   nudged.get("position") == 48 and state.get("clip") == second,
                   "nudge=%r select=%r state=%r" % (nudged, selected, state))


def check_patterns(session, recorder):
    listed = session.call("session.pattern_list")
    recorder.check("session.pattern_list answers the patterns",
                   listed.get("ok") and isinstance((listed.get("result") or {}).get("patterns"), list), "reply=%r" % listed)


def check_lua_refuses(session, recorder):
    source = ("local r, err, kind = zene.command('shortcuts.reset')\n"
              "print('shortcuts ' .. tostring(kind))\n"
              "local m, why, mk = zene.command('macro.list')\n"
              "print('macro ' .. tostring(mk))\n")
    ran = session.result("script.run", {"source": source})
    lines = ran.get("log") or ran.get("lines") or ran.get("output") or []
    text = "\n".join(lines) if isinstance(lines, list) else str(lines)
    recorder.check("scripts cannot write the user's customisations",
                   "shortcuts refused" in text and "macro refused" in text, "log=%r" % ran)


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
        check_shortcuts(session, recorder, instance.tmp)
        check_layouts(session, recorder)
        check_floating(session, recorder)
        check_header(session, recorder)
        check_macros(session, recorder)
        check_arranger_view(session, recorder)
        check_patterns(session, recorder)
        check_nudge_and_navigation(session, recorder)
        check_lua_refuses(session, recorder)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("customisation control-surface driver")
        return 1
    H.ok("customisation control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
