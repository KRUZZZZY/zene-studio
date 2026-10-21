IMPLEMENTED

# Cluster Q050-064 - Note editor input ergonomics

The note editor now previews the Draw tool's note under the idle pointer, moves the preview, the
auditioned key and the committed note together when a draw drags to another pitch, gives Draw, Erase,
Slice and Audition their own cursors (immediately after a toolbar or keyboard tool change and after
a gesture), and fits each opened clip's used pitch range as well as its length. `window.get_state`'s
`note_editor` reports the resulting row height, top and bottom key and visible row count with the
active tool.

## Acceptance

| # | Criterion | Test command (from `build/tests`) | What it proves | Exit |
|---|-----------|-----------------------------------|----------------|------|
| 1 | Pointer preview, pitch adjustment, draw/erase/slice/audition cursors | `ctest -R '^NoteEditorFlTest$' --output-on-failure` | `aDrawPreviewsAndAdjustsPitchUnderThePointer` renders the idle preview, checks the old piano key stops and the new one sounds on a two-row draw, renders the moved preview, and undoes the one committed note; `theToolsCursorsFollowTheToolbarAndSurviveAGesture` drives the real toolbar actions and the D/P letters and checks the cursor at once and after a gesture | 0 (reverted product: 8) |
| 2 | Per-clip default zoom and visible row range | `ctest -R '^NoteEditorTest$' --output-on-failure` | `openingClipsFitsTheirPitchRangeAndZoom` shows a narrow range with margin in taller rows, then a wide clip at 6 px rows centred on its range with a further-out horizontal zoom; `openingAnEmptyClipFallsBackToTheDefaultKey` checks the C5/14 px fallback | 0 (reverted product: 8) |
| 3 | View settings and active tool in `window.get_state` | `ctest -R '^ControlNoteEditor$' --output-on-failure` | `check_clip_fit` opens narrow and wide clips over the socket and checks `window.get_state.note_editor`'s tool, zoom, row height, top/bottom key and visible row count against each `window.note_editor_set` reply, including after selecting Slice | 0 (reverted product: 8) |

Supporting checks, all run from the worktree: `python3 research/reviews/m1-session-2026-10-12/scratch/m0/snapshot_live.py $PWD` exit 0;
`python3 tests/doc-figures-gate.py` exit 0; `ctest -R '^ControlCommandsSnapshot$'` exit 0;
`ctest -R '^(ProjectStateTest|MidiClipControllersLoadTest)$'` exit 0;
`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py` equivalent (run through a `/tmp` copy because the
sibling logs directory is not writable here) exit 0, every gate PASS.

## Self-check

- **Each new test FAILS with the product change reverted, then PASSES restored.** Saved the four
  product files to `product-final.patch`, applied it in reverse, rebuilt `zene NoteEditorFlTest
  NoteEditorTest` (exit 0), and ran: `NoteEditorFlTest` exit 8
  (`aDrawPreviewsAndAdjustsPitchUnderThePointer` "the idle pointer shows a note preview" false;
  `theToolsCursorsFollowTheToolbarAndSurviveAGesture` cursor mismatch), `NoteEditorTest` exit 8
  (row height > 14 false; empty-clip row height mismatch), `ControlNoteEditor` exit 8 (narrow/wide fit
  checks failed). Re-applied the patch, rebuilt (exit 0), and the same three commands exited 0.
- **Every acceptance criterion is proven by a test I ran.** Criterion 1: `NoteEditorFlTest` exit 0;
  criterion 2: `NoteEditorTest` exit 0 plus `ControlNoteEditor`'s fit checks exit 0; criterion 3:
  `ControlNoteEditor` exit 0. The RED exits above are the same tests without the product hunks.
- **One undo step, honest reversibility.** The draw gesture still sends one `note.add` on release
  and `aDrawPreviewsAndAdjustsPitchUnderThePointer` asserts one `control.undo` restores the clip from
  4 to 3 notes; view/tool changes remain `not_mutating` in the existing reversibility table. No new
  command was added; `window.note_editor_set`'s result schema only gained typed fields.
- **Realtime.** The diff touches only GUI-thread code: `NoteCanvas` mouse/paint handlers, cursor
  updates, `NoteEditor::zoomToFit` and `describe`. Audition still goes through the existing
  `Piano::handleKeyPress/Release` queued path; no audio-thread function was added or changed, and no
  new allocation, lock, wait, I/O or signal is on the audio path. Local TSan does not run on this box.
- **Old projects.** No project-format or journalled state changed; editor tool/zoom/scroll stay
  interface state. `ProjectStateTest` and `MidiClipControllersLoadTest` both exited 0 on the final build.
- **Portability.** The cursors are standard `Qt::CursorShape` values (Qt 5.15 and Qt 6), the fit uses
  `std::clamp`/`std::max` already used by the file, and no Qt-6-only API, `std::filesystem`, POSIX call,
  Windows macro identifier or forbidden `small`/`near`/`far`/`interface`/`min`/`max` name was added.
- **Completion check.** The installed `submit_check.py` exited 1 with one FAIL: `gates
  (feature_check.py)`, because this session cannot create the sibling directory
  `<worktree>-feature-check` (`EACCES` from the file sandbox). Every other row passed. Running the same
  checker and the same gates with only their log directory redirected to `/tmp` (the gate scripts and
  the worktree unchanged) exited 0 with every row PASS, including `gates (feature_check.py)`; the
  only WARN is the canary row, which names existing command ids from the diff, not new ones.

Note: the requested `~/.local/share/zene-bunny-fix/qwen/runs/Q050-064.r1/logs` directory is
read-only in this session (write probes fail with EACCES), so the numbered build/test logs cited above
are under `~/.cache/zene-bunny-fix/qwen/runs/Q050-064.r1/logs/`; both the commands and exit codes are
the ones observed.
