# Cluster new-p16-217: loading more than 256 sub-scenes silently drops the rest

**Verdict: FIXED**

The legacy `<session>` reader loaded at most `SubScene::MaxCount` (256) `<subscene>` rows and stopped without checking for more. A block with 257 valid rows therefore loaded 256, and the next save wrote 256. The fix refuses the whole block when a 257th valid row exists, the same way the reader already refuses other malformed blocks. The refused block is kept verbatim as an unclaimed element, so nothing is written back shorter.

## Evidence in the product code

- `src/core/SessionModel.cpp:96-108` - `restoreSubScenes` now returns `bool`. Rows that do not parse are skipped (`:101`, unchanged). A valid row that would be the 257th returns `false` (`:105`), instead of the loop ending early at the limit as before.
- `src/core/SessionModel.cpp:437-442` - the sole caller (`SessionModel::restoreState`). On `false` it calls `clear()` and returns `false`, the same as the existing refusals for scenes and clips in this function. On success it moves the list into `m_subScenes`.
- `src/core/SessionModel.cpp:303-313` (`clear()`) resets `m_subScenes`, so a refused block leaves no partial list.
- The refused `<session>` element is then captured as unclaimed (`src/core/Song.cpp:1992-1993`: `restoreNamedSection` and `restoreGuiSection` both return false, then `captureUnclaimed`), and re-emitted on save.

Precedent that the refusal is the file's contract: `tests/src/core/SessionModelTest.cpp:400-443` (`malformedLegacySessionDimensionsArePreserved`) already keeps an over-large `tracks="257"` block verbatim.

Not affected: the shipped `<z:subscenes>` reader. `src/core/SessionSubScenes.cpp:58-69` (`readRows`) already refuses the 257th row, and `:125-128` keeps the whole section verbatim rather than loading a truncated list. The control create command refuses at 256 (`src/core/ControlCommandsSessionSubScenes.cpp:91-93`), so the undo payload (`sessionBlockXml`) never holds more than 256 rows.

## Regression test

`tests/src/core/SessionModelTest.cpp:446-501`, new function `excessLegacySubScenesAreRefusedNotTruncated`. It checks:

1. Exactly 256 valid rows restore (`:464-466`).
2. 257 valid rows: `restoreState` returns false and the model is empty (`:470-472`). This is the assertion that fails before the fix.
3. 256 valid rows plus 3 unreadable rows: restores 256, so unreadable rows still do not count (`:473-476`).
4. Project file: a legacy block with 257 rows is kept verbatim across load and save. The marker appears once and no `<z:subscenes` is written (`:478-500`).

Deterministic: no timing, no sleeps, no synchronisation.

## Behaviour change to review

A legacy `<session>` block with more than 256 valid sub-scenes is now refused as a whole. Its clips and scenes are not loaded into the session view for that project. The block is kept in the file verbatim, so no data is lost on save. This matches the existing refusal for malformed block dimensions. Blocks with 256 or fewer valid rows behave exactly as before. The refusal does not show a message in the UI, the same as the existing malformed-block refusal. Reviewer decision: whether a silent refusal is acceptable here, or the list should be surfaced some other way.

## Evidence table

All paths are relative to the worktree unless absolute. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-217.r1/logs/`.

| Check | Exact command and result |
| --- | --- |
| Build attempt 1 (before fix, env) | `cmake --build build --target SessionModelTest -j4 > .../build-1-red.log 2>&1; echo EXIT=$?` **EXIT=2**: ccache could not create its temp file under `/run/user/1000` (permission). Not a code error. Log: `build-1-red.log`. |
| Build (before fix) | `env CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p16-217 cmake --build build --target SessionModelTest -j4 > .../build-2-red.log 2>&1; echo EXIT=$?` **EXIT=0** |
| RED before fix | From `build/tests`: `ctest -R '^SessionModelTest$' --output-on-failure > .../test-1-red.log 2>&1; echo EXIT=$?` **EXIT=8**. `FAIL! : SessionModelTest::excessLegacySubScenesAreRefusedNotTruncated() '!model.restoreState( overLimit.documentElement() )' returned FALSE` at `SessionModelTest.cpp(470)`. `Totals: 12 passed, 1 failed`. |
| Build (fix applied) | `env CCACHE_TEMPDIR=... cmake --build build --target zene SessionModelTest -j4 > .../build-3-green.log 2>&1; echo EXIT=$?` **EXIT=0** |
| GREEN after fix | From `build/tests`: `ctest -R '^SessionModelTest$' --output-on-failure > .../test-2-green.log 2>&1; echo EXIT=$?` **EXIT=0**, `100% tests passed out of 1`. |
| Proof: reverse the fix | `git diff -- src/core/SessionModel.cpp > .../fix.patch; echo DIFF_EXIT=$?` **0**; `git apply -R .../fix.patch > .../4b-revert.log 2>&1; echo REVERT_EXIT=$?` **0**. |
| Build (fix reversed) | `env CCACHE_TEMPDIR=... cmake --build build --target zene SessionModelTest -j4 > .../build-4-reverted.log 2>&1; echo BUILD_EXIT=$?` **0** |
| RED with fix reversed | `ctest -R '^SessionModelTest$' --output-on-failure > .../test-3-reverted.log 2>&1; echo TEST_EXIT=$?` **EXIT=8**, same assertion at `SessionModelTest.cpp(470)`, `Totals: 12 passed, 1 failed`. |
| Proof: reapply the fix | `git apply .../fix.patch > .../4b-reapply.log 2>&1; echo APPLY_EXIT=$?` **0**; build `build-5-reapplied.log` **EXIT=0**; `ctest -R '^SessionModelTest$' -V > .../test-4-reapplied.log 2>&1; echo TEST_EXIT=$?` **EXIT=0**, `491: Totals: 13 passed, 0 failed`. |
| Same-family tests (fix applied) | Build `cmake --build build --target SessionGridRecordTest SessionAudioRecordTest SessionSubSceneTest -j4 > .../build-6-family.log 2>&1; echo BUILD_EXIT=$?` **0**. From `build/tests`: `ctest -R '^(SessionModelTest\|SessionGridRecordTest\|SessionAudioRecordTest\|SessionSubSceneTest)$' -V > .../test-5-family.log 2>&1; echo TEST_EXIT=$?` **EXIT=0**. **4 of 4** ctest entries passed; **32 QTest functions** passed (SessionSubSceneTest 8, SessionGridRecordTest 6, SessionModelTest 13, SessionAudioRecordTest 5), 0 failed. |
| Gate runner (see deviation) | `python3 -I /tmp/fc-new-p16-217/gate_runner.py <worktree> .../gates-1 > .../gate-2-feature-check.log 2>&1; echo EXIT=$?` **EXIT=0**, `RESULT: PASS`. |
| Official gate (feature_check.py) | `python3 .../feature_check.py <worktree> > .../gate-1-feature-check.log 2>&1; echo EXIT=$?` **EXIT=1**, before any gate ran: `PermissionError` creating the sibling folder `zene-qwen-new-p16-217-feature-check` beside the worktree, which is outside the write-jail. Not a gate result. |

## Gate deviation

The official `feature_check.py` cannot create its log folder outside the worktree (`PermissionError` at `feature_check.py:71`, `logs.mkdir`). Writing there is outside the write-jail, so I did not create it. Instead `/tmp/fc-new-p16-217/gate_runner.py` imports the same module and runs its `SIMPLE` and `RATCHET` lists with the same comparison logic, writing per-gate logs to `gates-1/` in the LOG folder. Its base listing comes from the cached `gate-baselines/b61e5f9658c2f55b74ee8137de37292ebe2cc8a0.json` (base = HEAD = `b61e5f965`, since `FIX-BASE` is `050/m0-train-9` and the merge base is HEAD). Every gate row passed, and the two changed files are `src/core/SessionModel.cpp` and `tests/src/core/SessionModelTest.cpp`, both already registered in `tests/fork-sources.txt` (`:1849` and `:2476`). No new file was added, so no registration change is needed. The tree was clean after the gate runs (`git status` showed only the two files).

## Files committed

- `src/core/SessionModel.cpp`
- `tests/src/core/SessionModelTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p16-217.md`

## Self-check

Verdict is FIXED, so the ALREADY-FIXED / FALSE-POSITIVE item does not apply.

**1. Regression test fails with the product change reverted and passes with it.** Commands and exits are in the evidence table above: RED before the fix **EXIT=8** (`test-1-red.log`), reverted-fix proof **EXIT=8** (`test-3-reverted.log`), GREEN with the fix **EXIT=0** (`test-2-green.log`, `test-4-reapplied.log`). The failing assertion is the same in both RED runs: `SessionModelTest.cpp(470)`, `restoreState` returned `true` for 257 valid rows.

**2. Deterministic.** The test builds its XML from string literals and integer loop counters (`SessionModelTest.cpp:448-462`). It calls no clock, sleep, timer or thread, and its only I/O is a `QTemporaryDir` project file written and read back synchronously (`:478-500`). The control-command path and the undo payload are not involved.

**3. Fix at the cause, callers and siblings checked.**
- Cause: `restoreSubScenes` ended its loop at `MaxCount` (`SessionModel.cpp:96-108`).
- Callers: `rg -n restoreSubScenes src tests` finds one call, `SessionModel.cpp:437`, which now handles the `false` return (`clear()` and `return false`, as the scene and clip failures do at `:429-434`).
- Siblings: the shipped `<z:subscenes>` reader already refuses the 257th row (`SessionSubScenes.cpp:64-65`, `:125-128`), so the two readers now agree. The control create command refuses at 256 (`ControlCommandsSessionSubScenes.cpp:91-93`), and every undo payload is written from the model, so it stays within 256.

**4. No collateral damage.**
- Blocks with 256 or fewer valid rows load exactly as before. The test covers the boundary (`:464-466`) and the case of 256 valid plus unreadable rows (`:473-476`). The unreadable-row rule (skipped, not counted) is unchanged.
- The one changed behaviour is a legacy `<session>` block with more than 256 valid rows. It is refused as a whole and kept verbatim, not loaded shorter (see "Behaviour change to review" above). No caller in the codebase legitimately produces such a block: the create command refuses at 256 and the `<z:subscenes>` reader refuses past it.
- Same-family tests, fix applied: `SessionSubSceneTest` 8/8, `SessionGridRecordTest` 6/6, `SessionAudioRecordTest` 5/5, `SessionModelTest` 13/13 (exit 0).

**Gates item (submit_check "gates (feature_check.py)").** This item still shows FAIL, and the cause is the environment, not the code. `feature_check.py` exits 1 before running any gate: `PermissionError` at `feature_check.py:71` (`logs.mkdir`), because it writes its logs to the sibling folder `zene-qwen-new-p16-217-feature-check`, outside the worktree and outside my write-jail. I did not create that folder and did not change the gate. The same gate commands, run through `/tmp/fc-new-p16-217/gate_runner.py` (same module, logs in the LOG folder), all pass: `RESULT: PASS`, exit 0 (`gate-2-feature-check.log`, logs in `gates-1/`). The owner or the fix loop needs to run `feature_check.py` where it can create its log folder to get the official table.
