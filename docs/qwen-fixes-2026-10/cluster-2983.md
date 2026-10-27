Verdict: FALSE-POSITIVE

2983: FALSE-POSITIVE

The finding says `session.setlist` with `{"items":"project.mmp"}` succeeds and replaces the setlist with an empty one. On this branch that input is refused before the handler runs, so the reported behaviour does not occur through any caller in the tree.

Evidence:

- `session.setlist` declares `items` with `arrayProperty()` and `required` = none at [ControlCommandsSessionPerform.cpp:306](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-2983/src/core/ControlCommandsSessionPerform.cpp:306). `arrayProperty()` is `{"type":"array"}` at [ControlVocabulary.cpp:98](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-2983/src/core/ControlVocabulary.cpp:98). `ControlCommandDef.cpp:42` stores that schema as `argsSchema`.
- `ControlRegistry::invoke` runs `validateArgs(cmd->argsSchema, args)` at [ControlRegistry.cpp:277](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-2983/src/core/ControlRegistry.cpp:277) and returns `InvalidArgs` before the handler at lines 278-279. `validateArgs` is at [ControlSchema.cpp:178](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-2983/src/core/ControlSchema.cpp:178). It reaches `checkProperties` ([ControlSchema.cpp:130](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-2983/src/core/ControlSchema.cpp:130)) and `checkType`, where `"array"` is `value.isArray()` ([ControlSchema.cpp:56](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-2983/src/core/ControlSchema.cpp:56)). A string therefore fails with `items: expected array`.
- Every in-tree caller reaches the handler through `invoke`: `PerformView.cpp:60-63` (`run` wraps `invoke`), `SessionGridMenus.cpp:87` (direct `invoke`) and `:95` (through `runCommand`, [CommandRun.cpp:110](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-2983/src/gui/CommandRun.cpp:110), which calls `invoke`). The only direct `->handler(` calls are `ControlCommandsNoteAcross.cpp:130`, which is preceded by `validateArgs` at `:85`, and `ControlCommandsRecordTools.cpp:69` (`handlerOf`), which only calls `transport.punch_*`. `ControlCommandsSessionPerform.cpp:267` calls `project.open`'s handler, not `setlist`. No caller hands `session.setlist` a non-array.
- The handler's own gap is real but unreachable: `allPaths()` at [ControlCommandsSessionPerform.cpp:230-232](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-2983/src/core/ControlCommandsSessionPerform.cpp:230) and `setlist()` at `:238` call `toArray()`, which yields an empty array for a non-array, and `all_of` over empty is true. This is safe only because the schema rejects non-arrays first. The mutation below shows that if the schema check were absent, the call succeeds.
- Sibling checked: `set_playlist` reads `scenes` with `toArray()` at [ControlCommandsSessionPerform.cpp:161](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-2983/src/core/ControlCommandsSessionPerform.cpp:161). It has the same reliance on the schema, which declares `scenes` as `arrayProperty()` and `required` at `:300-301`.

Why no product change: the reported path cannot happen, and the handler's helper is only reachable through the schema-validated `invoke`. Adding `isArray()` inside the handler would not change any observable behaviour. I did not make it, so this stays a claim-only closure. If the reviewer wants defence in depth, the one-line change is `allPaths` returning false for a non-array; that is a choice for the reviewer, not a bug fix.

Regression guard: `SessionPerformTest::aNonArraySetlistIsRefusedAndKeepsTheList` (added in [tests/src/core/SessionPerformTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-2983/tests/src/core/SessionPerformTest.cpp)). It asserts that `items: "project.mmp"` and `items: [1]` return `InvalidArgs`, and that the stored list is kept after a refused call. It passes on the unchanged code, as expected for a not-real finding. It fails when the schema check is removed, which is the only way the reported symptom could appear (mutation below).

## Commands and results

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2983.r1/logs`

| Step | Command | Result |
| --- | --- | --- |
| Build target, first attempt | `cmake --build build --target SessionPerformTest -j2 > $L/build-1.log 2>&1; echo EXIT=$?` | EXIT=2: ccache could not create its temp file under `dsh/home/ccache-tmp-r6` ("Permission denied", outside the write-jail). Environment issue, not a code error. |
| Build target, with writable ccache temp | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-2983 cmake --build build --target SessionPerformTest -j2 > $L/build-2.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Guard run on unchanged code (ctest) | `cd build/tests && ctest -R SessionPerformTest --output-on-failure > $L/test-1.log 2>&1; echo EXIT=$?` | EXIT=0; 1/1 passed |
| Guard run, verbose binary | `./SessionPerformTest -v2 > $L/run-guard-unchanged.log 2>&1; echo EXIT=$?` | EXIT=0; `PASS : ...aNonArraySetlistIsRefusedAndKeepsTheList()`; Totals 13 passed, 0 failed |
| Mutation: remove the setlist `items` type check | `sed -i 's/{{QStringLiteral("items"), arrayProperty()}}, {}, true, setlist);/{{QStringLiteral("items"), QJsonObject()}}, {}, true, setlist);/' src/core/ControlCommandsSessionPerform.cpp` (original copy at `$L/ControlCommandsSessionPerform.orig.cpp`; patch at `$L/mutation-schema-removed.patch`) | applied |
| Mutation build | `cmake --build build --target SessionPerformTest -j2 > $L/build-3-mutation.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Mutation run (RED for the guard) | `cd build/tests && ./SessionPerformTest aNonArraySetlistIsRefusedAndKeepsTheList > $L/run-mutation-red.log 2>&1; echo EXIT=$?` | EXIT=1: `FAIL! ... aNonArraySetlistIsRefusedAndKeepsTheList()`, `Actual (notArray.errorKind): 0`, `Expected (ControlErrorKind::InvalidArgs): 3`, at `SessionPerformTest.cpp:164` |
| Revert the mutation | `cp $L/ControlCommandsSessionPerform.orig.cpp src/core/ControlCommandsSessionPerform.cpp`, then `git status --short` | only `tests/src/core/SessionPerformTest.cpp` modified; product file identical to HEAD |
| Rebuild after revert | `CCACHE_TEMPDIR=... cmake --build build --target SessionPerformTest -j2 > $L/build-4-restored.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Guard run after revert (GREEN) | `cd build/tests && ctest -R SessionPerformTest --output-on-failure > $L/test-2.log 2>&1; echo EXIT=$?` | EXIT=0; 1/1 passed |
| Family count | `ctest -N -R "Session|ControlRegistry|ControlSchema" > $L/family-list.log` | 33 tests listed; only `SessionPerformTest` is built in this tree, the other 32 binaries are absent. No product code changed, so none were built. |
| Gates | `feature_check.py` writes its logs to `<worktree>/../zene-qwen-2983-feature-check`, which is outside the write-jail (`PermissionError`, first attempt). I ran the same gate functions (`SIMPLE`, `RATCHET`, `base_listings`, the same comparison) from `$TMPDIR/2983-driver/gate_driver.py` with the per-gate logs in `$L/gate-1/`: `python3 -I $TMPDIR/2983-driver/gate_driver.py <worktree> $L/gate-1 > $L/gate-1-table.log 2>&1; echo EXIT=$?` | EXIT=0; RESULT: PASS. fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures: PASS. complexity, file-length: PASS (no new item in changed files). |

Affected-family test count: `SessionPerformTest` 1/1 passed (before and after the mutation revert). The guard case is one of 13 cases in that binary; `Totals: 13 passed, 0 failed`.

The procedure's step 4b (patch reversal of a product fix) does not apply: there is no product fix. The mutation above is the equivalent check that the guard catches the finding's symptom.

## Self-check

- *The regression test FAILS with your product change reverted and PASSES with it.* No product change exists. The guard passes on the unchanged code (`test-1.log` EXIT=0). With the schema check removed, it fails (`run-mutation-red.log` EXIT=1, `SessionPerformTest.cpp:164`). With the code restored, it passes (`test-2.log` EXIT=0).
- *The test is deterministic.* No timing, sleeps, threads or real files. It uses `ControlRegistry::invoke` and the stored setlist, and leaves the setlist empty at the end, as the neighbouring cases do.
- *The fix is at the cause and complete.* Not applicable: no fix. The cause of the rejection is the `"type":"array"` schema on `items` (`ControlVocabulary.cpp:98`, `ControlSchema.cpp:56`). Every caller and the sibling `set_playlist` (`ControlCommandsSessionPerform.cpp:161`) were checked for the same reliance.
- *No collateral damage.* No product code changed. The test restores the setlist to empty, as `theSetlistOpensItsProjects` does.
- *Realtime, undo and commands.* Not applicable: no product code changed, and the test runs on the control thread. No command was added and no undo step changed.
- *Registration.* No new file. `SessionPerformTest.cpp` was already registered in `tests/CMakeLists.txt` and `tests/fork-sources.txt` (the existing test for this area).
- *Gates.* Table above: RESULT PASS, via the driver because of the write-jail.
- *Environment notes.* ccache needed `CCACHE_TEMPDIR` pointing at a writable directory. `feature_check.py`'s log directory is outside the write-jail. Neither touches the product.

Files committed for this verdict: `tests/src/core/SessionPerformTest.cpp` and `docs/qwen-fixes-2026-10/cluster-2983.md`.
