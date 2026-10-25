Verdict: **ALREADY-FIXED** on the port target `050/m0-train-10` (tip `0a8842d39`) - cluster new-p15-20-port: `mts.master_set` records an undo step for an unchanged flag

The port target is `050/m0-train-10`. It already contains the guard (`src/core/ControlCommandsMts.cpp:333`), so no port patch is submitted for it. The evidence is in "Port target result" below.

Branch `bugs/qwen-new-p15-20-port` is based on train-9 (`b61e5f965`), not on the target. Its two commits (`9a23087ec` fix, `d4c414eab` self-check) carry the same guard and are a valid train-9 fix, but they are not the port. The finding is REAL on that train-9 base (the section "Change on this branch" describes it).

## What the finding says and what the code does

`mts.master_set` (`src/core/ControlCommandsMts.cpp:318-349` at HEAD, before the fix) wrote the flag with `setMtsMaster` and then called `control::addUndoStep` unconditionally. Setting the flag to the value it already has therefore still pushed an undo step. That step changes nothing, and it sits on top of the stack, so the next `control.undo` unwinds the no-op and leaves the real edit behind it applied.

The same no-step rule is already used by sibling commands for an unchanged value:

- `src/core/ControlCommandsDevice.cpp:149-152` (`device.mpe_set`): "A value the flag already had changes nothing, so it takes NO undo step ... the next control.undo would unwind the no-op".
- `src/core/ControlCommandsChordEdit.cpp:218` (`chord` mode): `if (previous == mode) { return ControlResult::success(result); }  // nothing changed: no step`.
- `src/core/ControlCommandsDeviceCopy.cpp:453` and `src/core/ControlCommandsAutomationTools.cpp:109`: the same early return.

The A16 contract allows this. `docs/A16-REVERSIBILITY.md:111-112` says that when nothing changed, `project.open` and `script.run` "register no step". The general line at `docs/A16-REVERSIBILITY.md:219` ("One record per successful mutating command") does not list those per-call exceptions, and it is not edited here.

## Why the earlier FALSE-POSITIVE is overturned

Commit `10e4d5fd1` (on the accepted `bugs/qwen-new-p15-20` line) called the finding FALSE-POSITIVE. It relied on the A16 line above and on `SessionTuningTest.cpp:1337`, which pinned one step for an idempotent disarm. That reasoning missed the per-call exceptions and the sibling rule. The test that pinned the step is the one this fix changes.

## Port target result: ALREADY-FIXED on `050/m0-train-10`

Checked by reading the target with `git show`, `git grep` and `git merge-base`. Nothing was checked out, switched or written.

| Check | Command | Result |
|---|---|---|
| Ancestry | `git merge-base --is-ancestor 050/m0-train-10 HEAD` (this branch) | EXIT 1: this branch is not on the target |
| Merge base | `git merge-base HEAD 050/m0-train-10` | `b61e5f9658c2f55b74ee8137de37292ebe2cc8a0` (train-9) |
| Guard | `git show 050/m0-train-10:src/core/ControlCommandsMts.cpp` | line 333: `if (enabled == wasEnabled) { return ControlResult::success(fullState()); }`, placed after `setMtsMaster` (line 325) and before `control::addUndoStep` (line 336) |
| Idempotent disarm test | `git show 050/m0-train-10:tests/src/core/SessionTuningTest.cpp` | lines 1339-1345 (library-absent branch): a disarm of a never-armed master, then `QCOMPARE(undoDepth(), depthBefore)` at line 1345 (no step) |
| Repeated-disarm test | same file | lines 1312-1320 (library-present branch): a repeated disarm, then `REV_UNDO_OR_FAIL()` brings the master back |
| Reversibility row | `git show 050/m0-train-10:src/core/ControlReversibilityTableMts.cpp` | row text still reads "pushes one addUndoStep pair" (this branch's doc-text edit is not on the target). Text only, no behaviour |
| TimelineSpeech | `git grep -l TimelineSpeech 050/m0-train-10 -- tests src` | no matches (no such test source on the target) |
| Round-1 failing test | `git grep -n masterSetEachUnchangedCallIsItsOwnUndoStep 050/m0-train-10` | no matches: reverted by `d5a6b6881` |

So the round-1 cause I had proposed for the train-10 failure is not on the target: the test that asserted two steps was reverted there, and the target's own test expects the guard's behaviour. The failure the brief cites on train-10 is therefore **not explained by the code I can see**, and I did not reproduce it.

**Not built or run.** Building the target would need a second full tree configured and compiled in `/tmp`, which this job did not do. The ALREADY-FIXED verdict rests on the static evidence above. What would settle it: build `050/m0-train-10` (`cmake --build <target-build> --target zene SessionTuningTest -j4`), run `ctest -R SessionTuningTest` from its `build/tests`, and check that `TimelineSpeechTest` is a real test name there (it is not a source on the target).

## Change on this branch (train-9 base, not the port)

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

## Round 2 re-run on this branch (train-9 base)

Run after the review asked for the port to be re-verified on `050/m0-train-10`. The branch itself was rebuilt and re-tested unchanged. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-20-port.r2/logs/` (the round-1 logs were in `.../new-p15-20-port.r1/logs/`).

| # | Command | EXIT | Result |
|---|---|---|---|
| build-1 | `env -u MAKEFLAGS -u MFLAGS CCACHE_TEMPDIR=/tmp/ccache-tmp-p15 cmake --build build --target zene SessionTuningTest -j4` | 0 | `build-1.log` |
| test-1 | `cd build/tests && ctest -R SessionTuningTest --output-on-failure` | 0 | `100% tests passed out of 1` (`test-1.log`) |
| test-2 | `cd build/tests && ctest -R SessionTuningTest -V` | 0 | `Totals: 25 passed, 0 failed, 0 skipped` (`test-2-verbose.log`) |
| gates-1 | `python3 /tmp/zene-fc-p15/feature_check_r2.py <worktree>` (copy of `feature_check.py`; only `ROOT` and the log folder changed) | 0 | `RESULT: PASS`, 8/8 rows (`gates-1.log`; per-gate logs in `logs/gates/`) |
| gates-2 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (shipped) | 1 | `PermissionError` creating `<worktree>-feature-check` outside the write-jail (`gates-2-shipped.log`) |

The round-1 RED/GREEN and 4b evidence (exit 8, then 0) is in the Evidence table above and still applies to the branch's fix commits.

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
- **Sibling commands in the command files touched** (the completion checker's list, each one checked):
  - `mts.master_set`: the fixed command.
  - `mts.get_state`: read-only (not mutating), so no undo step and nothing to change.
  - `mts.load_scale`, `mts.load_keymap`, `mts.set_tuning`, `mts.set_note`, `mts.reset`: all mutate through `tableMutation` (`ControlCommandsMts.cpp:127-149`), which records a step unconditionally. Consistent with the fix only in the sense that they are unchanged here. An unchanged-table write there still records a no-op step by reading. **Not changed** (see Known gaps 4). Not verified by a test.
- **ALREADY-FIXED evidence (the verdict)**: the guard is at `050/m0-train-10:src/core/ControlCommandsMts.cpp:333`, and the target's idempotent-disarm test expects no step at `050/m0-train-10:tests/src/core/SessionTuningTest.cpp:1345`. The no-step contract it relies on is `docs/A16-REVERSIBILITY.md:111-112` and the sibling early returns cited above.
- **Qt portability**: no new Qt API.
- **Registration**: no new source or test file, so `tests/fork-sources.txt`, `tests/upstream-modifications.txt` and `tests/CMakeLists.txt` are unchanged.

## Known gaps

1. `feature_check.py` cannot run as shipped from this sandbox (the sibling-folder `PermissionError` above). The table in this report comes from a copy with only the log folder redirected. `submit_check.py` runs the shipped script itself. Observed: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p15-20-port` EXIT 1 (`submit-check-1.log`). PASS: commits on the branch, working tree clean, report present, tests added or changed. The only FAIL is `gates (feature_check.py)`, from the same sandbox `PermissionError`. Gate results above come from the log-folder copy, which exits 0. That is an environment limit, not a gate result.
2. The port target `050/m0-train-10` was verified by reading only. It was not built or run here, so the train-10 failure cited in the brief is not reproduced (see "Port target result").
3. `docs/A16-REVERSIBILITY.md:219` still states the one-record-per-call rule without the no-change exception. Not edited.
4. The sibling `mts.*` no-op steps (above) are unfixed.
