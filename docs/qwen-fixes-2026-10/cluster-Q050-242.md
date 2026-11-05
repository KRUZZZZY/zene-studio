IMPLEMENTED

# Q050-242 — Pattern recurrence controls

Branch `feat/050-Q050-242-oc`, cut from `050/m0-train-24` (`2a7fd9f9c7`). Product commit
`d9c4902759`, tests + feature record `bd48d406da`, report `c46f6313c5`/`427d0dc5e6`.

The recurrence model (`NoteConditions::recursOnPass`, `recl`/`recm`, `note.edit`'s
`recur_length`/`recur_mask`) and the playback gate already existed. This unit exposes the pair in
the note properties box, publishes it through `roll.get_state`, and proves the pass contract
through a locally looped clip.

## Acceptance

| # | Criterion | Test / command | Proof | Exit |
|---|---|---|---|---|
| 1 | The editor exposes recurrence length and pass mask and reads back their saved values | `cd build/tests && ctest -R "^NoteEditorFlTest$" --output-on-failure` (log `test-20.log`) | `thePropertiesBoxExposesThePassPatternAndReadsItBack`: a note carrying 2/1 opens with `recur_length` 2, pass 1 ticked, pass 2 unticked and pass 3 hidden; ticking pass 3 at length 4 makes `changes()` the pair `{recur_length: 4, recur_mask: 4}`; the dialog's own OK button is clicked and the note carries 4/4, `recursOnPass` playing only pass 2; **one** `control.undo` restores 2/1 and a box opened again shows 2/1; length 0 greys the bits and reports mask 0. `aDoubleClickOpensTheNotesPropertiesAndOkIsOneEdit` asserts both controls exist on the double-click's box. | 0 |
| 1 | ...and the saved values survive a save/reload | `cd build/tests && ctest -R "^NotePlaybackTest$"` (log `test-20.log`) | `aRecurrencePairRoundTripsThroughTheProjectFile`: the clip element holds exactly one `recl="4"`/`recm="5"`; `saveProjectFile` + `project.new` + `loadProject` reloads 4/5, the note recurs on passes 0 and 2 only, the neutral note writes no recurrence attribute, and `roll.get_state` reports the reloaded pair. | 0 |
| 2 | Playback tests verify notes sound only on the selected passes, including looped clips | `cd build/tests && ctest -R "^NotePlaybackTest$"` (log `test-20.log`) | `recurrenceSelectsThePassesOfALocallyLoopedClip`: a 96-tick clip looped every 24 ticks renders at 999 BPM over five song loop wraps; with length 2 / mask 1 the note starts only on passes 0, 2 and 4 and never on an odd pass, although the clip keeps folding within each pass; length 2 / mask 0 sounds no pass; length 0 / mask 0 sounds again. The pair is written by `note.edit` and read back from `roll.get_state` first. | 0 |
| 3 | A control-socket edit and undo restore the original recurrence settings | `cd build/tests && ctest -R "^ControlNoteEditor$" -V` (log `test-20.log`) | `check_recurrence` in `tests/control-note-editor.py`: a fresh note reads `recur_length` 0 / `recur_mask` 0; a mask without a length is a typed `invalid_args`; `note.edit` writes 2/1 and `roll.get_state` reads both back; length 17 is refused; **one** `control.undo` restores 0/0. | 0 |
| — | No regression to the command contract, undo classes or snapshot | `ctest -R "^(ReversibilityContractTest|ControlCommandsSnapshot)$"` (log `test-16.log`) | no new command or row (878 ids / 77 groups; A16 histogram rows=887 unchanged); snapshot regenerated from a live instance. | 0 |

### Reverted-product evidence

| Test | Product reverted | Exit |
|---|---|---|
| `NoteEditorFlTest::thePropertiesBoxExposesThePassPatternAndReadsItBack` | `include/NoteProperties.h` + `src/gui/noteeditor/NoteProperties.cpp` set back to `2a7fd9f9c7`, rebuilt | 1 (`length != nullptr`) |
| `NotePlaybackTest::aRecurrencePairRoundTripsThroughTheProjectFile` and `::recurrenceSelectsThePassesOfALocallyLoopedClip` | only `src/core/ControlEditSupport.cpp` reversed (the readback removed), rebuilt | 1 and 1 (`recur_length` mismatch) |
| `ControlNoteEditor` | all six product hunks set back to `2a7fd9f9c7`, rebuilt | 8 (readback and undo checks FAILED) |

Restored afterwards; the final run (`test-20.log`) is 3/3 passed, exit 0, and the full
five-test run (`test-16.log`) was 5/5 passed, exit 0.

## Self-check

**? Each new test FAILS with your product change reverted (reverse the non-test hunks, rebuild,
run: exit code?), then PASSES restored.**
- `NoteEditorFlTest::thePropertiesBoxExposesThePassPatternAndReadsItBack`: reverted → exit 1
  (`'length != nullptr' returned FALSE`); restored → exit 0 (`Totals: 19 passed`).
- `NotePlaybackTest::recurrenceSelectsThePassesOfALocallyLoopedClip` and
  `::aRecurrencePairRoundTripsThroughTheProjectFile`: with `ControlEditSupport.cpp` reverted (the
  only product hunk they need) → exit 2 with both failing on the `recur_length` comparison; with
  everything restored → exit 0.
- `ControlNoteEditor`: all six product hunks reverted → exit 8, three checks FAILED; restored →
  exit 0.
- Logs: `reverted-gui-4.log`, `reverted-readback.log`, `reverted-socket.log` (reverted) and
  `test-19.log`, `test-14.log`, `test-13.log`, `test-20.log` (restored), in `/tmp/q050-242-logs/`.
  Each revert was `git checkout 2a7fd9f9c7 -- <product files>` and each restore
  `git checkout HEAD -- <product files>`, so the tested trees are exactly the committed ones.

**? Every acceptance criterion is proven by a test you ran yourself (name the test and its exit
code per criterion).**
- Criterion 1, editor exposure/readback: `NoteEditorFlTest::thePropertiesBoxExposesThePassPatternAndReadsItBack`
  (exit 0) plus `::aDoubleClickOpensTheNotesPropertiesAndOkIsOneEdit` (exit 0); persistence part:
  `NotePlaybackTest::aRecurrencePairRoundTripsThroughTheProjectFile` (exit 0).
- Criterion 2, selected passes including looped clips:
  `NotePlaybackTest::recurrenceSelectsThePassesOfALocallyLoopedClip` (exit 0).
- Criterion 3, socket edit + one undo: `ControlNoteEditor`'s `check_recurrence` (exit 0).
- All of them ran under `ctest -R "^(NoteEditorFlTest|NotePlaybackTest|ControlNoteEditor)$"`
  with exit 0 (`test-20.log`) and earlier under the five-test run (`test-16.log`, exit 0).

**? Each edit is ONE undo step, and every new or changed command has an honest reversibility
class.**
- One undo step: the dialog sends one `note.edit` with one `{note, recur_length, recur_mask}`
  entry (the pair travels together), and `note.edit` takes one `MidiClip` checkpoint; the GUI
  test proves `control.undo` once restores both values. No new command was added; `note.edit`
  stays `true_inverse` (its rationale text in `ControlReversibilityTableLive.cpp:220-224` now
  names the recurrence pair and the mask-cut reason). `roll.get_state` stays `not_mutating`. No
  reversibility row changed class, and `ReversibilityContractTest` passed (exit 0).

**? Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or
emits a signal (your diff touches audio-path code - name the functions and say why each is
safe).**
- The diff's only audio-path proximity is `control::noteState()` (`src/core/ControlEditSupport.cpp`),
  which is a UI/control-thread function: it is called by the command handlers and `rollState`, never
  by `InstrumentTrack::play`/`notePlaysNow`. It reads two plain ints already stored in the note and
  inserts them into a `QJsonObject` (allocation happens on the control thread, where JSON building
  already allocated).
- No other function in the diff is on the render path: `NotePropertiesDialog`,
  `addRecurrence`, `showPassBits`, `passMask`, `addRecurrenceChange`, `changes`, `apply` are Qt GUI
  thread; the three command-file changes only add schema entries; the playback gate
  (`InstrumentTrack::notePlaysNow` → `recursOnPass(MidiPlayback::pass())`) is unchanged.
- `grep` over the added lines for `new`, `malloc`, `lock`, `emit`, `std::` finds nothing in the
  core hunks. TSan does not run on this box; the realtime argument is by design and by the
  call-graph above.

**? Old projects still load and render exactly as before; new data round-trips through save and
load.**
- `recl`/`recm` were already optional attributes written only when `recurLength > 0`
  (`NoteConditions::save`), and absent/invalid recurrence loads neutral
  (`NoteConditions::load`). The dialog writes them only for a note the user changed; a note with
  no recurrence saves with neither attribute. `aRecurrencePairRoundTripsThroughTheProjectFile`
  asserts one `recl=` in the saved clip and 4/5 back after a real project save/load; the neutral
  note in the same clip carries 0/0. The three command-file schema edits are result-schema
  declarations, not format changes. No new serialization key, so no DataFile version change.

**? Qt 5.15 / MSVC / macOS < 10.15 portability: no Qt-6-only API without a guard, no windows.h
macro names, no std::filesystem outside _WIN32.**
- The added GUI code uses `QSpinBox`, `QCheckBox`, `QHBoxLayout`, `QLabel`, `QFormLayout`,
  `QDialogButtonBox`, `QJsonObject`, `QString::arg`, `qOverload` - all present in Qt 5.15 and 6.8.
  No `qsizetype`, `QStringView`, `std::filesystem`, no `windows.h` identifiers, no
  `small`/`near`/`far`/`interface`/`min`/`max` identifiers, no version-gated API. `std::size_t`
  casts follow the file's existing style. The test uses `QPushButton::click()` (Qt 5.15 and 6.8)
  and `QTemporaryDir`/`QDir` (both).

**Sibling commands** — the changed command files are `ControlCommandsNotes.cpp`,
`ControlCommandsNoteProbability.cpp`, `ControlCommandsNoteSlide.cpp`. Every command built on
`control::noteState()` there (`note.add`, `note.remove`, `note.resize`, `note.velocity_set`,
`note.probability_set`, `note.slide_set`) now declares `recur_length`/`recur_mask` in its closed
result schema, so all of them stay consistent with the readback; no command's behaviour or
reversibility changed, and `note.edit`'s own schema (`argsSchema` is `arrayProperty()`) is
unchanged.

## Notes for the reviewer

- The designated log directory `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-242.r1/logs`
  was not writable in this session (`touch` → `Permission denied`; so were `$HOME` and the
  worktree's parent), so the numbered logs cited above live in `/tmp/q050-242-logs/`
  (`build-*.log`, `test-*.log`, `reverted-*.log`). The build also had to pass the bunny guard's
  own `ZENE_BUILD_GUARDED=1` passthrough because its lock file was unwritable; parallelism stayed
  at `-j4`.
- `git status` is clean; the commits are the product change, the tests + feature record, and this
  report.
