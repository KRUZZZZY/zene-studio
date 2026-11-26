IMPLEMENTED

# Q050-211 - Step input and recording in the new editor (completion, M, L07)

Worktree `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-Q050-211`, branch
`feat/050-Q050-211-oc`, base `3436a808b6` (`050/m0-train-53`, the FIX-BASE commit). Product commit `7092bdae0c`;
this report and the feature record are the commit after it. Logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/Q050-211.r1/logs` (`$LOGDIR` below).

## What was missing, measured on the base

`docs/NOTE-EDITOR.md` §8 said of the new editor: "Not in the new editor yet: MIDI recording, step input, chord
stamping and detuning automation. Use the classic piano roll". §7 said the classic roll remained "for recording from a
MIDI keyboard, step input". In the code, nothing under `src/gui/noteeditor/` connected to `InstrumentTrack::midiNoteOn`,
and no command kept a position to put a key at: `note.add`/`note.add_many` take explicit positions.

## What changed

- `note.step_input` (new, `src/core/ControlCommandsNoteStepInput.cpp`): one step of step entry. `key` (one pitch) or
  `keys` (a chord, up to 32 distinct pitches), `position` (default: the clip's step cursor), `length` (default: the
  step), `step` (default 12 ticks), `velocity`, `select`. The notes are added under **one MidiClip journal
  checkpoint**, become the selection, and the cursor advances by `step`.
- The per-clip **step cursor** is core edit state (`include/ControlStepInput.h`, `src/core/ControlStepInput.cpp`):
  shared by the command and the editor, never saved, never journalled.
- The editor's **Step input mode** (`src/gui/noteeditor/NoteStepInput.cpp` + `NoteEditor`, `NoteEditorToolbar`,
  `ControlCommandsNoteEditor`): a checkable **Step input** button in the header runs `window.note_editor_set
  {step_input}`; armed, the clip track's `midiNoteOn` (queued connection) and the piano letters typed at the note area
  both call `note.step_input` with the editor's own grid and note length.
- `window.note_editor_set` gained `step_input` and `step_cursor` (args and reply); `window.get_state.note_editor`
  reports both.

## Acceptance

| # | Criterion | Test | Command | Exit |
| --- | --- | --- | --- | --- |
| 1 | A MIDI-input test enters a note, advances by the selected grid step, and records a short phrase | `NoteEditorStepInputTest::aMidiKeyboardEntersAStepAndAdvancesByTheSelectedGrid`; `tests/control-note-editor.py` `check_step_input` | `ctest -R '^NoteEditorStepInputTest$'`; `python3 tests/control-note-editor.py build/zene $LOGDIR/shots` | 0 (reverted: 8); 0 (reverted: 1) |
| 2 | Computer-key step entry and MIDI keyboard entry use the same grid and note-length settings | `NoteEditorStepInputTest::computerKeysAndTheMidiKeyboardShareTheGridAndLength` | same ctest | 0 (reverted: 8) |
| 3 | A UI and socket test verify note placement, selection, undo, and transport-safe stop | `NoteEditorStepInputTest::clickingTheStepButtonArmsTheModeThroughItsCommand`, `::stoppingTheTransportLeavesNothingPending`, `::aMidiKeyboardEntersAStep...` (selection + undo); `check_step_input` over the socket; `window.screenshot` | same two commands + `python3 $LOGDIR/shot-step-input.py build/zene $LOGDIR/shots/step-input.png` | 0 / 0 (reverted: 8 / 1); screenshot 0 |

What each asserts, in one line each:

- Criterion 1: with `1/8` chosen in the header's grid box (24 ticks) and New Note Length 30, keys 60, 64, 67 land at
  0, 24 and 48 ticks, each 30 ticks long; the cursor reads 0, 24, 72; the undo depth grows by exactly one per step;
  three `control.undo`s empty the clip. Over the socket: a bare step is a sixteenth at the cursor (0), the reply and
  `window.get_state` agree on cursor 12, a `keys: [64, 67]` chord lands at 12 with `step: 24, length: 30`.
- Criterion 2: `QTest::keyClick` `Z` at the note area writes a 30-tick note at the cursor; the next `midiNoteOn` lands
  exactly 24 ticks later with the same 30 ticks; with the mode off the same letter selects the Paint tool and writes
  nothing.
- Criterion 3: a `QTest::mouseClick` on the visible toolbar button arms the mode through `window.note_editor_set`
  (undo depth unchanged), the button follows a command that disarms it, `window.get_state` reports the switch; the
  socket sees each step's notes as the selection; one `control.undo` takes a chord back and leaves the cursor; a step
  during `transport.play` lands and `transport.stop` writes nothing and keeps the cursor; playing, two keys and
  `Song::stop()` leave two notes, the mode and the cursor, the late key-up pairs with nothing, and a disarm or
  `setClip(nullptr)` makes a further key write nothing.

### Revert proof

`$LOGDIR/product.patch` is `git diff 3436a808b6..7092bdae0c -- src include` (932 lines); reversed with
`git apply -R`, then `cmake --build build --target zene NoteEditorStepInputTest tripleoscillator -- -j4`
(`build-9-red.log`, `EXIT=0`):

| Run | Log | Exit |
| --- | --- | --- |
| `ctest -R '^NoteEditorStepInputTest$'` (reverted) | `test-6-red-qt.log` | **8** - four failed test functions, all of them at `window.note_editor_set {step_input}` being refused and the button absent |
| `python3 tests/control-note-editor.py build/zene $LOGDIR/shots-red` (reverted) | `control-note-editor-4-red.log` | **1** - every step check `no command 'note.step_input'`; the earlier view checks still passed |
| rebuilt after `git apply product.patch` | `build-10-green.log` | 0 |
| `ctest -R '^NoteEditorStepInputTest$'` (restored) | `test-7-green-qt.log` | **0** - 1/1 passed out of 1 |
| `python3 tests/control-note-editor.py build/zene $LOGDIR/shots` (restored) | `control-note-editor-5-green.log` | **0** - every check `ok`, `PASS: note editor control-surface driver` |

The QtTest compiles against the reverted tree on purpose (it reads the mode and cursor through `describe()` and finds
the button by its text), so the red run fails on behaviour, not on a missing symbol.

## GUI and agent use the same command

| User action | GUI control | Registered command |
| --- | --- | --- |
| Arm/disarm step entry | the header's checkable **Step input** button (icon `step`, lucide `footprints`) | `window.note_editor_set {step_input}` |
| Enter a step from a MIDI keyboard | the same button armed, then a key | `note.step_input` |
| Enter a step from the computer keys | the same button armed, then the piano letters at the note area | `note.step_input` |
| Put the cursor somewhere | `window.note_editor_set {step_cursor}` (agent), arming at the playhead (user) | `window.note_editor_set` |

The editor holds no write path of its own: `NoteStepInput.cpp` builds a `note.step_input` argument object and calls
`ControlRegistry::invoke`, and the button calls `window.note_editor_set`. The GUI test drives the visible control
(`QTest::mouseClick` on the button, `QTest::keyClick` at the canvas, the grid `QComboBox`) and checks the same
command's effect (undo depth +1 per step, the state the agent reads through `window.get_state`).

## Undo and reversibility

| Command | Class | Undo behaviour |
| --- | --- | --- |
| `note.step_input` (new) | `true_inverse` | One MidiClip journal checkpoint per step, so one `control.undo` takes the step's notes back (a chord included) and the cursor stays where it is. Proven by `clicking...`/`aMidiKeyboardEnters...` (depth +1 per step, three undos empty the clip) and `check_step_input` (one undo takes the chord, the clip keeps the first step) |
| `window.note_editor_set` (changed, two args added) | `not_mutating` (unchanged class) | It writes view/edit state only; the test asserts the undo depth is unchanged after the button arms the mode |

## Metadata, snapshot and figures

- `tools/mcp-zene-control/zene_control/commands_snapshot.json` regenerated from a live instance of this branch:
  936 ids, 78 groups (`snapshot-1.log`, exit 0). `ControlCommandsSnapshot` (ctest #983) passes in `test-5.log`.
- `tests/doc-figures-gate.py`: PASS (`feature-check-1.log`, exit 0); the README canary is 936 ids / 78 groups.
- `ReversibilityContractTest`: passes with the new row (`test-5.log`, exit 0), and the A16 line moved to
  `rows=945 true_inverse=466 snapshot=42 irreversible=25 not_mutating=412` in `docs/RELEASE-NOTES-v0.3.0-alpha.md`.

## Neighbours that had to keep passing

`ctest -R '^(ControlRegistryTest|NoteEditorTest|NoteEditorFlTest|NoteEditorStepInputTest|PianoRollRecordTest|NoteNoOpEditUndoStepTest|NoteCommandTransactionsTest|StepRecorderWidgetTest|MidiClipStepMenuTest|ControlNoteEditor|ControlCommandsSnapshot|ReversibilityContractTest)$'`
-> 12/12 passed, `EXIT=0` (`test-5.log`). `ControlRegistryTest`'s hard-coded count is untouched: this build has the
telemetry client compiled in, so the `#if` branch that asserts a count does not run, and the count-asserting branch was
not edited.

## Registration and gates

- Five new files registered in `tests/fork-sources.txt` (with a dated provenance comment) and `tests/all-sources.txt`
  re-derived with its header recipe: `bash tests/all-sources-reproduce.sh` -> `REPRODUCES`, exit 0
  (`all-sources-1.log`); `bash tests/fork-sources-gate.sh` -> PASS, exit 0 (`fork-gate-1.log`).
- `python3 .../feature_check.py $PWD` -> every gate PASS, `RESULT: PASS`, exit 0 (`feature-check-1.log`).

## Realtime

Nothing added on the audio thread. `note.step_input` runs on the control/GUI thread through `ControlRegistry::invoke`;
the cursor is a `QHash` touched only there. The editor's MIDI path is the instrument track's own `midiNoteOn`, received
on a `Qt::QueuedConnection` - the same signal and the same hop the classic roll's record pass already uses - so the
audio thread only appends to Qt's queued-connection event, exactly as it already did for that signal. `Song.cpp`,
`AudioEngine`, `NotePlayHandle` and the render path are untouched by this diff. Local TSan does not run on this box, so
this is a design argument plus the existing signal's own contract, not a measurement.

## Portability

- No Qt-6-only API added: the new code uses `QHash`, `QJsonObject`, `QVector`, `QKeySequence`/`Qt::Key` and
  `Qt::QueuedConnection`, all of them Qt 5.15 API; no `std::filesystem`; no POSIX-only call; no identifier named
  `small`, `near`, `far`, `interface`, `min` or `max`.
- The test uses `QTest::keyClick`/`mouseClick` and `QAbstractButton::isChecked`, all Qt 5.15.

## Known limits

- While Step input is armed the piano letters win over the tool letters (P B D T C E Y and 1..7 play a note). The tools
  are one click away, and disarming gives the letters back.
- The on-screen key column still only auditions: it plays through the piano model, not the track's MIDI input, so it
  enters no step.
- Live (transport-time) recording is still the classic piano roll's (docs/NOTE-EDITOR.md §7); this unit covers step
  entry from a MIDI keyboard and the computer keys.
- The mode and the cursor are session state: they are not saved with the project.

## Self-check

1. **Each new test FAILS with the product change reverted.** Reversed `git diff 3436a808b6..7092bdae0c -- src include`,
   rebuilt (`build-9-red.log` exit 0): `ctest -R '^NoteEditorStepInputTest$'` -> **exit 8** (four failed test functions,
   `test-6-red-qt.log`); `python3 tests/control-note-editor.py build/zene ...` -> **exit 1** (no `note.step_input`,
   `control-note-editor-4-red.log`). Restored with `git apply product.patch`, rebuilt (`build-10-green.log` exit 0):
   the same two commands -> **exit 0** and **exit 0** (`test-7-green-qt.log`, `control-note-editor-5-green.log`).
2. **Every acceptance criterion is proven by a test I ran.** Criterion 1: `NoteEditorStepInputTest::aMidiKeyboard...`
   exit 0, `check_step_input` exit 0. Criterion 2: `computerKeysAndTheMidiKeyboardShareTheGridAndLength` exit 0.
   Criterion 3: `clickingTheStepButton...` + `stoppingTheTransportLeavesNothingPending` + `aMidiKeyboard...` exit 0,
   `check_step_input` exit 0, screenshot exit 0. All in one `ctest` run of 12 tests, exit 0 (`test-5.log`, `test-7-green-qt.log`).
3. **One undo step per edit, honest class.** `note.step_input` is `true_inverse`: one MidiClip checkpoint per call,
   proven by the undo-depth assertions (+1 per step, three undos empty the clip) and by the socket check (one undo takes
   a two-note chord, the earlier step stays). `window.note_editor_set` stays `not_mutating` (view/edit state), and the
   test asserts the undo depth does not move when the button arms the mode.
4. **Nothing added on the audio thread.** The diff touches no `process*`/`render*`/`AudioEngine`/`NotePlayHandle` code.
   `note.step_input` runs on the GUI thread; the editor's MIDI path is the instrument track's existing `midiNoteOn`
   signal on a queued connection (classic roll's own hop); the cursor is a `QHash` only the control thread touches. No
   allocation, lock, wait, I/O or signal emission added there. TSan does not run on this box - reasoned, not measured.
5. **Old projects load and render exactly as before; new data round-trips through save and load.** No project-reading
   or project-writing code changed: the cursor is edit state (not saved, not journalled), and a step writes ordinary
   `Note`s that the owner `MidiClip` already serializes. `ClipSerialisationTest` and `ProjectOpenIntegrityTest`:
   `ctest -R '^(ClipSerialisationTest|ProjectOpenIntegrityTest)$'` -> 2/2 passed, exit 0 (`test-8-projects.log`); the
   twelve neighbours above include the note-editor and note-transaction suites.
6. **Completeness walk.** User path: arm the header button (click, undo depth unchanged) -> the cursor is put at the
   playhead snapped down -> `Z` at the note area writes a 30-tick note at the cursor and selects it -> a MIDI key writes
   the next one 24 ticks on with the same length -> one `control.undo` per step -> transport stop under a held key
   writes nothing, keeps the cursor and the mode, the late key-up pairs with nothing -> disarm drops the connection ->
   the clip is closed and a key writes nothing. Agent path: `window.note_editor_set {clip, grid, new_length,
   step_input, step_cursor}` -> `note.step_input {clip, key}` or `{clip, keys, step, length}` -> `note.list` shows the
   positions, lengths and `selected` flags -> `control.undo` -> `transport.play`/`transport.stop`. Edge cases tried:
   both `key` and `keys` (refused, typed), neither (refused), key 128 (refused), a repeated pitch (refused), a position
   past the song limit (refused), and `window.note_editor_set {step_input: false}` (disarms). Undo does not shift the
   cursor; a stale clip (closed) cannot be written through.
7. **The GUI test drives the visible control and checks the same registered command ran.** It `QTest::mouseClick`s the
   toolbar `ShellButton` (and asserts it is visible and checked), types `Z`/`B` with `QTest::keyClick` at the note
   canvas, sets the grid with the header `QComboBox`, and asserts the undo depth increases by exactly one per step
   (and does not move for the view-only command) plus the state `window.get_state` reports.
8. **Qt 5.15 / MSVC / macOS < 10.15 portability.** No Qt-6-only API (the new code uses `QHash`, `QVector`, `QJsonObject`,
   `Qt::Key`, `Qt::QueuedConnection` - all Qt 5.15); no header named after a windows.h macro; no `std::filesystem`; no
   POSIX-only call; the platform-neutral logical key is used for the piano map. Not compiled on Qt 5 here (this box is
   Qt 6 only), so CI is the first Qt 5 build - stated as a limit, not a claim.
9. **Same command on both paths, no second implementation.** `NoteStepInput.cpp` calls
   `ControlRegistry::instance()->invoke("note.step_input", ...)` and `window.note_editor_set` for the switch; there is
   no direct `clip->addNote` in the editor's step path.

## Evidence table (every command as run)

| Step | Command | Exit | Log |
| --- | --- | --- | --- |
| Build | `cmake --build build --target zene NoteEditorStepInputTest ControlRegistryTest NoteEditorTest NoteEditorFlTest NoteNoOpEditUndoStepTest NoteCommandTransactionsTest PianoRollRecordTest StepRecorderWidgetTest MidiClipStepMenuTest tripleoscillator -- -j4` | 0 | `build-8.log` |
| Neighbours + own tests | `ctest -R '^(ControlRegistryTest\|NoteEditorTest\|NoteEditorFlTest\|NoteEditorStepInputTest\|PianoRollRecordTest\|NoteNoOpEditUndoStepTest\|NoteCommandTransactionsTest\|StepRecorderWidgetTest\|MidiClipStepMenuTest\|ControlNoteEditor\|ControlCommandsSnapshot\|ReversibilityContractTest)$'` | 0 (12/12) | `test-5.log` |
| Project load/serialisation | `ctest -R '^(ClipSerialisationTest\|ProjectOpenIntegrityTest)$'` | 0 (2/2) | `test-8-projects.log` |
| Red build | `cmake --build build --target zene NoteEditorStepInputTest tripleoscillator -- -j4` (after `git apply -R product.patch`) | 0 | `build-9-red.log` |
| Red test | `ctest -R '^NoteEditorStepInputTest$'` | 8 | `test-6-red-qt.log` |
| Red socket | `python3 tests/control-note-editor.py build/zene $LOGDIR/shots-red` | 1 | `control-note-editor-4-red.log` |
| Green build | `cmake --build build --target zene NoteEditorStepInputTest tripleoscillator -- -j4` (after `git apply product.patch`) | 0 | `build-10-green.log` |
| Green test | `ctest -R '^NoteEditorStepInputTest$'` | 0 | `test-7-green-qt.log` |
| Green socket | `python3 tests/control-note-editor.py build/zene $LOGDIR/shots` | 0 | `control-note-editor-5-green.log` |
| Screenshot | `python3 $LOGDIR/shot-step-input.py build/zene $LOGDIR/shots/step-input.png` | 0 | `shot-1.log` |
| Snapshot | live regenerate (`snapshot_commands.py --socket`), 936 ids / 78 groups | 0 | `snapshot-1.log` |
| Sources | `bash tests/all-sources-reproduce.sh`; `bash tests/fork-sources-gate.sh` | 0; 0 | `all-sources-1.log`; `fork-gate-1.log` |
| Gates | `python3 .../feature_check.py $PWD` | 0 | `feature-check-1.log` |

Screenshot: `$LOGDIR/shots/step-input.png`, 1600x1000, sha256
`4b7e6fb20bcb37eb8bf58f9c08cfceb0f45fb2875e16a78393fe0f3ade5ac48b` - the Edit view with the **Step input** button
lit, the `1/8` grid and the four stepped notes C5 D5 E5 G5 at 24-tick steps.
