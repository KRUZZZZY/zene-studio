# Cluster new-p12-53-port

**Verdict: FIXED (ported).** The control note-selection store held note-list POSITIONS;
the newest line fixed two of the paths that shift those positions (`note.remove` and
`note.move` hand-remapped them), but every other removal path still left the store naming
a different note. The ported fix moves the store to note IDS, so no path can corrupt it.

This is round 2 (Luna's REWORK): the regression moved out of the overlong
`tests/src/core/ControlEditCommandsTest.cpp` into its own test source, the overlong file went
back to its parent's size, and the comments that described position storage as the current
behaviour now describe the pre-fix behaviour.

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
- `src/core/ControlCommandsNoteSelect.cpp:121,133` (this is where the newest line moved the
  `note.select` handler the original commit patched at `ControlCommandsNotes.cpp:468`),
  `src/core/ControlCommandsNoteShape.cpp:423`, `src/core/ControlNoteShared.cpp:55` - call sites.
- `src/core/ControlCommandsNotes.cpp:198-203` - the positional remap in `note.remove` and the
  `selectedNoteIdsSnapshot` / `remapSelectionAfterReorder` pair in `note.move` are gone: with an
  id store they were dead bookkeeping. `note.remove`'s own test above still passes, which is the
  proof that the behaviour those blocks produced is preserved.
- `src/core/ControlCommandsNoteSelect.cpp:51-53` (round 2) - the doc comment of
  `selectedNoteIds()` still claimed the store held positions a reorder invalidates; it now says
  what the store does hold. Comment only, no behaviour change.

## Regression

`tests/src/core/NoteSelectionIdentityTest.cpp:77`
`removingAnEarlierNoteManyTimesKeepsTheSelectionOnTheSameNote` - three notes, select the MIDDLE
one, `note.remove_many` the FIRST, then `roll.get_state` must still report exactly the middle
note's id in `selected_notes`. The test drives only the public command surface
(`track.add`, `clip.add`, `note.add`, `note.select`, `note.remove_many`, `roll.get_state`), so
it compiles against the pre-fix API as well - which is what makes the reverse-patch red proof
possible without touching a single test hunk.

Round 2 moved it here (its own source, 63 code lines) because
`tests/src/core/ControlEditCommandsTest.cpp` was already 511 code lines at the parent commit and
the added test pushed it to 551 - over the 500-line limit the file-length ratchet measures. The
file is back to 511 code lines and 698 physical lines, exactly its parent's size, and its
position-storage comments (`:320`, `:379`) now describe the PRE-FIX behaviour.

| Stage | Result | Log |
|---|---|---|
| RED (product fix absent) | `NoteSelectionIdentityTest` FAILED - `note "note-10"` reported where `"note-8"` was selected; `ControlEditCommandsTest` passed (17 slots) | `test-red-3.log` |
| RED, whole tree builds pre-fix | only `NoteSelectionIdentityTest` failed of 3 targets | `test-red-2.log` |
| GREEN (fix present) | 3/3 ctest targets | `test-5.log` |
| Wider family | 10/10 ctest targets | `test-4.log` |

Affected family: `NoteSelectionIdentityTest` (1 slot) + `ControlEditCommandsTest` (17 slots) +
`NoteShapeValidationTest`; the wider run adds `ControlNoteScaleVerbsTest`,
`ControlNoteExpressionCommandsTest`, `NoteEditingCommandsTest`, `NoteToolsTest`,
`ControlVerbInverseTest`, `ReversibilityContractTest`, `ReversibilityUndoTest` - all passing.

## Commands and exit codes

All output is under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-53-port.r2/logs/`.
`$LOGS` is that folder. The round-1 run (red proof of the same fix, in the r1 log folder) is
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-53-port.r1/logs/`.

| Stage | Exact command (run from the worktree root unless noted) | Exit | Log |
|---|---|---:|---|
| Build the new test target | `cmake --build build --target zene ControlEditCommandsTest NoteShapeValidationTest NoteSelectionIdentityTest -j12 > $LOGS/build-2.log 2>&1` | 0 | `build-2.log` |
| GREEN test (`build/tests`) | `ctest -R 'NoteSelectionIdentityTest\|ControlEditCommandsTest\|NoteShapeValidationTest' --output-on-failure > $LOGS/test-1.log 2>&1` | 0 (3/3) | `test-1.log` |
| Save + reverse the PRODUCT patch | `git diff ded355440 -- include/ControlEdit.h src/core/ControlCommandsNoteSelect.cpp src/core/ControlCommandsNoteShape.cpp src/core/ControlCommandsNotes.cpp src/core/ControlEditSupport.cpp src/core/ControlNoteShared.cpp > $LOGS/fix.patch && git apply -R $LOGS/fix.patch` | 0 (diff) / 0 (apply) | `fix.patch` |
| Reverse-patch build | `cmake --build build --target zene ControlEditCommandsTest NoteSelectionIdentityTest -j12 > $LOGS/build-red-3.log 2>&1` | 0 | `build-red-3.log` |
| Reverse-patch test (`build/tests`) | `ctest -R 'NoteSelectionIdentityTest\|ControlEditCommandsTest' --output-on-failure > $LOGS/test-red-3.log 2>&1` | **8** (1 of 2 failed) | `test-red-3.log` |
| Restore the patch | `git apply $LOGS/fix.patch` | 0 | `fix.patch` |
| Restored build | `cmake --build build --target zene ControlEditCommandsTest NoteShapeValidationTest NoteSelectionIdentityTest -j12 > $LOGS/build-5.log 2>&1` | 0 | `build-5.log` |
| Restored test (`build/tests`) | `ctest -R 'NoteSelectionIdentityTest\|ControlEditCommandsTest\|NoteShapeValidationTest' --output-on-failure > $LOGS/test-5.log 2>&1` | 0 (3/3) | `test-5.log` |
| Wider family (`build/tests`) | `ctest -R 'ControlNoteScaleVerbsTest\|ControlNoteExpressionCommandsTest\|NoteEditingCommandsTest\|ReversibilityContractTest\|ReversibilityUndoTest\|ControlVerbInverseTest\|NoteToolsTest\|NoteSelectionIdentityTest\|ControlEditCommandsTest\|NoteShapeValidationTest' --output-on-failure > $LOGS/test-4.log 2>&1` | 0 (10/10) | `test-4.log` |

The whole-tree variant of the red proof: the product patch AND the one-line call-site update in
`tests/src/core/NoteShapeValidationTest.cpp` (which uses the new signature) were reversed, so all
three targets build against the pre-fix product code -
`git apply -R $LOGS/testcall-site.patch` (exit 0),
`cmake --build build --target zene ControlEditCommandsTest NoteShapeValidationTest NoteSelectionIdentityTest -j12 > $LOGS/build-red-2.log 2>&1` (exit 0),
`ctest -R 'NoteSelectionIdentityTest\|ControlEditCommandsTest\|NoteShapeValidationTest' --output-on-failure > $LOGS/test-red-2.log 2>&1` (exit **8**, only
`NoteSelectionIdentityTest` failed). Both patches were re-applied afterwards
(`git apply $LOGS/testcall-site.patch` exit 0, `git apply $LOGS/fix.patch` exit 0).

## Gates

Both required ratchets are RED on the train by design and stay red: every item they list is in a
file this change did not touch, and the one file this change did touch
(`tests/src/core/ControlEditCommandsTest.cpp`) is listed at **511** - its parent's size - instead
of the 551 the round-1 attempt produced. No limit and no baseline was touched.

| Gate | Command | Exit | Note |
|---|---|---:|---|
| complexity | `bash tests/complexity-gate.sh --check` | 1 | 6 items, all in untouched files (`XyPadPairTest`, `DawProjectReadTracks`, `ControlCommandsMixerTools`, `ControlCommandsMixerStrip`, `ProjectRevisions`, `ControlCommandsShortcuts`) |
| file length | `bash tests/file-length-gate.sh --check` | 1 | 14 items, the same 14 as the base tree; `ControlEditCommandsTest.cpp` at 511 (parent size), the new test file is not listed |
| fork sources | `bash tests/fork-sources-gate.sh` | 0 | the new test source is registered in `tests/fork-sources.txt` and `tests/all-sources.txt` |
| unregistered tests | `bash tests/unregistered-tests-gate.sh` | 0 | the new test source is registered in `tests/CMakeLists.txt` |
| feature check | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** | 8/8 rows PASS, `RESULT: PASS` |

Logs: `gate-complexity-final.log`, `gate-file-length-final.log`, `gate-fork-sources-final.log`,
`gate-unregistered-final.log`, `gate-feature-check-final.log`.

## Commit payload

- `include/ControlEdit.h`
- `src/core/ControlEditSupport.cpp`
- `src/core/ControlCommandsNoteSelect.cpp`
- `src/core/ControlCommandsNoteShape.cpp`
- `src/core/ControlCommandsNotes.cpp`
- `src/core/ControlNoteShared.cpp`
- `tests/src/core/NoteSelectionIdentityTest.cpp` (new: the regression, 63 code lines)
- `tests/src/core/ControlEditCommandsTest.cpp` (the regression moved out; the position-storage
  comments now describe the pre-fix behaviour)
- `tests/src/core/NoteShapeValidationTest.cpp` (call-site update for the new signature)
- `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt` (the new test source)
- `docs/qwen-fixes-2026-10/cluster-new-p12-53-port.md`

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  RED: `git apply -R $LOGS/fix.patch` (exit 0) + `cmake --build build --target zene ControlEditCommandsTest NoteSelectionIdentityTest -j12` (exit 0) +
  `ctest -R 'NoteSelectionIdentityTest|ControlEditCommandsTest' --output-on-failure` from
  `build/tests` -> exit **8**, `NoteSelectionIdentityTest` FAILED with
  `'selectedNotes.first().toString() == ids.at(1)' returned FALSE. (after removing the first note
  the selection reported note "note-10", but the note that WAS selected is "note-8" - the
  selection landed on a note the client never selected)`, while `ControlEditCommandsTest` passed.
  GREEN: `git apply $LOGS/fix.patch` (exit 0) + the same build (exit 0) +
  `ctest -R 'NoteSelectionIdentityTest|ControlEditCommandsTest|NoteShapeValidationTest'` ->
  exit **0**, 3/3 passed. The wider family (10 targets) -> exit **0**.
- **The test is deterministic: no wall-clock timing, sleeps or races.** The test drives the
  control command surface only: `track.add`, `clip.add`, three `note.add`s at fixed positions
  (0, 24, 48 ticks), `note.select` with one note id, `note.remove_many` with one note id, then
  `roll.get_state`. No clock, no timer, no sleep, no thread, no fixture that depends on the
  machine's speed; the assertions read the ids the commands themselves returned. The note ids
  are compared as strings, never as indices into a list whose order the test does not pin.
- **The fix is at the cause and complete.** The cause is the STORE's payload: it held
  note-list positions, and every caller that shifted the list had to remap it by hand - which
  only `note.remove` and `note.move` did. The fix changes the store to hold `Note::id()` values
  (`src/core/ControlEditSupport.cpp:252`), so the remapping disappears instead of being
  extended. Every caller of the three changed functions was checked and updated:
  `control::selectNotes` -> `src/core/ControlCommandsNoteSelect.cpp:121` (`note.select`),
  `src/core/ControlCommandsNoteShape.cpp:423` (`note.select_where`);
  `control::selectedNoteIndices` -> `src/core/ControlCommandsNoteSelect.cpp:133`,
  `src/core/ControlNoteShared.cpp:55` (`scopeNotes`, the `scope=selection` verbs),
  `src/core/ControlEditSupport.cpp` (`rollState`);
  `control::noteSelected` -> `src/core/ControlEditSupport.cpp:374` (`rollState`).
  `grep -rn "selectNotes\|noteSelected\|selectedNoteIndices" src include plugins tests` lists
  no other call site (the GUI's `PianoRoll::selectNotesOnKey` is a different function). The
  declaration (`include/ControlEdit.h:133-135`) and every definition were changed together, and
  every target that includes the header was rebuilt.
- **No collateral damage.** The selection is view state and is not serialized, so no saved
  project, no `.mmp` and no old session changes: the store is a process-global that starts empty
  (`src/core/ControlEditSupport.cpp:248-252`). No limit or threshold was added, so no caller can
  legitimately hit one. The two hand-remap blocks that were deleted
  (`selectedNoteIdsSnapshot`, `remapSelectionAfterReorder` in `src/core/ControlCommandsNotes.cpp`)
  only ever rewrote the store, which the id store makes unnecessary - and their own regressions
  (`aNoteRemovalDoesNotShiftTheSelectionOntoAnotherNote`, `aNoteMoveDoesNotShiftTheSelectionOntoAnotherNote`)
  still pass, which is the proof that the behaviour they produced is preserved. No control
  command was added, so the command surface (and its snapshot canary) is unchanged.
- **If the verdict is ALREADY-FIXED or FALSE-POSITIVE: the code that shows it.** Not applicable -
  the verdict is FIXED. The pre-fix code that proved the defect is in the round-1 log folder's
  `fix.patch` and in the git history of commit `d076861f2` (its parent `ded355440` has
  `QVector<int> s_selectedNotes` and `void selectNotes(const QString& clip, int noteCount, const
  QVector<int>& indices)` in `src/core/ControlEditSupport.cpp`).
- **Sibling commands in the command files you changed - confirm each is consistent with your fix.**
  - `roll.get_state` (`src/core/ControlEditSupport.cpp:371-393`): reports the selection by
    mapping the stored ids to the clip's current positions, so it names the selected NOTE and
    not the position it used to sit in; a removed note is simply not reported.
  - `note.select` (`src/core/ControlCommandsNoteSelect.cpp:80-147`): writes the id store and
    reports the selected ids in the clip's current note order; it also sets the notes' own
    selection flags, which travel with the notes.
  - `note.select_where` (`src/core/ControlCommandsNoteShape.cpp:417-423`): writes the same id
    store, so it agrees with `note.select` (asserted by
    `tests/src/core/NoteShapeValidationTest.cpp:283`).
  - `note.remove` (`src/core/ControlCommandsNotes.cpp:163-213`): no remap any more; the removed
    note's id leaves the store's mapping by simply not being in the clip.
  - `note.move` (`src/core/ControlCommandsNotes.cpp:219-310`): the re-sort moves notes, the ids
    move with them, so the selection follows the notes.
  - `note.add`, `note.resize`, `note.velocity_set`, `note.edit` (same file): they add or change
    notes; the ids of the notes already in the clip do not change, and a note added after the
    selection was made was never selected, so the selection is untouched.
  - `note.remove_many`, `note.slice` and the clipboard verbs
    (`src/core/ControlCommandsNoteClipboard.cpp`): they shift the list and write nothing back to
    the store - which is exactly the defect this fix removes; the new regression covers
    `note.remove_many`.
  - The `scope=selection` verbs (`src/core/ControlNoteShared.cpp:55`): they read the selection
    through the same id-to-position mapping, so they act on the notes the client selected.
