#!/usr/bin/env python3
"""The 0.5.0 single-window shell, driven end to end over the control socket.

The shell (docs/UI-SHELL.md) is a product surface an agent must be able to drive, so this
driver starts the REAL `zene` binary headless and proves, through `window.*` alone:

  1. `window.get_state` reports the view, every panel (id, region, visible, available,
     mounted) and every window.toggle name - and every panel has an editor mounted;
  2. `window.set_view` switches arrange / mix / edit, says whether it changed anything,
     puts each view's own main panel in the centre, and refuses an unknown view, typed;
  3. `window.toggle` on a panel obeys the region rule (a region shows one panel), cannot
     hide a view's main panel, and REVEALS a panel the view lacks by switching view;
  4. the 0.4.0 editor names still work and resolve to the panel each editor became;
  5. a floating window (project notes) opens and closes through the same verb;
  6. `window.screenshot` renders every view at the working size, and the three views'
     images differ - an agent can SEE the layout it changed.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-shell.py <zene> [<screenshot dir>]
With a screenshot directory the PNGs are kept there (docs/ui-logs/ holds a reviewed set).
Exit 0 only when every check held.
"""

import hashlib
import os
import sys

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results

PANELS = ("arranger", "launcher", "mixer", "detail", "devices", "automation", "pattern",
          "inspector", "browser")
CENTRES = {"arrange": "arranger", "mix": "mixer", "edit": "detail"}


def panel(state, panel_id):
    for entry in state.get("panels") or []:
        if entry.get("id") == panel_id:
            return entry
    return {}


def check_state(session, recorder):
    state = session.result("window.get_state")
    ids = [entry.get("id") for entry in state.get("panels") or []]
    recorder.check("get_state lists every panel", sorted(ids) == sorted(PANELS), "panels=%r" % ids)
    unmounted = [entry.get("id") for entry in state.get("panels") or [] if not entry.get("mounted")]
    recorder.check("every panel has its editor mounted", not unmounted, "unmounted=%r" % unmounted)
    recorder.check("the first-run view is arrange", state.get("view") == "arrange", "state=%r" % state.get("view"))
    names = [entry.get("editor") for entry in state.get("editors") or []]
    recorder.check("editors still lists the 0.4.0 names",
                   all(n in names for n in ("song", "piano_roll", "clip_launcher", "mixer")), "editors=%r" % names)


def check_views(session, recorder):
    for view, centre in CENTRES.items():
        reply = session.result("window.set_view", {"view": view})
        state = session.result("window.get_state")
        recorder.check("set_view %s takes effect" % view, reply.get("view") == view and state.get("view") == view,
                       "reply=%r" % reply)
        main = panel(state, centre)
        recorder.check("%s puts %s in the centre" % (view, centre),
                       main.get("region") == "center" and main.get("visible") is True, "panel=%r" % main)
    again = session.result("window.set_view", {"view": "edit"})
    recorder.check("set_view to the current view reports no change", again.get("changed") is False, "reply=%r" % again)
    error = session.typed_error("window.set_view", {"view": "tablet"})
    recorder.check("an unknown view is refused, typed", error.get("kind") == "invalid_args", "error=%r" % error)
    session.result("window.set_view", {"view": "arrange"})


def check_regions(session, recorder):
    shown = session.result("window.toggle", {"editor": "mixer"})
    state = session.result("window.get_state")
    recorder.check("toggle shows the mixer below the arranger",
                   shown.get("visible") is True and panel(state, "mixer").get("region") == "bottom", "reply=%r" % shown)
    recorder.check("the bottom region now hides the note editor", panel(state, "detail").get("visible") is False,
                   "detail=%r" % panel(state, "detail"))
    centre = session.result("window.toggle", {"editor": "arranger"})
    recorder.check("a view's main panel cannot be hidden", centre.get("visible") is True, "reply=%r" % centre)
    session.result("window.set_view", {"view": "edit"})
    revealed = session.result("window.toggle", {"editor": "launcher"})
    recorder.check("a panel the view lacks is revealed by switching view",
                   revealed.get("visible") is True and revealed.get("view") == "arrange", "reply=%r" % revealed)


def check_legacy_names(session, recorder):
    session.result("window.set_view", {"view": "mix"})
    song = session.result("window.toggle", {"editor": "song"})
    recorder.check("song is the arranger, and brings back the arrange view",
                   song.get("panel") == "arranger" and song.get("view") == "arrange", "reply=%r" % song)
    roll = session.result("window.toggle", {"editor": "piano_roll"})
    recorder.check("piano_roll is the note editor panel", roll.get("panel") == "detail", "reply=%r" % roll)


def check_floating_window(session, recorder):
    opened = session.result("window.toggle", {"editor": "project_notes"})
    recorder.check("project notes opens as a floating window",
                   opened.get("visible") is True and opened.get("panel") == "", "reply=%r" % opened)
    closed = session.result("window.toggle", {"editor": "project_notes"})
    recorder.check("and closes through the same verb", closed.get("visible") is False, "reply=%r" % closed)


def digest(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def check_screenshots(session, recorder, outdir):
    digests = set()
    for view in CENTRES:
        session.result("window.set_view", {"view": view})
        path = os.path.join(outdir, "shell-%s.png" % view)
        shot = session.result("window.screenshot", {"path": path})
        recorder.check("the %s view renders at the working size" % view,
                       (shot.get("width") or 0) >= 1600 and (shot.get("height") or 0) >= 1000, "reply=%r" % shot)
        if os.path.exists(path):
            digests.add(digest(path))
    recorder.check("the three views render three different images", len(digests) == 3, "distinct=%d" % len(digests))


def run_checks(session, recorder, outdir):
    check_state(session, recorder)
    check_views(session, recorder)
    check_regions(session, recorder)
    check_legacy_names(session, recorder)
    check_floating_window(session, recorder)
    check_screenshots(session, recorder, outdir)


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
        outdir = os.path.abspath(argv[2]) if len(argv) > 2 else instance.tmp
        os.makedirs(outdir, exist_ok=True)
        run_checks(Session(client, transcript), recorder, outdir)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("shell control-surface driver")
        return 1
    H.ok("shell control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
