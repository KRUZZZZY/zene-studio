# Cluster new-p12-53-port

**Verdict: FIXED (ported).** The control note-selection store held note-list POSITIONS;
the newest line fixed two of the paths that shift those positions (`note.remove` and
`note.move` hand-remapped them), but every other removal path still left the store naming
a different note. The ported fix moves the store to note IDS, so no path can corrupt it.

## What the newest line already had

- `src/core/ControlCommandsNotes.cpp` (`note.remove`) remapped the stored positions after its
  own shift, and `note.move` did the same after its re-sort
  (`remapSelectionAfterReorder`, deleted by this fix - the ids need no remap).
- `tests/src/core/ControlEditCommandsTest.cpp:327` (`aNoteRemovalDoesNotShiftTheSelectionOntoAnotherNote`)
  and `:385` (`aNoteMoveDoesNotShiftTheSelectionOntoAnotherNote`) already cover those two verbs.

So the finding's literal `note.remove` repro is covered upstream; the defect behind it is not:
`note.remove_many` (`src/core/ControlCommandsNoteClipboard.cpp:98-109`) and every other removal
that shifts the list (`note.slice`, the clipboard verbs, `note.move_to_track`) wrote nothing back
to the store, so a selected middle note silently became a selected last note.

## The fix

- `include/ControlEdit.h:133-135` - the three selection functions take the `MidiClip` they speak
  about instead of a bare note count.
- `src/core/ControlEditSupport.cpp:266-297` - `s_selectedNotes` (positions) becomes
  `s_selectedNoteIds` (`Note::id()` values, `SPEC-stable-ids.md` slice 2). `selectedNoteIndices()`
  maps ids to the clip's CURRENT positions on every query; `noteSelected()` tests the id at an
  index. A note removed since the selection simply is not in the clip and is not reported.
- `src/core/ControlEditSupport.cpp:374,383` - `rollState` reports through the mapped selection.
- `src/core/ControlCommandsNoteSelect.cpp:122,134` (this is where the newest line moved the
  `note.select` handler the original commit patched at `ControlCommandsNotes.cpp:468`),
  `src/core/ControlCommandsNoteShape.cpp:423`, `src/core/ControlNoteShared.cpp:55` - call sites.
- `src/core/ControlCommandsNotes.cpp:198-203` - the positional remap in `note.remove` and the
  `selectedNoteIdsSnapshot` / `remapSelectionAfterReorder` pair in `note.move` are gone: with an
  id store they were dead bookkeeping. `note.remove`'s own test above still passes, which is the
  proof that the behaviour those blocks produced is preserved.

## Regression

`tests/src/core/ControlEditCommandsTest.cpp:444`
`removingAnEarlierNoteManyTimesKeepsTheSelectionOnTheSameNote` - three notes, select the MIDDLE
one, `note.remove_many` the FIRST, then `roll.get_state` must still report exactly the middle
note's id in `selected_notes`.

| Stage | Result | Log |
|---|---|---|
| RED (fix absent) | `Totals: 17 passed, 1 failed` - `note "note-37"` reported where `"note-35"` was selected | `test-1-red.log` |
| GREEN (fix present) | 2/2 ctest targets; 16/16 in the wider family | `test-4-restored.log`, `test-6-final.log` |
| Reverse-patch RED | same assertion fails, other 17 slots pass | `test-3-unfixed.log` |

Affected family: `ControlEditCommandsTest` (18 test slots) + `NoteShapeValidationTest`; the wider
note/clip/control run covers 16 ctest targets, all passing.

## Commands and exit codes

All output is under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-53-port.r1/logs/`.

| Stage | Exact command (run from the worktree root unless noted) | Exit | Log |
|---|---|---:|---|
| RED build | `cmake --build build --target ControlEditCommandsTest > $LOGS/build-1-red.log 2>&1` | 0 | `build-1-red.log` |
| RED test (`build/tests`) | `ctest -R ControlEditCommandsTest --output-on-failure > $LOGS/test-1-red.log 2>&1` | 8 | `test-1-red.log` |
| GREEN build | `cmake --build build --target zene ControlEditCommandsTest NoteShapeValidationTest > $LOGS/build-2-green.log 2>&1` | 0 | `build-2-green.log` |
| GREEN test (`build/tests`) | `ctest -R "ControlEditCommandsTest\|NoteShapeValidationTest" --output-on-failure > $LOGS/test-2-green.log 2>&1` | 0 | `test-2-green.log` |
| Save + reverse the product patch | `git diff -- include/ControlEdit.h src/core/ControlEditSupport.cpp src/core/ControlCommandsNoteSelect.cpp src/core/ControlCommandsNoteShape.cpp src/core/ControlCommandsNotes.cpp src/core/ControlNoteShared.cpp > $LOGS/fix.patch && git apply -R $LOGS/fix.patch` | 0 | `fix.patch` |
| Reverse-patch build | `cmake --build build --target ControlEditCommandsTest > $LOGS/build-3-unfixed.log 2>&1` | 0 | `build-3-unfixed.log` |
| Reverse-patch test (`build/tests`) | `ctest -R ControlEditCommandsTest --output-on-failure > $LOGS/test-3-unfixed.log 2>&1` | 8 | `test-3-unfixed.log` |
| Restore the patch | `git apply $LOGS/fix.patch` | 0 | `fix.patch` |
| Restored build | `cmake --build build --target zene ControlEditCommandsTest NoteShapeValidationTest > $LOGS/build-4-restored.log 2>&1` | 0 | `build-4-restored.log` |
| Restored test (`build/tests`) | `ctest -R "ControlEditCommandsTest\|NoteShapeValidationTest" --output-on-failure > $LOGS/test-4-restored.log 2>&1` | 0 | `test-4-restored.log` |
| Header dependents | `cmake --build build --target ControlPluginBusesTest ControlVerbInverseTest UndoHonestyTest ClipLinkTest LinkedClipEditsTest LinkedClipGroupEditsTest TrackFolderTest ControlSampleOperatorTest > $LOGS/build-6-dependents.log 2>&1` | 0 | `build-6-dependents.log` |
| Header dependents 2 | `cmake --build build --target ClipConsolidateTest ClipImportFileTest ControlBounceRenderTest ControlVcaLockedEditsTest MidiReconnectTest ProjectToolsTest TrackFolderTest UndoHonestyStateTest NoteShapeValidationTest > $LOGS/build-7-dependents2.log 2>&1` | 0 | `build-7-dependents2.log` |
| Wider family (`build/tests`) | `ctest -R "ControlEditCommandsTest\|NoteShapeValidationTest\|ControlPluginBusesTest\|ControlVerbInverseTest\|UndoHonestyTest\|ClipLinkTest\|LinkedClipEditsTest\|LinkedClipGroupEditsTest\|ControlSampleOperatorTest\|ClipConsolidateTest\|ClipImportFileTest\|ControlBounceRenderTest\|ControlVcaLockedEditsTest\|MidiReconnectTest\|ProjectToolsTest\|UndoHonestyStateTest" --output-on-failure > $LOGS/test-6-final.log 2>&1` | 0 (16/16) | `test-6-final.log` |

## Gates - both already RED on the pristine branch, unchanged by this fix

Both required ratchets fail on this branch BEFORE any edit. The same regression set was run
against a pristine `git archive HEAD` checkout in `/tmp/opencode/pristine-p12-53` and is byte
identical to the post-fix run, so this port adds no gate entry (no limit or baseline touched).

| Gate | Command | Exit (post-fix) | Exit (pristine HEAD) |
|---|---|---:|---:|
| complexity | `bash tests/complexity-gate.sh --check` | 1 | 1 (same 6 REGRESSION lines) |
| file length | `bash tests/file-length-gate.sh --check` | 1 | 1 (same 14 REGRESSION lines) |

Logs: `gate-1-complexity.log` / `gate-3-complexity-pristine.log` and
`gate-2-file-length.log` / `gate-4-file-length-pristine.log`.
`tests/src/core/ControlEditCommandsTest.cpp` was already over the 500-line limit at HEAD
(511 counted lines) and is still listed after this fix (551); every other listed file is untouched
by this change.

Other gates run and green: `bash tests/fork-sources-gate.sh` (exit 0, `gate-6-fork-sources.log` -
no new product file, every changed file is already registered in `tests/fork-sources.txt`) and
`bash tests/no-upstream-regression-gate.sh` (exit 0, `gate-5-upstream.log`).

## Commit payload

- `include/ControlEdit.h`
- `src/core/ControlEditSupport.cpp`
- `src/core/ControlCommandsNoteSelect.cpp`
- `src/core/ControlCommandsNoteShape.cpp`
- `src/core/ControlCommandsNotes.cpp`
- `src/core/ControlNoteShared.cpp`
- `tests/src/core/ControlEditCommandsTest.cpp`
- `tests/src/core/NoteShapeValidationTest.cpp` (call-site update for the new signature)
- `docs/qwen-fixes-2026-10/cluster-new-p12-53-port.md`