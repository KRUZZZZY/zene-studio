#!/usr/bin/env python3
"""The 0.5.0 L13 customisation surface, driven end to end over the control socket.

Starts the REAL `zene` binary headless (its own throwaway config) and proves:

  1. shortcuts.*: every menu action is listed with its keys; a key can be bound, a clash is
     refused unless forced (which moves the key), one control.undo restores, a set exports to a
     file and imports back, and reset gives the defaults back (WF-110/111);
  2. window.layout_*: a layout saves the view, applies again after a switch, is listed and
     removed, and one undo brings a removed layout back (WF-091); a save says what it keeps and,
     asked not to replace, refuses a name in use; the project remembers a layout of its own, saved
     with it and put back when it opens, forgotten in one undo step (design theme 22);
  3. window.detach / attach: a panel floats in its own window and comes back (WF-092);
  4. window.header_*: a header button hides, a command button is added, undo restores (WF-116);
  5. workflow.* (the recorded command macros, renamed for design theme 19): a workflow saves, runs
     (acknowledged, then polled) and adds what its steps add; a step naming no command or a workflow is
     refused (WF-114); a workflow is recorded from what is done, and the recording is reported while it
     lasts (ED-056); workflows are shared or kept with the project, as chosen, one undo step; a run that
     stops says which step failed and why, what applied and what did not run, and resumes - the step retried,
     or skipped and the rest run (design theme 19's step recovery, workflow.resume);
  6. arrangement.zoom / zoom_back / fit_tracks / set_tool and window.tool_menu (WF-094/095/108);
  7. session.pattern_list names the patterns a launcher cell can play (WF-054);
  8. the Lua sandbox refuses the customisation writers (they change the user's own config).

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-customisation.py <zene>
Exit 0 only when every check held.
"""

import json
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
    malformed_sets = [
        ("missing overrides", {"format": "zene-shortcuts", "version": 1}),
        ("non-object overrides", {"format": "zene-shortcuts", "version": 1, "overrides": []}),
        ("missing version", {"format": "zene-shortcuts", "overrides": {}}),
        ("non-integer version", {"format": "zene-shortcuts", "version": "1", "overrides": {}}),
        ("fractional version", {"format": "zene-shortcuts", "version": 1.5, "overrides": {}}),
    ]
    malformed_path = os.path.join(tmp, "malformed-shortcuts.json")
    for label, document in malformed_sets:
        session.result("shortcuts.set", {"id": first, "keys": ODD_KEYS})
        with open(malformed_path, "w", encoding="utf-8") as malformed_file:
            json.dump(document, malformed_file)
        rejected = session.call("shortcuts.import", {"file": malformed_path, "replace": True})
        recorder.check("%s shortcut set is rejected without clearing overrides" % label,
                       kind(rejected) == "invalid_args" and binding(session, first).get("keys") == ODD_KEYS,
                       "reply=%r keys=%r" % (rejected, binding(session, first).get("keys")))
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
    # Theme 22: a save says what it keeps, and the menu's Save (replace false) never overwrites silently.
    first = session.result("window.layout_save", {"name": "l13-keep"})
    kept = session.call("window.layout_save", {"name": "l13-keep", "replace": False})
    replaced = session.result("window.layout_save", {"name": "l13-keep"})
    recorder.check("a save lists what it keeps; asked not to replace, a name in use is refused; Replace replaces",
                   "view" in (first.get("captures") or []) and first.get("max_layouts") == 32
                   and kind(kept) == "refused" and replaced.get("replaced") is True,
                   "first=%r kept=%r replaced=%r" % (first, kept, replaced))
    session.result("window.layout_remove", {"name": "l13-keep"})


def view_of(session):
    return session.result("window.get_state").get("view")


def check_project_layout(session, recorder, tmp):
    none = session.call("window.layout_project_apply")
    session.result("window.set_view", {"view": "mix"})
    remembered = session.result("window.layout_project_save")
    unchanged = session.result("window.layout_project_save")
    session.result("window.set_view", {"view": "arrange"})
    applied = session.result("window.layout_project_apply")
    listed = session.result("window.layout_list")
    recorder.check("the project remembers a layout of its own and puts it back",
                   kind(none) == "not_found" and remembered.get("replaced") is False and unchanged.get("changed") is False
                   and applied.get("view") == "mix" and listed.get("project_layout") is True,
                   "none=%r remembered=%r unchanged=%r applied=%r listed=%r" % (none, remembered, unchanged, applied, listed))
    path = os.path.join(tmp, "remembers-a-layout.mmp")
    session.result("project.save", {"path": path})
    session.result("window.set_view", {"view": "arrange"})
    session.result("project.open", {"path": path})
    view = None
    for _ in range(50):
        view = view_of(session)
        if view == "mix":
            break
        time.sleep(0.1)
    recorder.check("a project that remembers a layout gets it back when it opens", view == "mix", "view=%r" % view)
    session.result("window.layout_project_clear")
    cleared = session.result("window.layout_list").get("project_layout")
    again = session.call("window.layout_project_clear")
    session.result("control.undo")
    restored = session.result("window.layout_list").get("project_layout")
    recorder.check("forgetting it is one undo step; forgetting none is not_found",
                   cleared is False and kind(again) == "not_found" and restored is True,
                   "cleared=%r again=%r restored=%r" % (cleared, again, restored))
    session.result("window.layout_project_clear")
    session.result("window.set_view", {"view": "arrange"})


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


def wait_run(session):
    status = {}
    for _ in range(50):
        status = session.result("workflow.status")
        if status.get("state") == "done":
            break
        time.sleep(0.1)
    return status


def check_workflows(session, recorder):
    before = track_count(session)
    steps = [{"command": "track.add", "args": {"type": "instrument", "name": "workflow one"}},
             {"command": "track.add", "args": {"type": "sample", "name": "workflow two"}}]
    saved = session.call("workflow.save", {"name": "two tracks", "steps": steps})
    ran = session.result("workflow.run", {"name": "two tracks"})
    status = wait_run(session)
    recorder.check("a workflow saves, runs and adds what its steps add",
                   saved.get("ok") and ran.get("queued") == 2 and status.get("stopped_at") == -1
                   and track_count(session) == before + 2, "save=%r run=%r status=%r" % (saved, ran, status))
    unknown = session.call("workflow.save", {"name": "bad", "steps": [{"command": "no.such_command"}]})
    nested = session.call("workflow.save", {"name": "nest", "steps": [{"command": "workflow.run", "args": {"name": "x"}}]})
    recorder.check("a step naming no command, or a workflow, is refused",
                   kind(unknown) == "invalid_args" and kind(nested) == "invalid_args", "unknown=%r nested=%r" % (unknown, nested))
    listed = [m.get("name") for m in session.result("workflow.list").get("workflows") or []]
    session.result("workflow.remove", {"name": "two tracks"})
    recorder.check("workflow.list lists it and workflow.remove forgets it",
                   "two tracks" in listed and not session.result("workflow.list").get("workflows"), "listed=%r" % listed)
    gone = session.call("macro.list")
    recorder.check("the old macro.* ids are gone (renamed workflow.*, never shipped)", kind(gone) == "not_found", "reply=%r" % gone)


def check_workflow_recording(session, recorder):
    # 0.5.0 ED-056: what is done while recording becomes the workflow's steps (not the undo in between).
    idle = session.result("workflow.status").get("recording") or {}
    started = session.result("workflow.record_start", {"name": "recorded"})
    session.result("track.add", {"type": "instrument", "name": "recorded one"})
    session.result("control.undo")
    session.result("track.add", {"type": "sample", "name": "recorded two"})
    during = session.result("workflow.status").get("recording") or {}
    # Theme 19: the recording state is visible while it lasts.
    recorder.check("the recording is reported while it lasts, with its name and steps",
                   idle.get("on") is False and started.get("recording") is True and during.get("on") is True
                   and during.get("name") == "recorded" and during.get("steps") == 2,
                   "idle=%r started=%r during=%r" % (idle, started, during))
    stopped = session.call("workflow.record_stop")
    recorded = [m for m in session.result("workflow.list").get("workflows") or [] if m.get("name") == "recorded"]
    steps = recorded[0].get("steps") if recorded else []
    empty = session.call("workflow.record_stop")
    after = session.result("workflow.status").get("recording") or {}
    recorder.check("a recorded workflow keeps what was done, not the undo",
                   stopped.get("ok") and len(steps) == 2 and all(s.get("command") == "track.add" for s in steps)
                   and kind(empty) == "refused" and after.get("on") is False,
                   "stop=%r steps=%r empty=%r after=%r" % (stopped, steps, empty, after))
    session.result("workflow.remove", {"name": "recorded"})


def check_workflow_recovery(session, recorder):
    # Design theme 19: step 2 removes a track that is not there, so the run stops at it.
    before = track_count(session)
    steps = [{"command": "track.add", "args": {"type": "sample", "name": "recover one"}},
             {"command": "track.remove", "args": {"track": "no-such-track"}},
             {"command": "track.add", "args": {"type": "sample", "name": "recover three"}}]
    session.result("workflow.save", {"name": "stops", "steps": steps})
    nothing = session.call("workflow.resume")
    session.result("workflow.run", {"name": "stops"})
    stopped = wait_run(session)
    failed = stopped.get("failed") or {}
    recorder.check("a stopped run says which step failed and why, what applied and what did not run",
                   stopped.get("stopped_at") == 1 and failed.get("step") == 2 and failed.get("command") == "track.remove"
                   and failed.get("error") and stopped.get("applied_steps") == [1] and stopped.get("not_run") == [3]
                   and stopped.get("resumable") is True and "stopped at step 2 of 3" in (stopped.get("message") or "")
                   and track_count(session) == before + 1, "status=%r" % stopped)
    retried = session.result("workflow.resume", {"skip": False})
    again = wait_run(session)
    skipped = session.result("workflow.resume", {"skip": True})
    done = wait_run(session)
    recorder.check("a stopped run resumes: the step retried, or skipped and the rest run",
                   retried.get("retried") == 2 and again.get("stopped_at") == 1 and skipped.get("skipped") == 2
                   and skipped.get("from") == 2 and done.get("stopped_at") == -1 and done.get("applied_steps") == [1, 3]
                   and done.get("skipped_steps") == [2] and done.get("resumable") is False
                   and track_count(session) == before + 2, "retried=%r again=%r skipped=%r done=%r" % (retried, again, skipped, done))
    finished = session.call("workflow.resume")
    recorder.check("nothing to resume is refused: before any stop, and after a run that finished",
                   kind(nothing) == "refused" and kind(finished) == "refused", "before=%r after=%r" % (nothing, finished))
    session.result("workflow.remove", {"name": "stops"})


def workflow_names(session):
    return [m.get("name") for m in session.result("workflow.list").get("workflows") or []]


def check_workflow_scope(session, recorder):
    step = [{"command": "track.add", "args": {"type": "sample", "name": "scoped"}}]
    shared = session.result("workflow.save", {"name": "everywhere", "steps": step})
    chosen = session.result("workflow.set_scope", {"scope": "project"})
    empty_here = workflow_names(session)
    kept = session.result("workflow.save", {"name": "this song", "steps": step})
    listed = session.result("workflow.list")
    recorder.check("kept with the project: a library of its own, the shared one untouched",
                   shared.get("scope") == "shared" and chosen.get("changed") is True and "everywhere" not in empty_here
                   and kept.get("scope") == "project" and listed.get("scope") == "project"
                   and workflow_names(session) == ["this song"],
                   "shared=%r chosen=%r here=%r kept=%r listed=%r" % (shared, chosen, empty_here, kept, listed))
    again = session.result("workflow.set_scope", {"scope": "project"})
    bad = session.call("workflow.set_scope", {"scope": "everywhere"})
    recorder.check("choosing the scope in use changes nothing; an unknown scope is refused",
                   again.get("changed") is False and kind(bad) == "invalid_args", "again=%r bad=%r" % (again, bad))
    session.result("control.undo")  # the project workflow's save
    session.result("control.undo")  # the scope
    back = session.result("workflow.list")
    recorder.check("one undo each: the save, then the scope back to shared with its library",
                   back.get("scope") == "shared" and "everywhere" in workflow_names(session), "back=%r" % back)
    session.result("workflow.remove", {"name": "everywhere"})


def check_arranger_view(session, recorder):
    first = session.result("arrangement.zoom", {"pixels_per_bar": 32})
    second = session.result("arrangement.zoom", {"pixels_per_bar": 96})
    third = session.result("arrangement.zoom", {"pixels_per_bar": 200})
    three = len({first.get("zoom"), second.get("zoom"), third.get("zoom")}) == 3
    back = session.result("arrangement.zoom_back")
    forward = session.result("arrangement.zoom_forward")
    back_again = session.result("arrangement.zoom_back")
    earlier = session.result("arrangement.zoom_back")
    to_flip = session.result("arrangement.zoom_toggle")
    flipped_back = session.result("arrangement.zoom_toggle")
    recorder.check("zoom back and forward walk three prior extents in order, and the toggle swaps with the extent "
                   "the zoom last left",
                   three and back.get("zoom") == second.get("zoom") and forward.get("zoom") == third.get("zoom")
                   and back_again.get("zoom") == second.get("zoom") and earlier.get("zoom") == first.get("zoom")
                   and to_flip.get("zoom") == second.get("zoom") and flipped_back.get("zoom") == first.get("zoom")
                   and back.get("forward") == 1 and forward.get("forward") == 0,
                   "first=%r second=%r third=%r back=%r forward=%r again=%r earlier=%r flip=%r back=%r"
                   % (first, second, third, back, forward, back_again, earlier, to_flip, flipped_back))
    drained = 0
    while kind(session.call("arrangement.zoom_back")) is None and drained < 64:  # the walk's cap is 32
        drained += 1
    recorder.check("the back walk drains within its bound", drained < 64, "drained=%d" % drained)
    settled = session.result("arrangement.zoom", {"pixels_per_bar": 160})
    left_behind = settled.get("previous_zoom")  # what the fresh zoom left: the toggle's swap target
    swapped = session.result("arrangement.zoom_toggle")
    back_after = session.call("arrangement.zoom_back")
    forward_after = session.result("arrangement.zoom_forward")
    recorder.check("after the toggle, Back is refused where it would only repeat the zoom, and Forward "
                   "walks the extent the swap left",
                   swapped.get("zoom") == left_behind and kind(back_after) == "not_found"
                   and forward_after.get("zoom") == settled.get("zoom"),
                   "settled=%r swapped=%r back=%r forward=%r" % (settled, swapped, back_after, forward_after))
    drained = 0
    while kind(session.call("arrangement.zoom_back")) is None and drained < 64:  # what they left behind
        drained += 1
    recorder.check("the walk drains again after the swap and the forward step", drained < 64,
                   "drained=%d" % drained)
    no_back = session.call("arrangement.zoom_back")
    fresh = session.call("arrangement.zoom", {"step": 3})
    no_forward = session.call("arrangement.zoom_forward")
    recorder.check("a zoom cut the forward walk off, and a drained back walk is refused",
                   fresh.get("ok") and kind(no_back) == "not_found" and kind(no_forward) == "not_found",
                   "no_back=%r fresh=%r no_forward=%r" % (no_back, fresh, no_forward))
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
              "local m, why, mk = zene.command('workflow.list')\n"
              "print('workflow ' .. tostring(mk))\n")
    ran = session.result("script.run", {"source": source})
    lines = ran.get("log") or ran.get("lines") or ran.get("output") or []
    text = "\n".join(lines) if isinstance(lines, list) else str(lines)
    recorder.check("scripts cannot write the user's customisations",
                   "shortcuts refused" in text and "workflow refused" in text, "log=%r" % ran)


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
        check_project_layout(session, recorder, instance.tmp)
        check_workflows(session, recorder)
        check_workflow_recording(session, recorder)
        check_workflow_recovery(session, recorder)
        check_workflow_scope(session, recorder)
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
