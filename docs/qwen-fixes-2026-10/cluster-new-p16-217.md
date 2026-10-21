# Cluster new-p16-217: loading more than 256 sub-scenes silently drops the rest

**Verdict: FIXED**

The legacy `<session>` reader loaded at most `SubScene::MaxCount` (256) `<subscene>` rows and stopped without checking for more. A block with 257 valid rows therefore loaded 256, and the next save wrote 256. The fix refuses the whole block when a 257th valid row exists, the same way the reader already refuses other malformed blocks. The refused block is kept verbatim as an unclaimed element, so nothing is written back shorter.

## Round 2 (review feedback)

The review's only FAIL was the complexity gate: `SessionModel::restoreState` reached CCN 11, over the limit of 10, because the sub-scene refusal added one decision. Commit `b42570322` moves the tracks/scenes checks (four decisions, same parse and clamps) into a file-scope helper `restoreGridSize` (`src/core/SessionModel.cpp:63-78`). `restoreState` is back under the limit, and the gate runner reports complexity `PASS: no new item in your files`. Behaviour is unchanged. The round-1 sub-scene refusal and the regression test are unchanged.

## Evidence in the product code

- `src/core/SessionModel.cpp:116-128` - `restoreSubScenes` returns `bool`. Rows that do not parse are skipped (`:121-122`). A valid row that would be the 257th returns `false` (`:125`), instead of the loop ending early at the limit as before.
- `src/core/SessionModel.cpp:450-455` - the sole caller in `SessionModel::restoreState` (`:405-457`). On `false` it calls `clear()` and returns `false`, as the scene and clip failures do in the same function. On success it moves the list into `m_subScenes`.
- `src/core/SessionModel.cpp:325-334` (`clear()`) resets `m_subScenes`, so a refused block leaves no partial list.
- `src/core/SessionModel.cpp:63-78` - `restoreGridSize`, the moved tracks/scenes checks (round 2).
- The refused `<session>` element is captured as unclaimed (`src/core/Song.cpp:1992-1993`: `restoreNamedSection` and `restoreGuiSection` both return false, then `captureUnclaimed`) and re-emitted on save.

Precedent that the refusal is the file's contract: `tests/src/core/SessionModelTest.cpp:400-443` (`malformedLegacySessionDimensionsArePreserved`) already keeps an over-large `tracks="257"` block verbatim.

Not affected: the shipped `<z:subscenes>` reader. `src/core/SessionSubScenes.cpp:58-69` (`readRows`) already refuses the 257th row, and `:125-128` keeps the whole section verbatim rather than loading a truncated list. The control create command refuses at 256 (`src/core/ControlCommandsSessionSubScenes.cpp:91-93`), so the undo payload (`sessionBlockXml`) never holds more than 256 rows.

## Regression test

`tests/src/core/SessionModelTest.cpp:446-501`, function `excessLegacySubScenesAreRefusedNotTruncated` (unchanged in round 2). It checks:

1. Exactly 256 valid rows restore (`:464-466`).
2. 257 valid rows: `restoreState` returns false and the model is empty (`:470-472`). This is the assertion that fails without the fix.
3. 256 valid rows plus 3 unreadable rows: restores 256, so unreadable rows still do not count (`:473-476`).
4. Project file: a legacy block with 257 rows is kept verbatim across load and save. The marker appears once and no `<z:subscenes` is written (`:478-500`).

## Behaviour change to review

A legacy `<session>` block with more than 256 valid sub-scenes is now refused as a whole. Its clips and scenes are not loaded into the session view for that project. The block is kept in the file verbatim, so no data is lost on save. This matches the existing refusal for malformed block dimensions. Blocks with 256 or fewer valid rows behave exactly as before. The refusal does not show a message in the UI, the same as the existing malformed-block refusal. Reviewer decision: whether a silent refusal is acceptable here, or the list should be surfaced some other way.

## Evidence table

Logs: round 2 is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-217.r2/logs/`. Round 1 is in `.../new-p16-217.r1/logs/`. Commands run from the worktree or from `build/tests` as stated.

| Check | Exact command and result |
| --- | --- |
| Round 1 RED (fix absent) | From `build/tests`: `ctest -R '^SessionModelTest$' --output-on-failure > .../r1/logs/test-1-red.log 2>&1; echo EXIT=$?` **EXIT=8**; `FAIL! ... returned FALSE` at `SessionModelTest.cpp(470)`; `Totals: 12 passed, 1 failed`. |
| Round 1 GREEN (fix applied) | Same ctest command, `.../r1/logs/test-2-green.log`; **EXIT=0**. Verbose re-run `test-4-reapplied.log`: **13 passed, 0 failed**. |
| Build note | First attempt `cmake --build build --target SessionModelTest -j4 > build-1-red.log 2>&1` **EXIT=2**: ccache could not create its temp file under `/run/user/1000` (permission). Rerun with `env CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p16-217` **EXIT=0**. Not a code error. |
| Round 2 build and family | `env CCACHE_TEMPDIR=... cmake --build build --target zene SessionModelTest SessionGridRecordTest SessionAudioRecordTest SessionSubSceneTest -j4 > r2/logs/build-1-refactor.log 2>&1; echo BUILD_EXIT=$?` **EXIT=0**. From `build/tests`: `ctest -R '^(SessionModelTest\|SessionGridRecordTest\|SessionAudioRecordTest\|SessionSubSceneTest)$' -V > r2/logs/test-1-family.log 2>&1; echo TEST_EXIT=$?` **EXIT=0**: **4/4** ctest entries; **32/32** QTest functions (SessionSubSceneTest 8, SessionGridRecordTest 6, SessionModelTest 13, SessionAudioRecordTest 5). |
| Round 2 proof, fix reversed | `git diff b61e5f965 -- src/core/SessionModel.cpp > r2/logs/fix.patch; echo DIFF_EXIT=$?` **0**. `git apply -R r2/logs/fix.patch > r2/logs/4b-revert.log 2>&1; echo REVERT_EXIT=$?` **0**. Build `build-2-reverted.log` **EXIT=0**. `ctest -R '^SessionModelTest$' --output-on-failure > r2/logs/test-2-reverted.log 2>&1; echo TEST_REVERTED_EXIT=$?` **EXIT=8**: `FAIL! ... returned FALSE` at `SessionModelTest.cpp(470)`; `Totals: 12 passed, 1 failed`. |
| Round 2 proof, fix reapplied | `git apply r2/logs/fix.patch > r2/logs/4b-reapply.log 2>&1; echo REAPPLY_EXIT=$?` **0**. Build `build-3-reapplied.log` **EXIT=0**. `ctest -R '^SessionModelTest$' -V > r2/logs/test-3-reapplied.log 2>&1; echo TEST_REAPPLIED_EXIT=$?` **EXIT=0**: `491: Totals: 13 passed, 0 failed`. |
| Round 2 gate runner | `python3 -I /tmp/fc-new-p16-217/gate_runner.py <worktree> r2/logs/gates-2 > r2/logs/gate-2-runner.log 2>&1; echo GATE_RUNNER_EXIT=$?` **EXIT=0**, `RESULT: PASS`. All eight rows pass, including `complexity` and `file-length` (`no new item in your files`). |
| Official gate (feature_check.py) | `python3 .../feature_check.py <worktree> > r2/logs/gate-1-official.log 2>&1; echo OFFICIAL_EXIT=$?` **EXIT=1**, `PermissionError` writing into the sibling folder `zene-qwen-new-p16-217-feature-check`, outside the write-jail. The gate does not run. |

## Gate deviation

The official `feature_check.py` writes its per-gate logs into the sibling folder `<worktree>-feature-check` next to the worktree. That is outside my write-jail, so it fails with `PermissionError` (round 1 at `feature_check.py:71`, `logs.mkdir`; round 2 when writing `fork-sources.log`). I did not create or write into that folder and did not change the gate. `/tmp/fc-new-p16-217/gate_runner.py` imports the same module and runs its `SIMPLE` and `RATCHET` lists, with the same comparison logic and the cached base listing `gate-baselines/b61e5f9658c2f55b74ee8137de37292ebe2cc8a0.json`, writing logs to the LOG folder. The base is `b61e5f965`, which is the merge base of HEAD with `FIX-BASE` (`050/m0-train-9`). Every gate row passes. The changed files are `src/core/SessionModel.cpp`, `tests/src/core/SessionModelTest.cpp` and this report. Both source files are already registered in `tests/fork-sources.txt` (`:1849` and `:2476`). No new file was added, so no registration change is needed.

## Self-check

Verdict is FIXED, so the ALREADY-FIXED / FALSE-POSITIVE item does not apply.

**1. Regression test fails with the product change reverted and passes with it.** Round 2 proof: `git diff b61e5f965` of the product file, reversed with `git apply -R` (**REVERT_EXIT=0**), built (**EXIT=0**), `ctest -R '^SessionModelTest$'` **TEST_REVERTED_EXIT=8**, failing at `SessionModelTest.cpp(470)`. Reapplied (**REAPPLY_EXIT=0**), built (**EXIT=0**), **TEST_REAPPLIED_EXIT=0**, 13/13 QTest functions. Round 1 showed the same pattern (RED **EXIT=8**, GREEN **EXIT=0**). The failing assertion is `restoreState` returning `true` for 257 valid rows.

**2. Deterministic.** The test builds its XML from string literals and integer loop counters (`SessionModelTest.cpp:448-462`). It calls no clock, sleep, timer or thread. Its only I/O is a `QTemporaryDir` project file written and read back synchronously (`:478-500`). The control-command path and the undo payload are not involved.

**3. Fix at the cause, callers and siblings checked.**
- Cause: `restoreSubScenes` ended its loop at `MaxCount` (`SessionModel.cpp:116-128`).
- Callers: `rg -n restoreSubScenes src tests` finds one call, `SessionModel.cpp:450`, which handles the `false` return (`clear()` and `return false`, as the scene and clip failures do).
- Refactor: `restoreGridSize` (`:63-78`) keeps the original tracks/scenes parse and clamps, and is called only from `restoreState` (`:433`).
- Siblings: the shipped `<z:subscenes>` reader already refuses the 257th row (`SessionSubScenes.cpp:64-65`, `:125-128`), so the two readers agree. The control create command refuses at 256 (`ControlCommandsSessionSubScenes.cpp:91-93`), and every undo payload is written from the model, so it stays within 256.

**4. No collateral damage.**
- Blocks with 256 or fewer valid rows load exactly as before. The test covers the boundary (`:464-466`) and 256 valid plus unreadable rows (`:473-476`). The unreadable-row rule (skipped, not counted) is unchanged. The grid-size refactor keeps the same checks and the same early refusal.
- The one changed behaviour is a legacy `<session>` block with more than 256 valid rows. It is refused as a whole and kept verbatim, not loaded shorter (see "Behaviour change to review"). No caller in the codebase legitimately produces such a block: the create command refuses at 256, and the `<z:subscenes>` reader refuses past it.
- Same-family tests with the final code: `SessionSubSceneTest` 8/8, `SessionGridRecordTest` 6/6, `SessionAudioRecordTest` 5/5, `SessionModelTest` 13/13, all exit 0 (`test-1-family.log`).

**5. Verdict item.** Not applicable: the verdict is FIXED.

**Gates item (submit_check "gates (feature_check.py)").** The official `feature_check.py` still exits 1 because of the `PermissionError` above, which is environmental. The gate runner, which runs the same gates with the same comparison, passes (`gate-2-runner.log`, `RESULT: PASS`). The owner or the fix loop needs to run `feature_check.py` where it can write its log folder to get the official table.

## Files committed

- `src/core/SessionModel.cpp` (round 1 `b91066a57`, round 2 refactor `b42570322`)
- `tests/src/core/SessionModelTest.cpp` (round 1 `b91066a57`)
- `docs/qwen-fixes-2026-10/cluster-new-p16-217.md` (round 1 `b91066a57`, self-check `d42fee428`, round 2 update)
