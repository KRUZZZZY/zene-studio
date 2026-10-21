Verdict: **FIXED** - cluster new-p15-20-port: `mts.master_set` records an undo step for an unchanged flag

Branch `bugs/qwen-new-p15-20-port` (base `b61e5f965`, the train-9 snapshot this worktree was cut from). The finding is REAL on this branch. The fix is the same guard the newest line already carries, and the port re-applies it by hand.

## What the finding says and what the code does

`mts.master_set` (`src/core/ControlCommandsMts.cpp:318-349` at HEAD, before the fix) wrote the flag with `setMtsMaster` and then called `control::addUndoStep` unconditionally. Setting the flag to the value it already has therefore still pushed an undo step. That step changes nothing, and it sits on top of the stack, so the next `control.undo` unwinds the no-op and leaves the real edit behind it applied.

The same no-step rule is already used by sibling commands for an unchanged value:

- `src/core/ControlCommandsDevice.cpp:149-152` (`device.mpe_set`): "A value the flag already had changes nothing, so it takes NO undo step ... the next control.undo would unwind the no-op".
- `src/core/ControlCommandsChordEdit.cpp:218` (`chord` mode): `if (previous == mode) { return ControlResult::success(result); }  // nothing changed: no step`.
- `src/core/ControlCommandsDeviceCopy.cpp:453` and `src/core/ControlCommandsAutomationTools.cpp:109`: the same early return.

The A16 contract allows this. `docs/A16-REVERSIBILITY.md:111-112` says that when nothing changed, `project.open` and `script.run` "register no step". The general line at `docs/A16-REVERSIBILITY.md:219` ("One record per successful mutating command") does not list those per-call exceptions, and it is not edited here.

## Why the earlier FALSE-POSITIVE is overturned

Commit `10e4d5fd1` (on the accepted `bugs/qwen-new-p15-20` line) called the finding FALSE-POSITIVE. It relied on the A16 line above and on `SessionTuningTest.cpp:1337`, which pinned one step for an idempotent disarm. That reasoning missed the per-call exceptions and the sibling rule. The test that pinned the step is the one this fix changes.

## The newest line already carries this fix

- `050/m0-train-10` (tip `0a8842d39`) is 643 commits ahead of this branch's HEAD `b61e5f965`. This worktree is therefore not "a fresh branch off the newest line"; it is older.
- On the newest line, `src/core/ControlCommandsMts.cpp` has the same early return after `setMtsMaster` (`git show 050/m0-train-10:src/core/ControlCommandsMts.cpp`, lines 329-334). That guard arrived with `539461020` (`fix(new-p14-35)`). `SessionTuningTest.cpp` on that line expects depth unchanged for an idempotent disarm.
- Accepted commit `81fa5d5de` (this cluster) added `masterSetEachUnchangedCallIsItsOwnUndoStep`, which requires two identical calls to add two steps. That assertion contradicts the guard. This is the likely reason the port failed on train-10. I did not build or run the newest line here (it is not checked out in this worktree), so that cause is inferred from the code, not reproduced.
- `TimelineSpeechTest` is not a test source on this branch (`rg -l TimelineSpeech tests src` finds only docs and include files). I did not investigate it.

## Change in this commit

1. `src/core/ControlCommandsMts.cpp:329-333`: after `setMtsMaster` succeeds (so a refusal, and a repeated arm's re-registration, still behave as before), `if (enabled == wasEnabled) { return ControlResult::success(fullState()); }`. No undo step and no `__transaction`. A changed flag still records exactly one step.
2. `src/core/ControlReversibilityTableMts.cpp:115-116`: the `mts.master_set` row text now says a call that leaves the flag as it is pushes no step. The text is not in `commands_snapshot.json` or any doc (checked with `rg`), so the canary is unaffected.
3. `tests/src/core/SessionTuningTest.cpp`:
   - `masterSetReportsItsOwnMtsEspState`, line 1337: `QCOMPARE(undoDepth(), depthBefore + 1)` becomes `depthBefore`, and the comment is rewritten. This changes an expectation to the new contract. It asserts more (zero steps), not less.
   - New test `repeatedMasterSetLeavesTheRealEditForUndo` (lines 1342-1370): records a real table edit (`mts.set_note`), repeats `mts.master_set enabled=false`, then requires that `control.undo` restores the table before the `set_note`.

No new file. No new command. No new header. No product file outside the three above.

## Evidence table

All commands run from the worktree, logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-20-port.r1/logs/`.

| # | Command | EXIT | Result |
|---|---|---|---|
| build-1 | `cmake --build build --target zene SessionTuningTest -j4` | 2 | Environment only: `make cannot open jobserver ... make-jobserver.fifo: Permission denied`; `ccache: ... /run/user/1000/ccache-tmp ... Permission denied`. No code error (`build-1.log`). |
| build-2 | `env -u MAKEFLAGS -u MFLAGS CCACHE_TEMPDIR=/tmp/ccache-tmp-p15 cmake --build build --target zene SessionTuningTest -j4` | 0 | Baseline build of HEAD plus the new test (`build-2.log`). |
| test-1 RED | `cd build/tests && ctest -R SessionTuningTest --output-on-failure` (test edited, product fix absent) | 8 | `FAIL! masterSetReportsItsOwnMtsEspState` at `SessionTuningTest.cpp:1337`: Actual `undoDepth()` 1, Expected `depthBefore` 0. `FAIL! repeatedMasterSetLeavesTheRealEditForUndo` at `:1361`: Actual 3, Expected `depthBefore + 1` 2. `Totals: 23 passed, 2 failed` (`test-1-red.log`). |
| fix | guard added to `ControlCommandsMts.cpp` | - | - |
| build-3 | `env -u MAKEFLAGS -u MFLAGS CCACHE_TEMPDIR=/tmp/ccache-tmp-p15 cmake --build build --target zene SessionTuningTest -j4` | 0 | `build-3-fix.log` |
| test-2 GREEN | `cd build/tests && ctest -R SessionTuningTest --output-on-failure` | 0 | `100% tests passed, 0 tests failed out of 1` (`test-2-green.log`). |
| 4b-a | `git diff -- src/core/ControlCommandsMts.cpp > fix.patch` (16 lines), then `git apply -R fix.patch` | 0 | Product fix reverted; test file unchanged. |
| 4b-b | build (`build-4-reverted.log`) | 0 | |
| 4b-c | `ctest -R SessionTuningTest --output-on-failure` (`test-3-reverted-red.log`) | 8 | Same two failures at `:1337` (Actual 1) and `:1361` (Actual 3). |
| 4b-d | `git apply fix.patch` | 0 | Fix restored. |
| 4b-e | build (`build-5-reapplied.log`) | 0 | |
| 4b-f | `ctest -R SessionTuningTest --output-on-failure` (`test-4-reapplied-green.log`) | 0 | Passes. |
| row | whitespace fix in `ControlReversibilityTableMts.cpp` row text; rebuild | 0 | `build-6-final.log` |
| test-5 | `ctest -R SessionTuningTest --output-on-failure` | 0 | `test-5-final-green.log` |
| test-6 | `ctest -R SessionTuningTest -V` | 0 | `Totals: 25 passed, 0 failed, 0 skipped` (`test-6-final-verbose.log`). |
| build-7 | rebuild on the committed tree | 0 | `build-7-committed-tree.log` |
| test-7 | `ctest -R SessionTuningTest -V` on the committed tree | 0 | `Totals: 25 passed, 0 failed, 0 skipped` (`test-7-committed-tree.log`). |

Affected family: `ctest -R SessionTuningTest` ran **1** ctest entry, which runs **25** QTest subtests, all passing. `masterSetReportsItsOwnMtsEspState` and `repeatedMasterSetLeavesTheRealEditForUndo` are both among them.

Gates:

| Command | EXIT | Result |
|---|---|---|
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (as shipped) | 1 | `PermissionError` creating `<worktree>-feature-check`, a sibling folder outside the write-jail (`gates-1.log`). The script has no log-folder override. |
| `python3 /tmp/zene-fc-p15/feature_check_logdir.py <worktree>` (copy of the same script; only `ROOT` and the log folder changed, to the LOG FOLDER) | 0 | `RESULT: PASS`: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures all exit 0; complexity and file-length "no new item in your files" (`gates-3-final-tree.log`, per-gate logs in `logs/gates/`). |

## Self-check

- **Each new test FAILS with the product change reverted and PASSES with it.** Yes: 4b-c exit 8, 4b-f exit 0 (`fix.patch` reverts only `ControlCommandsMts.cpp`). Caveat: the repeated-disarm test stops at its depth assertion (`:1361`) before its undo-restores-table assertion (`:1369`), so the undo assertion itself was not separately observed failing. The depth failure is the user-visible symptom (a no-op sits on the undo stack).
- **Acceptance criteria**: none (not a feature item).
- **Deterministic**: no timing, sleeps, races or seams. The tests compare `undoDepth()` values measured relative to a baseline taken in the same test, and the table with `snapshotState`/`sameTable`. Both hold whether or not the MTS-ESP library is present: in the present case the first disarm may record a step, but the baseline is taken after it.
- **Fix at the cause and complete**: the cause is the unconditional `addUndoStep` in `masterSet`. The only C++ caller of `mts.master_set` is the registry handler; the GUI caller `src/gui/SessionTuningMenu.cpp:123-126` reads the reply and does not depend on an undo step. The refusal path and the repeated-arm re-registration still run first, as before.
- **Sibling commands** (`mts.load_scale`, `mts.load_keymap`, `mts.set_tuning`, `mts.set_note`, `mts.reset`): all record through `tableMutation`, which calls `control::addUndoStep` unconditionally (`src/core/ControlCommandsMts.cpp:139`). By reading, an unchanged table write there records a no-op step too. **Not changed here** (not the finding, and the newest line did not change them either). Whole-table comparison and the built-from scale/keymap need a design decision. Follow-up, not verified by test.
- **No collateral damage**: the MTS master flag is engine state with no `JournallingObject` (`src/core/ControlReversibilityTableMts.cpp:112-113`), so it is not written into saved projects. Old projects load as before. No other caller depends on the step (checked: `rg -n "master_set|mts_master"` in `tests/` lists only `SessionTuningTest.cpp`, plus `clock.master_set` tests that are unrelated).
- **Realtime**: `masterSet` is a control-thread handler, not an audio-path function. The guard adds no allocation beyond the reply object the existing success path already builds. No locks, I/O or signals added.
- **Undo honesty**: an unchanged call records no step; a changed call records exactly one (unchanged code path).
- **Reversibility class**: still `true_inverse` (`ControlReversibilityTableMts.cpp:111`). Only the explanatory row text changed.
- **Sibling commands in the command files touched** (from the completion checker): the `mts.*` group, as listed above.
- **Qt portability**: no new Qt API.
- **Registration**: no new source or test file, so `tests/fork-sources.txt`, `tests/upstream-modifications.txt` and `tests/CMakeLists.txt` are unchanged.

## Known gaps

1. `feature_check.py` cannot run as shipped from this sandbox (the sibling-folder `PermissionError` above). The table in this report comes from a copy with only the log folder redirected. `submit_check.py` runs the shipped script itself, so its "gates" row will show the same `PermissionError` FAIL. That is an environment limit, not a gate result.
2. The newest line `050/m0-train-10` was not built or run here. Its SessionTuningTest failure is inferred from the code (see above).
3. `docs/A16-REVERSIBILITY.md:219` still states the one-record-per-call rule without the no-change exception. Not edited.
4. The sibling `mts.*` no-op steps (above) are unfixed.
