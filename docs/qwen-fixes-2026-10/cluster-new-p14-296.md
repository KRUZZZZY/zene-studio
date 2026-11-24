FIXED

# cluster new-p14-296: a refused script can still destroy existing undo history

Verdict: REAL, fixed at the cause.

## Evidence (before the fix)

- `src/core/ProjectJournalStructural.cpp:67-83` (`addJournalStructure`): every step a script's commands push
  clears the redo stack (`m_redoCheckPoints.clear()`) and calls `trimUndoStack()`, which evicts the oldest
  undo steps once the byte budget is reached.
- `src/core/SessionCheckpoint.cpp` `finishScriptCheckpoint` (pre-fix lines 239 and 254): on refusal (the
  snapshot pair is over budget, or the result could not be captured), it restored the project and only
  `discardTopSteps(retainedStepsFrom(serialBefore + 1))` dropped the script's own steps. The redo steps that
  were cleared and the older steps that were evicted were never put back. On the success path (pre-fix
  line 262), older steps evicted while the script ran were also lost under the one checkpoint step.
- Sibling, same function family: `pushLoadedSessionForRedo` (project.open refusal after the load). The load
  runs `Song::clearProject`, which calls `clearJournal()` (`src/core/Song.cpp:1888`). The refusal restored
  the displaced session but left the journal empty.
- RED run (`test-1.log`, and the reverse-patch run `test-4.log`):
  - `aRefusedScriptKeepsTheExistingUndoAndRedoSteps` failed at `SessionCheckpointBudgetTest.cpp(162)`:
    `after.redoDepth` was 0, expected 1.
  - `aRefusedOpenKeepsTheDisplacedSessionsUndoAndRedoSteps` failed at `SessionCheckpointBudgetTest.cpp(161)`:
    `after.undoDepth` was 0, expected 1.

## Fix

- `include/SessionCheckpoint.h`: `SessionCheckpoint::journalBefore` is a `shared_ptr` to the opaque
  `SessionJournal`, which holds the journal history as it was when the checkpoint was taken.
- `src/core/SessionCheckpoint.cpp`:
  - `takeSessionCheckpoint` stores a copy of the history (`holdJournal`) when the checkpoint is Taken. It uses
    the existing `ProjectJournal::detachHistory`/`reattachHistory`. The stacks are implicitly shared, so the
    live journal detaches from the held copy on its next write.
  - `finishScriptCheckpoint`: both refusal paths put the held history back (`dropScriptSteps`). This drops
    the script's steps and returns the cleared redo steps and the evicted older steps. On success, the one
    checkpoint step is pushed onto the held history, so the commit is atomic: the history beneath it is
    exactly the pre-script history, and the push clears redo as any new edit does.
  - `pushLoadedSessionForRedo` (project.open): both refusal paths restore the session and the held history
    (`rollBack`).
  - Without a held history (a checkpoint that was not Taken never reaches these paths), the old
    `discardTopSteps` behaviour is kept as the fallback.
- Unchanged:
  - The `!changed` path, where a script made no net change, leaves the journal as it did before.
  - The step serial counter is not rewound, so serials stay unique.
  - Nothing runs on the audio thread: the journal is GUI/control-side only (`include/ProjectJournal.h`
    class comment).

## Test matrix

- Normal path (a script/open that fits): the existing `SessionCheckpointTest`, `ScriptDawRemovalUndoTest`,
  `ProjectOpenIntegrityTest` and `ControlRenderJobTest` still pass (`test-3.log`).
- Failure/exhaustion (refused over the byte budget): two new tests in
  `tests/src/core/SessionCheckpointBudgetTest.cpp`, one for script.run and one for project.open. Each checks
  undo depth, redo depth, evicted count, retained bytes and track names, then shows that `redo()` still
  restores the undone track.
- Reconfiguration: the budget is set with `setMaxUndoBytes` before the run, and `init()` restores it.
- Audio thread: not touched. Platform: only standard C++ and existing Qt containers, no new Qt API.

## Commands (EXIT codes copied from the logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p14-296.r1/logs)

| Step | Command | EXIT |
|---|---|---|
| RED build | `cmake --build build --target zene SessionCheckpointBudgetTest -j4 > logs/build-1.log 2>&1` | 0 |
| RED test | `cd build/tests && ctest -R SessionCheckpointBudgetTest --output-on-failure > logs/test-1.log 2>&1` | 8 |
| full build (refused by guard) | `cmake --build build -j4 > logs/build-2.log 2>&1` | 97 ("build a named target") |
| GREEN build | `cmake --build build -j4 --target zene SessionCheckpointBudgetTest SessionCheckpointTest ScriptDawRemovalUndoTest ProjectOpenIntegrityTest ControlRenderJobTest > logs/build-3.log 2>&1` | 0 |
| GREEN test | `ctest -R SessionCheckpointBudgetTest --output-on-failure > logs/test-2.log 2>&1` | 0 |
| family | `ctest -R "SessionCheckpoint\|ScriptDawRemovalUndo\|ProjectOpenIntegrity\|ControlRenderJob" --output-on-failure > logs/test-3.log 2>&1` | 0 (5/5) |
| 4b patch | `git diff -- include/SessionCheckpoint.h src/core/SessionCheckpoint.cpp > logs/fix.patch` | 0 |
| 4b revert | `git apply -R logs/fix.patch` | 0 |
| 4b build | `cmake --build build -j4 --target zene SessionCheckpointBudgetTest > logs/build-4.log 2>&1` | 0 |
| 4b RED | `ctest -R SessionCheckpointBudgetTest --output-on-failure > logs/test-4.log 2>&1` | 8 |
| 4b reapply | `git apply logs/fix.patch` | 0 |
| 4b build | `cmake --build build -j4 --target zene SessionCheckpointBudgetTest > logs/build-5.log 2>&1` | 0 |
| 4b GREEN | `ctest -R SessionCheckpointBudgetTest --output-on-failure > logs/test-5.log 2>&1` | 0 |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-1.log 2>&1` | 0 (RESULT: PASS, all 8 rows PASS) |

The header `include/SessionCheckpoint.h` is included only by `zene` sources (`SessionCheckpoint.cpp`,
`ControlCommandsScript.cpp`, `ControlCommandsProjectFiles.cpp`, `src/gui/MainWindow.cpp`). All of them were
rebuilt with the `zene` target. A full build without a target is refused by the session guard (exit 97).

Registration: all three changed files are fork files already listed in `tests/fork-sources.txt`. There is no
new file and no upstream file was changed. The fork-sources gate passes.

Delegation:
- Sonnet wrote the two regression tests and ran the RED build and test.
- Haiku: one run was refused by the full-build guard. A second run built the named targets and ran the test
  family (GREEN). A third ran the 4b reverse-patch RED/GREEN sequence.
- I did the diagnosis, the design, the product fix, the review, the gates, the report and the commit.

## Other findings

- `finishScriptCheckpoint`'s `!changed` path, where a script made no net change, still leaves the script's
  no-op steps on the stack, and their pushes have cleared redo. This is not in this finding's scope and was
  left unchanged.
- `loadProjectWithCheckpoint` treats a load with a non-empty `loadRefusal()` as "ready" without a rollback.
  If such a load clears the journal before refusing, the displaced history would be lost there too. Not
  verified this session.

## Self-check

- **The test fails without the fix and passes with it.**
  - `git apply -R logs/fix.patch` returned EXIT 0. After rebuilding `zene` and `SessionCheckpointBudgetTest`
    (EXIT 0, `build-4.log`), `ctest -R SessionCheckpointBudgetTest` returned EXIT 8 (`test-4.log`). It failed
    at line 162 (redoDepth 0 vs 1) and line 161 (undoDepth 0 vs 1).
  - `git apply logs/fix.patch` returned EXIT 0. After the rebuild (EXIT 0, `build-5.log`), the same ctest
    returned EXIT 0 (`test-5.log`).
- **The test is deterministic.**
  - There is no timing, sleep or thread. Each test drives the synchronous control registry
    (`ControlRegistry::invoke`) and `ProjectJournal::undo()`/`redo()` on the test thread.
  - The refusal is forced by an injected byte budget: `setMaxUndoBytes(captureUndoSnapshot()->size() + 512)`.
  - The payload is fixed: 24 tracks whose 512-character names come from a fixed LCG (`longTrackName`).
- **The fix is at the cause and complete.**
  - The cause is that `SessionCheckpoint.cpp` threw away journal state it never held. Every caller of the
    changed functions was checked:
    - `takeSessionCheckpoint`: `ControlCommandsScript.cpp:308` (script.run) and `SessionCheckpoint.cpp:253`.
    - `finishScriptCheckpoint`: `ControlCommandsScript.cpp:330`.
    - `loadProjectWithCheckpoint`: `ControlCommandsProjectFiles.cpp:647` (project.open) and
      `src/gui/MainWindow.cpp:999` (the GUI open path, same function, so it is covered by the open fix).
  - Both rollback paths of each command (capture failed, pair over budget) now restore the held history.
  - The script success path pushes onto the held history.
  - The header has no other includers (searched for `SessionCheckpoint.h`), and the `zene` target rebuilt
    all of them.
- **No collateral damage.**
  - No limit or check was added, and no file format, load or save path was changed.
  - The successful open path is unchanged: the held copy is unused there, and the opened project's history
    still replaces the displaced one.
  - The script `!changed` path is unchanged.
  - The step serial counter is not rewound, so transaction records cannot alias a later step.
  - `SessionCheckpointTest`, `ScriptDawRemovalUndoTest`, `ProjectOpenIntegrityTest` and `ControlRenderJobTest`
    still pass (`test-3.log`, 5/5, EXIT 0).
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable. The verdict is FIXED; the RED runs above show
  the bug.
- **Sibling commands listed by the check:** `app.*` and `arrangement.fit_tracks`/`arrangement.get_state` are
  not affected. They come from the branch-wide diff against the base, not from this commit, which changes
  no command file. The only commands that reach the changed code are script.run and project.open (plus the
  GUI open path), and all are covered above.
- **Audio thread:** not involved. `ProjectJournal` and the checkpoint run on the GUI/control thread only
  (the `include/ProjectJournal.h` class comment: "Neither the bounds nor the merge touch the audio thread").
  Local TSan does not run on this box, so this is a design argument, not a TSan result.
