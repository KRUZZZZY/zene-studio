IMPLEMENTED

# Q050-211 - Step input and recording in the new editor (completion, M, L07)

Worktree `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-Q050-211`, branch
`feat/050-Q050-211-oc`, base `3436a808b6`. Product commits on this branch: `97ae92a95d` (the driver's `check_step_input`
split), `2d9390b60a` (the recording commands), `aeece837ca` (the editor's Record half), `d61cda6a0d` (`journal_later`
on a take's later steps), `0d8ccaf038` and `11d74ef73c` (the complexity splits: `readKey` / `readVelocity` /
`readStart` / `openTake` / `selectOnly` in `ControlCommandsNoteRecord.cpp`, `applyDisplay` / `applyEntryModes` in
`ControlCommandsNoteEditor.cpp`); the commits after `11d74ef73c` are documentation, the snapshot and the manifests.
Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/Q050-211.r2/logs` (`$LOGDIR` below).

## Round 2: what the review asked for, and what is now true

1. **Complexity ratchet.** `check_step_input` in `tests/control-note-editor.py` was split by assertion into
   `check_step_input_armed` / `_first_step` / `_chord_placement` / `_chord_selection` / `_step_undo` /
   `_step_under_transport` / `_step_refusals` with `REFUSED_STEPS` and an orchestrator that only calls them
   (`97ae92a95d`). The final gate run adds the two product functions that the check found at the end of the round
   (`recordOn` CCN 16 and `applyView` CCN 11) and splits them too; `bash tests/complexity-gate.sh --check` now prints
   `PASS (check mode: no regressions; baseline not written)` (`complexity-5.log`). No baseline line was edited and
   `--reanchor` was never used.
2. **Live recording, reusing what exists.** `NoteStepInput.cpp` already held the queued `midiNoteOn` connection; it now
   also holds `midiNoteOff` (`followRecord`), opens a note on the key's note-on (`recordEnter` -> `note.record_on`) and
   closes it on the key-off (`onMidiNoteOff` -> `note.record_off`), both snapped to the header's grid, modelled on
   `PianoRoll::startRecordNote` / `finishRecordNote`. The take is armed through `window.note_editor_set {record}` (the
   toolbar's Record button), takes **one** MidiClip checkpoint on its first note, and the transport's stop finalises it
   (`note.record_end` from the command layer's own `Song::stopped` listener, so an agent's take ends the same way).
3. **Proof, with its own red exit.** A GUI case for the Record button and one for a played phrase
   (`clickingTheRecordButtonArmsTheModeThroughItsCommand`, `aPlayedPhraseIsRecordedAsOneTake`,
   `stoppingTheTransportFinishesTheTake`, `disarmingAndAnotherClipEndTheTake`), and socket checks appended as four
   further small helpers (`check_record_armed` / `_phrase` / `_under_transport` / `_disarm`, called by
   `check_recording`). The reverted tree fails all eight GUI cases that need the commands (`QT=8`) and the driver
   (`DRIVER=1`); restored, both are 0. Exit codes below.
4. **Docs corrected only after recording worked.** `docs/NOTE-EDITOR.md` §3a now points at a new §3b Recording, and §7
   and §8 no longer list live recording as missing; `docs/050-features/Q050-211.md` and
   `data/manual/index.html` describe the recording.
5. **Nothing weakened.** No test, gate, assertion or baseline line was removed or loosened; the only edits to existing
   tests are the appended record cases and the appended socket helpers.

## What was missing, measured on the base

`docs/NOTE-EDITOR.md` §8 said of the new editor: "Not in the new editor yet: MIDI recording, step input, chord
stamping and detuning automation. Use the classic piano roll". §7 said the classic roll remained "for recording from a
MIDI keyboard, step input". In the code, nothing under `src/gui/noteeditor/` connected to `InstrumentTrack::midiNoteOn`,
and no command kept a position or a held key: `note.add`/`note.add_many` take explicit positions.

## What changed

- `note.step_input` (new, `src/core/ControlCommandsNoteStepInput.cpp`): one step of step entry. `key` (one pitch) or
  `keys` (a chord, up to 32 distinct pitches), `position` (default: the clip's step cursor), `length` (default: the
  step), `step` (default 12 ticks), `velocity`, `select`. The notes are added under **one MidiClip journal
  checkpoint**, become the selection, and the cursor advances by `step`.
- The per-clip **step cursor** is core edit state (`include/ControlStepInput.h`, `src/core/ControlStepInput.cpp`):
  shared by the command and the editor, never saved, never journalled.
- `note.record_on` / `note.record_off` / `note.record_end` (new, `src/core/ControlCommandsNoteRecord.cpp` over
  `include/ControlNoteRecord.h`): the first opens a held note at the playhead snapped to the step and takes the clip's
  ONE journal checkpoint for the take (later calls report `applies: journal_later`); the second closes it where the
  playhead is then, so its length is what was played (at least one step); the third (`not_mutating`) ends the take.
  `Song::stopped` ends an open take, and `control.undo`/`control.redo` end them before they unwind.
- The editor's **Step input** and **Record** buttons (`src/gui/noteeditor/NoteStepInput.cpp` + `NoteEditor`,
  `NoteEditorToolbar`, `ShellIcons`): both run `window.note_editor_set`, both take the clip track's `midiNoteOn` (and,
  for Record, `midiNoteOff`) on queued connections, and both read the header's grid and note length, so the GUI and an
  agent share one command path and one set of settings. The modes are exclusive.
- `window.note_editor_set` gained `step_input`, `step_cursor` and `record` (args and reply); `window.get_state.note_editor`
  reports all three.

## Acceptance

| # | Criterion | Test | Command | Exit |
| --- | --- | --- | --- | --- |
| 1 | A MIDI-input test enters a note, advances by the selected grid step, and records a short phrase | `NoteEditorStepInputTest::aMidiKeyboardEntersAStepAndAdvancesByTheSelectedGrid`, `::aPlayedPhraseIsRecordedAsOneTake`; `tests/control-note-editor.py` with `tests/control_note_editor_entry.py` (`check_step_input`, `check_recording`) | `ctest -R '^NoteEditorStepInputTest$'`; `python3 tests/control-note-editor.py build/zene $LOGDIR/shots-green7` | 0 (reverted: 8); 0 (reverted: 1) |
| 2 | Computer-key step entry and MIDI keyboard entry use the same grid and note-length settings | `NoteEditorStepInputTest::computerKeysAndTheMidiKeyboardShareTheGridAndLength` | same ctest | 0 (reverted: 8) |
| 3 | A UI and socket test verify note placement, selection, undo, and transport-safe stop | `NoteEditorStepInputTest::clickingTheStepButtonArmsTheModeThroughItsCommand`, `::clickingTheRecordButtonArmsTheModeThroughItsCommand`, `::stoppingTheTransportLeavesNothingPending`, `::stoppingTheTransportFinishesTheTake`, `::disarmingAndAnotherClipEndTheTake`; `check_step_input`, `check_recording`; `window.screenshot` | same two commands + `python3 $LOGDIR/shot-record.py build/zene $LOGDIR/shots/record.png` | 0 / 0 (reverted: 8 / 1); screenshot 0 |

What each asserts, in one line each:

1. A queued `midiNoteOn` per key with the `1/8` grid chosen in the header and New Note Length set: notes at 0/24/48
   ticks, 30 ticks long, cursor 0 -> 24 -> 72, undo depth +1 per step, three `control.undos` empty the clip; the Record
   button plus three key down/up pairs write the watched phrase as one take; over the socket a bare step is a sixteenth
   at the cursor and `record_on`/`record_off` write `60@0+48` and `64@48+12` as ONE undo step.
2. `QTest::keyClick` `Z` writes a 30-tick note at the cursor, the next `midiNoteOn` lands 24 ticks on with the same 30
   ticks, and with the mode off `Z` selects the Paint tool and writes nothing.
3. A mouse click on the visible Step input button arms through `window.note_editor_set` and journals nothing; a mouse
   click on the visible Record button arms recording the same way; playing, three keys and a stop write the phrase and
   the stop ends the take; a key still held keeps one step; a late key-up after the stop writes nothing; a disarm or
   `setClip(nullptr)` leaves nothing pending; each step's notes are the selection; one `control.undo` takes a chord or a
   whole take back and leaves the cursor.

## Realtime

`Song.cpp`, `AudioEngine`, `NotePlayHandle`, the render path and every `process*` function are untouched by this diff.
The commands run on the control/GUI thread through `ControlRegistry::invoke`; the cursor and the held notes are a
`QHash` only that thread touches; the editor's MIDI path is the instrument track's existing `midiNoteOn` /
`midiNoteOff` signals received on a `Qt::QueuedConnection` - the same signal and the same hop the classic roll's record
pass already uses - so the emitting thread only appends to Qt's queued-connection event, exactly as it already did for
that signal. No allocation, lock, wait, I/O or signal emission was added on the audio path. Local TSan does not run on
this box, so this is a design argument plus the existing signals' own contract, not a measurement.

## Portability

- No Qt-6-only API added: the new code uses `QHash`, `QJsonObject`, `QVector`, `QMetaObject::Connection`, `QPointer`,
  `Qt::Key`, `Qt::QueuedConnection` and `QTest::keyClick`/`mouseClick`, all of them Qt 5.15 API; no
  `std::filesystem`; no POSIX-only call; no identifier named `small`, `near`, `far`, `interface`, `min` or `max`.
- This box builds Qt 6 only, so the first Qt 5.15 / MSVC / macOS build is CI's - stated as a limit, not a claim.

## Known limits

- While Step input is armed the piano letters win over the tool letters (P B D T C E Y and 1..7 play a note). The tools
  are one click away, and disarming gives the letters back.
- The on-screen key column still only auditions: it plays through the piano model, not the track's MIDI input, so it
  enters no step and records nothing.
- Recording is one take at the playhead: no punch region, no loop-pass take lanes and no count-in of its own (the
  classic roll keeps those). A key shorter than one grid step is recorded one grid step long, and a key still held at
  the stop keeps the step it had.
- The modes and the cursor are session state: they are not saved with the project.

## Self-check

1. **Each new test FAILS with the product change reverted, PASSES with it.** Reversed
   `git diff 3436a808b6..HEAD -- src include` (1796 lines; `git apply -R`, 20 files, 6 insertions, 1507 deletions),
   rebuilt `zene NoteEditorStepInputTest tripleoscillator` (`build-red4.log` exit 0):
   `ctest -R '^NoteEditorStepInputTest$'` -> **exit 8** (`test-red4-qt.log`: "Totals: 2 passed, 8 failed"), and
   `python3 tests/control-note-editor.py build/zene ...` -> **exit 1** (`control-note-editor-red4.log`: `no command
   'note.step_input'`, and the `window.note_editor_set` argument check that refuses `record`). Restored with
   `git apply product2.patch` (exit 0, `git status` empty), rebuilt (`build-green6.log` exit 0): the same two commands
   -> **exit 0** and **exit 0** (`test-green6-qt.log`, `control-note-editor-green7.log`).
2. **Every acceptance criterion is proven by a test I ran.** Criterion 1: `aMidiKeyboardEntersAStepAndAdvancesByTheSelectedGrid`
   and `aPlayedPhraseIsRecordedAsOneTake` exit 0 in the ctest run above; `check_step_input` and `check_recording` exit 0
   in the driver run. Criterion 2: `computerKeysAndTheMidiKeyboardShareTheGridAndLength` exit 0 (same ctest).
   Criterion 3: the five GUI cases above exit 0 in the same ctest (10 test functions, `Totals: 10 passed` in
   `test-qt-direct.log`), `check_step_input`/`check_recording` exit 0 (`control-note-editor-green7.log`), and the
   screenshot ran (exit 0, `shot-record.log`).
3. **One undo step per edit, honest class.** `note.step_input`, `note.record_on` and `note.record_off` are
   `true_inverse`; `note.record_end` is `not_mutating` (`ReversibilityContractTest` passes, `test-8.log`); the A16
   histogram line is `rows=948 true_inverse=468 snapshot=42 irreversible=25 not_mutating=413` (published
   configuration; this build measures 939 = 467 + 39 + 25 + 408 without the nine `wasm.*` rows). The undo depth is
   asserted per step (+1) and per take (one `control.undo` takes the whole phrase back, `check_record_phrase`).
4. **Nothing added on the audio thread.** See Realtime above; the check's audio-path hint fires on the test's
   `NotePlayHandleManager::init()` (the same setup the classic roll's test uses) and on comments only.
5. **Old projects load and render exactly as before; new data round-trips.** No project-reading or project-writing
   code changed: the cursor and the held notes are edit state (not saved, not journalled), and both entry paths write
   ordinary `Note`s that `MidiClip` already serialises. `ctest -R
   '^(ClipSerialisationTest|ControlCommandsSnapshot|ReversibilityContractTest|ControlRegistryTest)$'` -> 4/4 passed,
   exit 0 (`test-9.log`), plus the twelve neighbour tests of round 1 (which still pass in the driver and GUI runs here).
6. **Completeness walk.** User path: click Record -> the header button lights through `window.note_editor_set {record}`
   (undo depth unchanged) -> play: each key opens at the playhead snapped to the grid and closes where it came up ->
   the editor follows the playhead -> stop: the take ends, the notes stay, one Undo takes the phrase back ->
   click Record again: a new take with its own checkpoint. Agent path: `window.note_editor_set {record}` ->
   `note.record_on` / `note.record_off` (positions named or the playhead) -> `note.list` -> `control.undo` ->
   `note.record_end`. Edge cases tried: a key-up with nothing open (writes nothing, `closed: false`), a key already
   held (refused), a position past the song limit (refused), a stop under a held key (keeps one step), a late key-up
   after the stop (writes nothing), a disarm and a closed clip (no key reaches a clip).
7. **The GUI test drives the visible controls and checks the same registered command ran.** It `QTest::mouseClick`s the
   toolbar Record and Step input `ShellButton`s (asserting visible and checked), types `Z`/`B` at the note canvas, sets
   the grid with the header `QComboBox`, sends the MIDI edges as the track's own `midiNoteOn`/`midiNoteOff`, and asserts
   the undo depth (+1 per step, unchanged for view-only calls) and the state `window.get_state` reports.
8. **Qt 5.15 / MSVC / macOS < 10.15 portability.** See Portability above; not compiled on Qt 5 here.
9. **Same command on both paths, no second implementation.** `NoteStepInput.cpp` calls
   `ControlRegistry::instance()->invoke("note.step_input" / "note.record_on" / "note.record_off" / "note.record_end")`
   and `window.note_editor_set` for the switches; there is no direct `clip->addNote` in the editor's entry paths.
10. **Snapshot and docs figures.** `commands_snapshot.json` was regenerated from a live instance (939 ids / 78 groups,
    `snapshot-1.log`, `SNAP_EXIT=0`); `python3 tests/doc-figures-gate.py` -> exit 0 (`docfig-5.log`); the README canary
    is 939/78. `data/manual/index.html` has a Recording paragraph in the note editor section, and
    `docs/NOTE-EDITOR.md` has §3b Recording with §7/§8 corrected.
11. **Sources registered.** `include/ControlNoteRecord.h`, `src/core/ControlNoteRecord.cpp`,
    `src/core/ControlCommandsNoteRecord.cpp` and `tests/control_note_editor_entry.py` are in `tests/fork-sources.txt`
    (dated comment and the manifest's own recipe lines) and `tests/all-sources.txt` was re-derived with its recipe;
    `bash tests/fork-sources-gate.sh` -> exit 0 (`forkgate-6.log`), `bash tests/all-sources-reproduce.sh` -> exit 0
    (`allsources-6.log`).
12. **No weakened gate, and the file-length gate is clean.** The completion check's one FAIL was
    `file-length: new file over 500 lines: tests/control-note-editor.py (537)`. Nothing was deleted: the step-input and
    recording checks moved verbatim into `tests/control_note_editor_entry.py` (198 lines) and the driver imports the two
    orchestrators and passes its own `new_clip` (driver now 476 lines). The driver still runs both phases in one run
    (`control-note-editor-green7.log`, exit 0), and `complexity-gate.sh --check` -> exit 0 (`complexity-6.log`),
    `doc-figures-gate.py` -> exit 0 (`docfig-6.log`).

## Evidence table (every command as run)

| Step | Command | Exit | Log |
| --- | --- | --- | --- |
| Red build | `cmake --build build --target zene NoteEditorStepInputTest tripleoscillator -- -j4` after `git apply -R product2.patch` | 0 | `build-red4.log` |
| Red GUI test | `ctest -R '^NoteEditorStepInputTest$'` | 8 (8 failed, 2 passed) | `test-red4-qt.log` |
| Red socket | `python3 tests/control-note-editor.py build/zene $LOGDIR/shots-red4` | 1 | `control-note-editor-red4.log` |
| Green build | the same build after `git apply product2.patch` | 0 | `build-green6.log` |
| Green GUI test | `ctest -R '^NoteEditorStepInputTest$' --output-on-failure` | 0 | `test-green6-qt.log` |
| Green socket | `python3 tests/control-note-editor.py build/zene $LOGDIR/shots-green7` | 0 | `control-note-editor-green7.log` |
| Socket driver after the module split | `python3 tests/control-note-editor.py build/zene $LOGDIR/shots-green6` | 0 | `control-note-editor-green6.log` |
| GUI test, direct | `./NoteEditorStepInputTest` (from `build/tests`) | 0 (`Totals: 10 passed`) | `test-qt-direct.log` |
| Contract + snapshot + registry + project | `ctest -R '^(ClipSerialisationTest\|ControlCommandsSnapshot\|ReversibilityContractTest\|ControlRegistryTest)$'` | 0 (4/4) | `test-9.log` |
| Complexity ratchet | `PATH=$LOGDIR/venv/bin:$PATH bash tests/complexity-gate.sh --check` | 0 (`PASS (check mode: no regressions)`) | `complexity-6.log` |
| Doc figures | `python3 tests/doc-figures-gate.py` | 0 (`RESULT: PASS`) | `docfig-6.log` |
| Source manifests | `bash tests/fork-sources-gate.sh`; `bash tests/all-sources-reproduce.sh` | 0 (`PASS`); 0 | `forkgate-6.log`; `allsources-6.log` |
| Snapshot | live regenerate against this branch's binary (939 ids / 78 groups) | 0 | `snapshot-1.log` |
| Screenshot | `python3 $LOGDIR/shot-record.py build/zene $LOGDIR/shots/record.png` | 0 | `shot-record.log` |

Screenshot: `$LOGDIR/shots/record.png`, 1600x1000, sha256
`2a1cce88b5091cbcd4d21a8f9a200f7ba92096f2be0b412b37ffa6027cbf7c23` - the Edit view with the **Record** button lit
(`note_editor record=True`), a two-note take C4 (0..48 ticks) and G4 (72..96 ticks) written by
`note.record_on`/`note.record_off`.
