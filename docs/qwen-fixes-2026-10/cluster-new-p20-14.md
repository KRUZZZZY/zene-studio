FALSE-POSITIVE

Cluster new-p20-14: "Undoing a section edit or landed take can leave the project marked clean".

## Verdict

The finding is literally right about the closures and wrong about the consequence. The section closure
(`src/core/ControlCommandsSections.cpp:82`) and the landed-take closure
(`src/core/ControlCommandsSessionAudioRecord.cpp:241-242`) do not call `setModified()` themselves. But those
closures run only from `ProjectJournal::undo()` and `ProjectJournal::redo()`, and both of those mark the song
modified after the closures run. So an undo or redo that applies a section or take change leaves the project
dirty, and the failure in the finding (save, undo, close without a prompt) cannot happen on this branch.

No product code was changed. No regression test was added, because the procedure's RED/GREEN step applies only
to a real defect.

## Evidence (file:line, branch `bugs/qwen-new-p20-14`, base `37eaf07557`)

Where the closures are registered (the claim):
- `src/core/ControlCommandsSections.cpp:82` `addUndoStep([before]{...restore(before);}, [after]{...restore(after);});`
  then `:83` `Engine::getSong()->setModified();`, i.e. the caller sets the flag after registering.
- `src/core/ControlCommandsSessionAudioRecord.cpp:241-243`: the same shape for `session.record_slot_land`.
- `src/core/ControlReversibility.cpp:147-151`: `addUndoStep` forwards to `ProjectJournal::addJournalAction`.
- `src/core/ProjectJournal.cpp:277-288`: `addJournalAction` pushes the pair onto `m_undoCheckPoints`.

Where the closures run (the reason the claim does not hold):
- `src/core/ProjectJournal.cpp:131` `bool did = restoreStep(c, &redo);`, then `:135-147` the LIFO loop runs
  each recorded `undoAction()` (`:142`) and sets `did = true` (`:143`).
- `src/core/ProjectJournal.cpp:145` `if( !did ) { continue; }`, then `:148` `Engine::getSong()->setModified();`.
- `src/core/ProjectJournal.cpp:166-198`: `redo()` does the same, with `:198` `Engine::getSong()->setModified();`.
- The declined-redo branch (`ProjectJournal.cpp:179-186`) returns before `:198`. It applies no net change (it
  rolls back the actions it replayed and pushes the step back on the redo stack), so the flag is still correct there.
- `src/core/Song.cpp:975-981` `Song::setModified(bool)` sets `m_modified` unless `m_loadingProject` is set. No
  user undo or redo runs during loading.
- The only places that clear the flag are `src/core/Song.cpp:1902` (new project), `:2325` (load) and `:2668`
  (save, `noteProjectSaved`). None is on the undo or redo path.

Tests that already pin the contract:
- `tests/src/core/SessionAudioRecordTest.cpp:169-196` `undoAndRedoOfALandedTakeMarkTheSongModified`: save, undo,
  then check `isModified()` at `:187`; save, redo, then check at `:196`. Run on this branch (see below): passes.
- `tests/src/core/SessionCommandArgsTest.cpp:119` and `:126`: the same check for session edits. Cited, not run by me.
- No test asserts the modified flag after undoing or redoing a section edit (`rg` over `tests/` for section plus
  `isModified` found none). This is a coverage gap on the section path only. The section path uses the same
  journal mechanism as the take path, which the run test covers, so the gap is not a defect. No test was added.

## Commands run (all output in the LOG FOLDER `logs/`)

| # | Command (cwd noted) | Log | EXIT | Result |
|---|---|---|---|---|
| 1 | `cmake --build build --target SessionAudioRecordTest -j4` | `build-1.log` | 2 | ccache wrapper could not write its temp dir (`Permission denied`), not a compile error |
| 2 | `CCACHE_DISABLE=1 cmake --build build --target SessionAudioRecordTest -j4` | `build-2.log` | 0 | built |
| 3 | `cd build/tests && ctest -R SessionAudioRecordTest --output-on-failure` | `test-1.log` | 0 | 100% tests passed, 1 of 1 |
| 4 | `cd build/tests && ./SessionAudioRecordTest -v2` | `test-2.log` | 0 | `PASS : ...undoAndRedoOfALandedTakeMarkTheSongModified()`; `Totals: 7 passed, 0 failed` |
| 5 | `python3 feature_check.py <worktree>` (unwrapped) | (none) | 1 | `PermissionError` creating the sibling folder `<worktree>-feature-check`, outside the write jail. No gate ran. |
| 6 | `python3 -I run_gate_redirect.py <worktree> <LOG>/gate-logs` (see below) | `gates-2.log` | 0 | `RESULT: PASS`, 8 of 8 gates PASS |

Run 6 is the shared `feature_check.py` source, unchanged except for its log folder. The wrapper
(`logs/run_gate_redirect.py`) replaces the one line `logs = wt.parent / f"{wt.name}-feature-check"` with a log folder
inside the run folder and execs the rest. The gate logic and limits are not touched. This is a workaround for the jail,
and the owner should know about it.

Gate table (run 6, before this commit, 0 changed files):

| Gate | Result |
|---|---|
| fork-sources | PASS |
| all-sources-reproduce | PASS |
| no-upstream-regression | PASS |
| check-namespace | PASS |
| no-tautology | PASS |
| doc-figures | PASS |
| complexity | PASS (no new item in changed files) |
| file-length | PASS (no new item in changed files) |

Affected-family test count: `SessionAudioRecordTest` ran 1 ctest test and 7 QTest functions, all passed. The
section path has no test of its own on this branch.

RED/GREEN: not applicable. No product change, so the regression test was not written, per the procedure for a
not-real verdict. Step 4b (revert and re-apply the fix) is not applicable.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p20-14.md` (this report)

No product files, test files or gate baselines were changed.

## Not done / residual risk

- The section undo path has no test that asserts the modified flag. A passing characterization test would
  guard it against a future change to `ProjectJournal::undo()`. Not added, because it would not be RED and the
  verdict does not call for a fix.
- I did not run `SessionCommandArgsTest` (cited above, not run).
