#!/usr/bin/env python3
"""Group arrangement edits driven end to end over the control socket.

Starts the REAL `zene` binary headless (its own throwaway config) and proves:

  Q050-255  track.folder_spans reads a folder's summary clips; track.folder_span_resize and _split change the
            matching child clips together, a child named beside its summary clip changing once; the selection
            (track.folder_span_select) aims an edit without 'spans'; each edit is undone by ONE control.undo.
  Q050-219  section.move carries a section's material across every track, in gap and in ripple mode: read back
            through section.list and clip.list, and undone by ONE control.undo, clips and section together.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-arrangement-group-edits.py <zene>
Exit 0 only when every check held.
"""

import sys

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results

BAR = 192


def layout(session, track):
    """Every clip of `track` as (start, length), earliest first."""
    clips = session.result("clip.list", {"track": track}).get("clips") or []
    return sorted((c.get("start"), c.get("length")) for c in clips)


def sample_track(session, name, spans, folder=None):
    track = session.result("track.add", {"type": "sample", "name": name}).get("track")
    if folder is not None:
        session.result("track.set_folder", {"track": track, "folder": folder})
    for start, length in spans:
        session.result("sample.generate", {"track": track, "position": start, "length": length, "kind": "tone"})
    return track


def check_resize_and_split(session, recorder, folder, a, b, a0, b0):
    listed = session.result("track.folder_spans", {"folder": folder}).get("spans") or []
    recorder.check("track.folder_spans reads the two summary clips",
                   [(s.get("start"), s.get("end"), s.get("clips")) for s in listed] == [(0, 288, 2), (768, 960, 1)], str(listed))
    first = {"folder": folder, "start": 0, "end": 288}

    reply = session.result("track.folder_span_resize", {"spans": [first], "edge": "end", "delta": -96})
    recorder.check("a resize cuts the crossing child clip", layout(session, a)[0] == (0, 192) and layout(session, b) == [(96, 96)],
                   str((reply, layout(session, a), layout(session, b))))
    recorder.check("its reply reads the summary clip back", reply.get("spans", [{}])[0].get("end") == 192, str(reply))
    session.result("control.undo")
    recorder.check("one undo restores the resize", layout(session, a) == a0 and layout(session, b) == b0, str(layout(session, b)))

    session.result("track.folder_span_split", {"spans": [first], "at": 144})
    recorder.check("a split cuts each crossing clip", layout(session, a)[:2] == [(0, 144), (144, 48)]
                   and layout(session, b) == [(96, 48), (144, 144)], str((layout(session, a), layout(session, b))))
    session.result("control.undo")
    recorder.check("one undo restores the split", layout(session, a) == a0 and layout(session, b) == b0, str(layout(session, a)))


def check_selection(session, recorder, folder, a, b, a0, b0):
    outside = sample_track(session, "Outside", [(0, 192)])
    outside_clip = session.result("clip.list", {"track": outside}).get("clips")[0].get("clip")
    child = session.result("clip.list", {"track": b}).get("clips")[0].get("clip")
    session.result("track.folder_span_select", {"folder": folder, "at": 0})
    session.result("track.folder_span_select", {"folder": folder, "at": 800, "add": True})
    selected = session.result("track.folder_spans", {"folder": folder}).get("selected") or []
    recorder.check("two summary clips can be selected", len(selected) == 2, str(selected))
    session.result("track.folder_span_resize", {"clips": [outside_clip, child], "edge": "end", "delta": 48})
    recorder.check("the selection and named clips resize together, a child once",
                   layout(session, a)[1] == (768, 240) and layout(session, b) == [(96, 240)] and layout(session, outside) == [(0, 240)],
                   str((layout(session, a), layout(session, b), layout(session, outside))))
    session.result("control.undo")
    recorder.check("one undo restores them all", layout(session, a) == a0 and layout(session, b) == b0
                   and layout(session, outside) == [(0, 192)], str((layout(session, a), layout(session, outside))))
    session.result("track.folder_span_select", {})
    session.result("track.remove", {"track": outside})


def check_folder_spans(session, recorder):
    folder = session.result("track.add", {"type": "folder", "name": "Group"}).get("track")
    a = sample_track(session, "A", [(0, 192), (768, 192)], folder)
    b = sample_track(session, "B", [(96, 192)], folder)
    a0, b0 = layout(session, a), layout(session, b)
    recorder.check("the fixture's clips are where they were put", a0 == [(0, 192), (768, 192)] and b0 == [(96, 192)], str((a0, b0)))
    check_resize_and_split(session, recorder, folder, a, b, a0, b0)
    check_selection(session, recorder, folder, a, b, a0, b0)
    for track in (a, b, folder):
        session.result("track.remove", {"track": track})


def check_section_move(session, recorder):
    one = sample_track(session, "One", [(4 * BAR, 4 * BAR), (8 * BAR, 2 * BAR)])
    two = sample_track(session, "Two", [(3 * BAR, 3 * BAR)])
    section = session.result("section.add", {"start": 4 * BAR, "end": 8 * BAR, "name": "Chorus"}).get("section")
    before = layout(session, one), layout(session, two)

    def chorus():
        return next(s for s in session.result("section.list").get("sections") if s.get("section") == section)

    reply = session.result("section.move", {"section": section, "start": 12 * BAR, "mode": "gap"})
    recorder.check("gap: only the section's material moves", layout(session, one) == [(8 * BAR, 2 * BAR), (12 * BAR, 4 * BAR)]
                   and layout(session, two) == [(3 * BAR, BAR), (12 * BAR, 2 * BAR)], str((reply, layout(session, one), layout(session, two))))
    recorder.check("gap: the section reads back at its new place", chorus().get("start") == 12 * BAR and chorus().get("end") == 16 * BAR, str(chorus()))
    session.result("control.undo")
    recorder.check("one undo restores clips and section", (layout(session, one), layout(session, two)) == before
                   and chorus().get("start") == 4 * BAR, str((layout(session, one), chorus())))

    session.result("section.move", {"section": section, "start": 12 * BAR, "mode": "ripple"})
    recorder.check("ripple: what it passed closes the gap", layout(session, one) == [(4 * BAR, 2 * BAR), (12 * BAR, 4 * BAR)], str(layout(session, one)))
    session.result("control.undo")
    session.result("control.redo")
    recorder.check("one redo makes the whole move again", layout(session, one) == [(4 * BAR, 2 * BAR), (12 * BAR, 4 * BAR)]
                   and chorus().get("start") == 12 * BAR, str((layout(session, one), chorus())))
    session.result("control.undo")
    recorder.check("one undo restores the ripple", (layout(session, one), layout(session, two)) == before
                   and chorus().get("start") == 4 * BAR, str((layout(session, one), chorus())))


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
        session.result("control.set_undo_coalescing", {"window_ms": 0})
        check_folder_spans(session, recorder)
        check_section_move(session, recorder)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("group-edits control-surface driver")
        return 1
    H.ok("group-edits control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
