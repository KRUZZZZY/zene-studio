FIXED

# Cluster new-p15-22: `note.edit` records an undo step when all requested values are already set

Finding (Luna patrol, BUG 12, `src/core/ControlCommandsNoteEdit.cpp`, historical 343-357): `note.edit` always took a
`MidiClip` checkpoint and reported an edit, even when every requested value already held. `control.undo` then consumed
a step that changed no note, and the real edit before it stayed in place.

## Decision: REAL

Before the fix, `editNotes` called `clip->addJournalCheckPoint()` unconditionally (pre-fix line 346), and `resolveEdits`
never compared the requested values with the notes. The registry records a step for any call that leaves one
(`src/core/ControlRegistry.cpp` `runHandler`, `step != 0`), so a no-op call became an undo step. The reversibility row
for `note.edit` is `TrueInverse` (`src/core/ControlReversibilityTableLive.cpp:214`), so the registry's own rule
(`changedNothing`, `src/core/ControlRegistry.cpp:298-305`) applies once the call leaves neither a step nor a
`__transaction`. The fix uses exactly that path.

## The fix (src/core/ControlCommandsNoteEdit.cpp)

- `conditionsAfter(NoteConditions, v)` (line 296): the W3 fields' pure computation, split out of `applyConditions`
  (which now calls it, same behaviour).
- `struct NoteState` (line 341) plus `stateOf(const Note*)` (line 361): the writable note state, read back from the note.
- `applyCoreTo` (line 371) and `applyFlModelTo` (line 387): the same casts `applyEdit` writes with, applied to a copy.
  Every value is validated first (`checkFieldValue`), and the setters' own ranges (volume 0..200, pan ±100, key 0..127)
  match the validated ranges, so no clamp can move a value. `mpeCaptured` mirrors `setMpeTimbre`/`setMpePressure`,
  which set the captured flag even at 0.
- `editChangesAnyNote(edits)` (line 404): carries each touched note's state through the batch's entries in order and
  reports whether any note ends different from how it started.
- `editNotes` (lines 430-436): when nothing changes, returns success with `edited: 0`, `notes: []`, and no
  `__transaction`. No checkpoint, no write, no re-sort, no mirror.
- Header comment (lines 12-13): one sentence documenting the rule.

The command description and argument/result schemas are unchanged, so `ControlCommandsSnapshot` and the canary are
untouched. No control command was added.

## Regression test

Placed in `tests/src/core/NoteEditRangeTest.cpp` (the closest registered `note.edit` test file; its fixture already builds
the same one-note clip). Three tests:

- `noteEditOfValuesAlreadySetRecordsNoUndoStep` (line 213): set velocity 90, then set 90 again. The second call must report
  `edited` 0, and one `control.undo` must restore the original velocity.
- `aBatchThatSetsAValueBackChangesNothing` (line 232): a batch that sets velocity 80 then 90 on one note must report
  `edited` 0 and leave no step.
- `aZeroTimbreOnAPlainNoteIsStillAChange` (line 251): a guard. Timbre 0 on a note without expression still counts as a
  change (it captures the expression), so the no-change check must not drop it. This test passes with and without the fix.

Determinism: the first two tests call `control.set_undo_coalescing` with `window_ms` 0 and restore the default
(`control::UndoCoalesceWindowMs`) at the end. Coalescing is the only wall-clock element (a 400 ms window), so with it off
nothing depends on timing. No sleeps, no races, no injected clock needed.

Placement note: a new test file was tried first (`tests/src/core/NoteEditNoOpTest.cpp`). It could not be built here:
the lane build tree refuses re-configuration (`configure-1.log`: "bunny-guard: configuring is the parent's job"), so
`cmake --build` had no target for it (`build-7-newtest-green.log`: "No rule to make target 'NoteEditNoOpTest'"). The file
was removed and its registration reverted. The tests also did not go into `NoteEditingCommandsTest.cpp`, which sits
exactly at the file-length ratchet's 500 code-line baseline (`git show HEAD:...` counted 500 with the gate's rule), so any
addition is refused.

## Evidence: commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-22.r1/logs/`.

| Step | Command (run in worktree / build/tests) | EXIT | Log |
|---|---|---|---|
| Build, first attempt | `cmake --build build --target NoteEditingCommandsTest -j4` | 2 | build-1.log (ccache could not write `/run/user/1000/ccache-tmp`) |
| Build, ccache temp in /tmp | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p15-22 cmake --build build --target NoteEditingCommandsTest -j4` | 0 | build-2.log |
| RED, first set (historical) | `ctest -R NoteEditingCommandsTest --output-on-failure` | 8 | test-1-red.log: `edited` Actual 1 (noteEditOfValues...) and Actual 2 (aBatchThat...) |
| Final RED: save product diff | `git diff -- src/core/ControlCommandsNoteEdit.cpp > fix.patch` | 0 | fix.patch (145 lines) |
| Final RED: revert product | `git apply -R fix.patch` | 0 | |
| Final RED: rebuild | `cmake --build build --target NoteEditRangeTest -j4` | 0 | build-9-range-red.log |
| **Final RED: run** | `./NoteEditRangeTest` (in build/tests) | **2** | run-range-red.log: 8 passed, 2 failed. `NoteEditRangeTest.cpp:225` `edited` Actual 1, Expected 0. `NoteEditRangeTest.cpp:244` `edited` Actual 2, Expected 0. The guard passes. |
| Final GREEN: reapply | `git apply fix.patch` | 0 | |
| Final GREEN: rebuild | `cmake --build build --target NoteEditRangeTest -j4` | 0 | build-10-range-green2.log |
| **Final GREEN: run** | `./NoteEditRangeTest` (in build/tests) | **0** | run-range-green2.log: **10 passed**, 0 failed (the 7 pre-existing tests plus 3 new) |
| Earlier GREEN, before the test moved | `./NoteEditingCommandsTest` | 0 | run-green-full.log: 23 passed (historical, that test file has since been reverted to HEAD) |

Affected family (`ctest -R` with the nine targets that contain `note.edit` calls): `test-4-family-final.log`, EXIT 8.
8 of 9 pass. The one failure is `NotePlaybackTest` (see below). Test counts per binary (`family-totals.log`, EXIT 0):

| Binary | Totals |
|---|---|
| NoteEditingCommandsTest | 20 passed, 0 failed |
| NoteEditRangeTest | 10 passed, 0 failed |
| NoteShapeValidationTest | 10 passed, 0 failed |
| NotePlaybackTest | 22 passed, **1 failed** |
| NoteGridTest | 17 passed, 0 failed |
| NoteEditorTest | 33 passed, 0 failed |
| ItemListPanelTest | 18 passed, 0 failed |
| NoteEditorFlTest | 16 passed, 0 failed |
| DevicePanelTest | 19 passed, 0 failed |

`NotePlaybackTest` failure, not caused by this change: `monstroNoteStartDoesNotAllocate` fails at `NotePlaybackTest.cpp:359`
(`instrument->nodeName()` is `dummyinstrument`, expected `monstro`). The `monstro` plugin is not built in this tree
(`find build -iname '*monstro*'` finds only `build/plugins/Monstro/moc_Monstro.cpp_parameters`, no shared object), and
the plugin scan also reports the cache write as "Permission denied". That assertion is the function's first check and runs
before any `note.edit` call (the first `note.edit` in that file is at line 406, in another function). I did not rebuild HEAD
to confirm this as a baseline run.

Gates:

| Command | EXIT | Log |
|---|---|---|
| `python3 .../qwen/feature_check.py <worktree>` | 1 | gates-1.log: `PermissionError` creating `<worktree>-feature-check`, a sibling folder outside the write-jail. The gates never ran. |
| Same gate code, logs redirected: `python3 runs/new-p15-22.r1/logs/feature_check_logsredirect.py <worktree>` | **0** | gates-3.log: fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS ("no new item in your files"), file-length PASS ("no new item in your files"). RESULT: PASS. |

The redirected copy changes only `ROOT` (pinned to the shared qwen directory, so the base-listing cache is read as before)
and the logs directory. Gate logic is unchanged. The base listing for `2da2ed356` was already cached.

Registration: no new files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are unchanged. The edited
files are fork-owned (`ControlCommandsNoteEdit.cpp`, `NoteEditRangeTest.cpp`, both "Zene Studio contributors 2026").

## Observed, not changed (outside this finding; not verified by a test)

- `note.velocity_offset` (`src/core/ControlCommandsNoteTransform.cpp:283`, `:294`) checkpoints only when `changes` is
  true, but always inserts a `__transaction` (with `reversible` true). A call that changes no note therefore records a
  transaction with no journal step. Probably the same class of bug; not in this finding.
- Undo of a timbre or pressure write does not restore the MPE captured flag. `previousValues`
  (`src/core/ControlCommandsNoteEdit.cpp:263`, keep lines at 279-280) stores the values, but the inverse `note.edit`
  calls `setMpeTimbre`/`setMpePressure`, which set `m_mpeCaptured = true` (`src/core/Note.cpp:369`). After such an undo,
  `hasMpeExpression()` stays true. Pre-existing, unchanged by this fix.

## Self-check

Results of `python3 .../qwen/submit_check.py <worktree> new-p15-22` run before the commit (`submit-check-1.log`, EXIT 1):
FAIL "commits on the branch" and "working tree clean", FAIL "report ... missing", FAIL "report has a Self-check section",
FAIL "tests added or changed"; these are the state before this commit.
After the fix commit (`submit-check-2.log`, EXIT 1): PASS commits on the branch, PASS working tree clean, PASS report,
PASS tests added or changed (`tests/src/core/NoteEditRangeTest.cpp`). The one FAIL is `gates (feature_check.py)`, for the
environment reason above (the sibling log folder is outside the write-jail). The gates themselves pass (gates-3.log,
EXIT 0).

1. **Regression test fails with the change reverted and passes with it.** Yes. RED: `run-range-red.log`, EXIT 2, 2 failures
   at `NoteEditRangeTest.cpp:225` and `:244`. GREEN: `run-range-green2.log`, EXIT 0, 10 passed. The timbre guard passes
   both ways by design.
2. **Deterministic.** Yes. The only wall-clock element in the note-edit undo path is the 400 ms coalescing window; both
   undo tests set it to 0 with `control.set_undo_coalescing` and restore the default at the end. No sleeps and no
   races.
3. **Fix at the cause and complete.** `editNotes` is referenced only by its handler registration
   (`ControlCommandsNoteEdit.cpp:494`, `cmd.handler = editNotes`), so there is no second entry into the function. The
   note editor's gestures reach the same command through `m_editor->run("note.edit", ...)`
   (`src/gui/noteeditor/NoteCanvasGestures.cpp:147`, `NoteCanvasInput.cpp:342`, `NoteCanvasAccess.cpp:184`), so the
   fix covers that path too. The no-change check mirrors the writes `applyEdit` makes: `applyCoreTo` and
   `applyFlModelTo` use the same casts, `conditionsAfter` is shared with the write path, and `mpeCaptured` mirrors the
   setters' side effect. Siblings checked: `note.transpose`, `note.velocity_offset` and `note.velocity_scale` checkpoint
   only `if (changes)` (`ControlCommandsNoteTransform.cpp:234`, `:283`, `:322`). Their transaction part is the
   observation above, which I did not change.
4. **No collateral damage.** No file format or saved data changes; the command's arguments and schemas are unchanged.
   The reply changes in one case only: a batch that changes no note now returns `edited: 0` and `notes: []`, where it
   returned the entry count before. A batch that changes at least one note takes the same path as before. No new limit
   was added, so no legitimate caller is refused. The family above passes except the pre-existing `NotePlaybackTest`
   plugin failure.
5. **Verdict** FIXED, so ALREADY-FIXED and FALSE-POSITIVE evidence does not apply.
