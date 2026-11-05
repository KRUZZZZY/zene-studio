IMPLEMENTED

# Q050-242 — Pattern recurrence controls

Branch `feat/050-Q050-242-oc`, cut from `050/m0-train-24` (`2a7fd9f9c7`). Product commit
`d9c4902759`, test/docs commit `bd48d406da`.

The recurrence model (`NoteConditions::recursOnPass`, `recl`/`recm`, `note.edit`'s
`recur_length`/`recur_mask`) and the playback gate already existed. This unit exposes the pair in
the note properties box, publishes it through `roll.get_state`, and proves the pass contract
through a locally looped clip.

## Acceptance

| # | Criterion | Test / command | Proof | Exit |
|---|---|---|---|---|
| 1 | The editor exposes recurrence length and pass mask and reads back their saved values | `cd build/tests && ctest -R "^NoteEditorFlTest$" --output-on-failure` (log `test-16.log`) | `thePropertiesBoxExposesThePassPatternAndReadsItBack`: a note carrying 2/1 opens with `recur_length` 2, pass 1 ticked, pass 2 unticked and pass 3 hidden; ticking pass 3 at length 4 makes `changes()` the pair `{recur_length: 4, recur_mask: 4}`; after OK the note carries 4/4 and `recursOnPass` plays only pass 2; **one** `control.undo` restores 2/1 and a box opened again shows 2/1; length 0 greys the bits and reports mask 0. `aDoubleClickOpensTheNotesPropertiesAndOkIsOneEdit` asserts both controls on the double-click's box. | 0 |
| 1 | ...and the saved values survive a save/reload | `ctest -R "^NotePlaybackTest$"` | `aRecurrencePairRoundTripsThroughTheProjectFile`: the clip element holds exactly one `recl="4"`/`recm="5"`; `saveProjectFile` + `project.new` + `loadProject` reloads 4/5, the note recurs on passes 0 and 2 only, the neutral note writes no recurrence attribute, and `roll.get_state` reports the reloaded pair. | 0 |
| 2 | Playback tests verify notes sound only on the selected passes, including looped clips | `ctest -R "^NotePlaybackTest$"` | `recurrenceSelectsThePassesOfALocallyLoopedClip`: a 96-tick clip looped every 24 ticks renders at 999 BPM over five song loop wraps; with length 2 / mask 1 the note starts only on passes 0, 2 and 4 and never on an odd pass, although the clip keeps folding within each pass; length 2 / mask 0 sounds no pass; length 0 / mask 0 sounds again. The pair is written by `note.edit` and read back from `roll.get_state` first. | 0 |
| 3 | A control-socket edit and undo restore the original recurrence settings | `cd build/tests && ctest -R "^ControlNoteEditor$" -V` (log `test-16.log`) | `check_recurrence` in `tests/control-note-editor.py`: a fresh note reads `recur_length` 0 / `recur_mask` 0; a mask without a length is a typed `invalid_args`; `note.edit` writes 2/1 and `roll.get_state` reads both back; length 17 is refused; **one** `control.undo` restores 0/0. | 0 |
| — | No regression to the command contract, undo classes or snapshot | `ctest -R "^(ReversibilityContractTest|ControlCommandsSnapshot)$"` | no new command or row (878 ids / 77 groups, A16 histogram rows=887 unchanged); snapshot regenerated from a live instance. | 0 |

### Reverted-product evidence

| Test | Product reverted | Exit |
|---|---|---|
| `NoteEditorFlTest::thePropertiesBoxExposesThePassPatternAndReadsItBack` | all six product hunks reversed, rebuilt | 1 (`length != nullptr`) |
| `NotePlaybackTest::aRecurrencePairRoundTripsThroughTheProjectFile` and `::recurrenceSelectsThePassesOfALocallyLoopedClip` | only `src/core/ControlEditSupport.cpp` reversed (readback removed), rebuilt | 1 and 1 (`recur_length` mismatch) |
| `ControlNoteEditor` | all six product hunks reversed, rebuilt | 8 (readback and undo checks FAILED) |

Restored afterwards; the final full run (`test-16.log`) is 5/5 passed, exit 0.

## Self-check

(answers to every ASK item from submit_check.py — filled below)
