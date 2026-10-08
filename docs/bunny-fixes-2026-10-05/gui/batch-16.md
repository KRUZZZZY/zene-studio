# GUI batch-16 — Space Bunny fixer lane, 2026-10-05

Findings 5188, 2373, 2374, 3312, 3313, 2376 (Luna). Checked against
`bugs/bunny-gui-2026-10-05` at 0711eee8f; line numbers below are from that tree.

### Cluster 5188 — Fast paint strokes skip the grid cells between mouse events

Verdict: FIXED

Evidence: `src/gui/noteeditor/NoteCanvasInput.cpp:243` called `extendPaint(event->pos())` with only
the delivered position, while the erase/mute branch on the next line (245) already walked the whole
path via `strokeAlong(from, event->pos())`. `extendPaint()`
(`src/gui/noteeditor/NoteCanvasGestures.cpp:70`) is a per-cell marker, so with Qt coalescing a fast
drag into sparse move events, only the press and release cells ever got notes. New `paintAlong()`
(`NoteCanvasGestures.cpp:78`) walks the path in 2-pixel steps exactly like `strokeAlong()`.

Note: the neighbouring commit 0711eee8f (cluster 5189) fixed a different paint defect — the
duplicate check comparing only the start tick — so this is not a duplicate.

Commit: 7da791f23

Test: NoteEditorFlTest::aFastPaintStrokePaintsTheCellsItSkipsOver

Red/green:
- RED `cmake --build build --target zene NoteEditorFlTest` EXIT=0; `cd build/tests && ctest -R '^NoteEditorFlTest$' --output-on-failure` EXIT=8 — `FAIL! ... 'noteAt(m_clip, 408, 66) != nullptr' returned FALSE. (the first cell it crossed)`
- GREEN same build EXIT=0; same ctest EXIT=0 (15 passed, 0 failed)
- Related: `ctest -R '^(NoteEditorTest|CanvasKeyboardTest|NoteEditorFlTest|LaneReadoutTest|PianoRollBenchmarkTest|NoteEditingCommandsTest|NoteToolsTest)$'` EXIT=0
- Gates: `bash tests/complexity-gate.sh --check` EXIT=0; `bash tests/file-length-gate.sh --check` EXIT=0

### Cluster 2373 — Shift-band selection uses the release modifier

Verdict: ALREADY-FIXED

Evidence: `src/gui/noteeditor/NoteCanvasInput.cpp:395` commits the band with
`selectOnly(inside, m_pressMods.testFlag(Qt::ShiftModifier))` — the press-time snapshot, not
`m_mods` (the release modifiers). `pressBand()` at line 226 reads the same `m_pressMods`. The release
handler at lines 232-243 assigns `m_mods = event->modifiers()` but `commitBand()` never reads it. The
press-time Shift state has been used since b9a463ad4, so this report describes a pre-b9a463ad4 tree.

Commit: none

Test: none (NoteEditorFlTest::shiftLocksThePitchAndCtrlTheTimingWhileDragging and NoteEditorFlTest::ctrlSelectsAndCtrlShiftAddsWithoutDrawing already cover the behaviour)

Red/green: none

### Cluster 2374 — Ctrl-drag copies discard note properties

Verdict: FALSE-POSITIVE

Evidence: there is no Ctrl-drag copy path in the note canvas. Ctrl in this widget only does two
things: `pressDrawOrSelect()` at `NoteCanvasInput.cpp:167` turns a Ctrl press into a band selection,
and `dragTicks()` at line 102 uses Ctrl to LOCK the timing of an ordinary move (FL's horizontal lock)
— `commitMove()` at lines 355-368 always sends `note.edit` on the selection, never an add. The only
`note.add_many` in the whole note editor is `commitPaint()`
(`src/gui/noteeditor/NoteCanvasGestures.cpp:109`), which paints brand-new notes and has no source
note whose properties it could copy. The actual copy commands are `note.copy` / `note.paste` /
`note.duplicate` in `src/core/ControlCommandsNoteClipboard.cpp`, and those clone the whole note:
`copies.push_back(*note)` at line 296 and `addSelected()`'s `for (Note copy : copies)` at line 228,
which carry pan, articulation, lyric, probability, release velocity and the expression/condition
data with the `Note` object. The report's `NoteCanvasInput.cpp:303` is inside `commitDraw()` /
`commitMove()`, i.e. a draw or move, not a copy.

Commit: none

Test: none

Red/green: none

### Cluster 3312 — Erasing multiple notes creates multiple undo steps

Verdict: ALREADY-FIXED

Evidence: `commitErase()` at `src/gui/noteeditor/NoteCanvasInput.cpp:398-412` collects every stroked
note id and deletes them in ONE command, `note.remove_many` (line 411), with the comment on line 400
stating the rule. There is no per-id `note.remove` loop left in the file. Core takes a single
`clip->addJournalCheckPoint()` for the whole list
(`src/core/ControlCommandsNoteClipboard.cpp:107`), so the gesture is one journal entry.

Commit: none

Test: NoteEditorFlTest::aRightDragDeletesEveryNoteItCrossesInOneUndo (line 161 asserts both notes come
back with ONE undo), NoteEditorFlTest::theClipboardKeysCopyPasteDuplicateAndDelete

Red/green: none

### Cluster 3313 — Slicing a note is split across two undo steps

Verdict: ALREADY-FIXED

Evidence: `commitSlice()` at `src/gui/noteeditor/NoteCanvasGestures.cpp:142-158` collects all the
notes the line crosses into one `QJsonArray` and sends a single `note.slice` (line 156). There is no
`note.edit`-then-`note.add` sequence and no `sliceAt()` function anywhere in the tree. Core
`sliceNotes()` (`src/core/ControlCommandsNoteClipboard.cpp:130-160`) validates every target BEFORE
mutating (lines 134-137) and takes one `clip->addJournalCheckPoint()` (line 144) for the whole slice,
so a refusal cannot leave a half-sliced note either.

Commit: none

Test: NoteEditorFlTest::aRightDragFromAnEmptyRowSlicesWhatTheLineCrosses

Red/green: none

### Cluster 2376 — Slicing a note drops properties from the new segment

Verdict: ALREADY-FIXED

Evidence: `src/core/ControlCommandsNoteClipboard.cpp:150` builds the right half as
`Note half = *note;` — a full copy of the note — before adjusting only the position and length
(lines 153-154) and calling `clip->addNote(half, false)`. Pan, articulation, lyric, probability,
release velocity and the condition/expression data all travel with the `Note` copy. The only
deliberate change is `half.forgetTiming()` (line 152), which drops pre-quantise timing rather than a
note property, and matches the ratchet-repeat rule.

Commit: none

Test: none specific to slice properties; NoteEditingCommandsTest::sliceCutsTheCrossedNotesAsOneEdit

Red/green: none

Leftovers: cluster 2374's underlying idea (a copy path that drops properties) has no counterpart in
the note canvas — if a Ctrl-drag copy gesture is ever added it must clone via `Note`, not rebuild a
note from key/position/length/velocity. Nothing half-done.