ALREADY-FIXED

## Finding and target evidence

The finding is REAL on the accepted train-9 source branch: commit `9a23087ec` adds an equality guard to `mts.master_set` before `control::addUndoStep`. On this port target, pinned at `aac861e62bba78448c3ff14e7077cc7c9712e4f9`, the same behavior already exists in `src/core/ControlCommandsMts.cpp:325-336`. `setMtsMaster` refusal returns first; an unchanged flag returns success at line 333 before undo recording at line 336 or transaction creation; changed flags still record one step. The accepted equality guard is semantically identical to the target's existing code, so no production behavior change is needed.

The three-way conflict in `tests/src/core/SessionTuningTest.cpp` is resolved by preserving the train-15 test body and adding `repeatedMasterSetLeavesTheRealEditForUndo` at line 1401. The MTS reversibility explanation now documents the no-step case at `src/core/ControlReversibilityTableMts.cpp:115`. The test file's modification is recorded in `tests/upstream-modifications.txt`; no new test source or control command was added.

## Affected paths and hazards

| Path | Expected behavior and evidence |
|---|---|
| Changed `enabled` value | False-to-true with MTS-ESP available and true-to-false continue through `addUndoStep` and the transaction. The target's existing present-library test branch checks publication state and undo restores the arm. |
| Same-value false | `setMtsMaster(false)` succeeds, then the handler returns the current state before creating an undo step or transaction. The absent-library test branch checks unchanged depth. |
| Same-value true / repeated arm | The handler still calls `setMtsMaster(true)` before checking equality, preserving MTS-ESP re-registration. The early return then avoids a no-op step. The installed-library branch is conditional; this environment did not have the optional library, so this branch was source-reviewed, not executed here. |
| MTS-ESP arm refusal | `src/core/SessionTuningMtsEsp.cpp:91-99` refuses an arm when the optional IPC library is absent. `masterSet` returns that refusal before its equality check and before undo recording. The test checks the typed error, state, and undo depth. |
| Invalid command args | The `enabled` schema requires a boolean; `ControlRegistry` validates args before invoking the handler (`src/core/ControlRegistry.cpp:251`). No schema behavior changed. |
| Undo regression | The new test records `mts.set_note`, repeats `mts.master_set enabled=false`, and checks that one undo restores the earlier table. The no-guard run fails the depth assertion before reaching the table comparison; the restored-guard run reaches and passes that comparison. |
| Sibling `mts.*` verbs and GUI caller | `mts.get_state` is read-only; `mts.load_scale`, `mts.load_keymap`, `mts.set_tuning`, `mts.set_note`, and `mts.reset` keep their existing `tableMutation` snapshot path. `SessionTuningMenu.cpp:123-127` invokes `mts.master_set` and rereads state without relying on an undo transaction. |
| Persistence and real time | No project serialization or audio-thread code changed. The guard is in the control-command handler; it adds no audio-thread allocation, lock, wait, or log. No header or Qt/platform API changed. |
| Supported platforms | Only the configured Linux build was run. MTS-ESP remains optional; the existing test selects its installed/absent path with `mtsEspLibraryPresent()`. No platform-specific code was introduced. |
| Undo exhaustion | The unchanged path now exits before `addUndoStep`; changed-state behavior and its existing one-step path remain the same. No new journal capacity check or resource limit was added. |

The test target exists: `tests/CMakeLists.txt:313` registers `SessionTuningTest.cpp` and line 2689 wires its `tripleoscillator` dependency. The executed ctest family contained one ctest entry and 26 QTest cases.

## Regression evidence

The target already had the accepted guard, so I tested the merged regression against a temporary local no-guard version, then restored the exact target code. This counterfactual is separate from the port result: the final `src/core/ControlCommandsMts.cpp` has no diff from `HEAD`.

| Run | Command | EXIT | Result |
|---|---|---:|---|
| Build with target guard | `env -u MAKEFLAGS -u MFLAGS CCACHE_TEMPDIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-20-port-port.r1/logs/ccache-tmp cmake --build build --target zene SessionTuningTest -j4` (`build-1.log`) | 0 | Built `zene` and `SessionTuningTest`. |
| GREEN | `ctest -R SessionTuningTest --output-on-failure` from `build/tests` (`test-1.log`) | 0 | Focused family passed. |
| GREEN count | `ctest -R SessionTuningTest -V` from `build/tests` (`test-2-verbose.log`) | 0 | 26 passed, 0 failed, 0 skipped. |
| Save temporary no-guard diff | `git diff -- src/core/ControlCommandsMts.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-20-port-port.r1/logs/fix.patch` | 0 | 16-line patch; temporary guard removal only. |
| Build without guard | Same build command (`build-2-no-guard.log`) | 0 | Built the counterfactual source. |
| RED | `ctest -R SessionTuningTest --output-on-failure` from `build/tests` (`test-3-no-guard-red.log`) | 8 | `masterSetReportsItsOwnMtsEspState`: actual depth 1, expected 0 at line 1396. `repeatedMasterSetLeavesTheRealEditForUndo`: actual 3, expected 2 at line 1420. Totals: 24 passed, 2 failed. |
| Restore target guard | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-20-port-port.r1/logs/fix.patch` | 0 | Restored the exact inherited guard. |
| Rebuild restored target | Same build command (`build-3-restored.log`) | 0 | Built `zene` and `SessionTuningTest`. |
| GREEN | `ctest -R SessionTuningTest --output-on-failure` from `build/tests` (`test-4-restored-green.log`) | 0 | Focused family passed. |
| GREEN count | `ctest -R SessionTuningTest -V` from `build/tests` (`test-5-restored-verbose.log`) | 0 | 26 passed, 0 failed, 0 skipped. |

All build and test output is in the required log folder. The `fff` grep tool was not exposed in this session (`command -v fff` returned no path); I used targeted `rg` searches on the log files and did not dump full build logs. No build log contained `error:`, `FAILED`, or `Assertion` matches.

## Gate results

The first required feature check (`gates-1-feature-check.log`) exited 1 in `fork-sources` and `all-sources-reproduce`. Both reported the same stale inventory: `tests/all-sources.txt` included 1,148 vendored sfizz/Abseil entries that its header recipe excludes. I regenerated only the entry list from that documented recipe (the header is unchanged); the repeated check passed all 8 gates. The first failure was repository inventory drift, not a source or test failure.

| Gate | Result |
|---|---|
| fork-sources | PASS |
| all-sources-reproduce | PASS |
| no-upstream-regression | PASS |
| check-namespace | PASS |
| no-tautology | PASS |
| doc-figures | PASS |
| complexity | PASS; no new item in changed files |
| file-length | PASS; no new item in changed files |

Commands and exit codes:

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-new-p15-20-aac861e62b` — first run EXIT 1 (`gates-1-feature-check.log`), rerun after regeneration EXIT 0 (`gates-2-feature-check.log`).
- Manifest recipe from `tests/all-sources.txt`: `{ git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/|plugins/SfzPlayer/sfizz/)' ; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } | LC_ALL=C sort -u` — EXIT 0; output was written to the run log folder and copied over the manifest's entry list.
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-new-p15-20-aac861e62b new-p15-20-port-port` — EXIT 0 on the committed tree (`submit-check-1.log`).

## Self-check

- **Verdict evidence:** ALREADY-FIXED on this target. `src/core/ControlCommandsMts.cpp:333` returns on equality before the `addUndoStep` call at line 336; this is the accepted guard from `9a23087ec`.
- **RED/GREEN:** the merged test fails with the guard removed (ctest EXIT 8; two undo-depth assertions fail) and passes with the target guard restored (ctest EXIT 0; 26 QTest cases pass). The no-guard run stops before the table-restoration assertion; the green run executes it successfully.
- **Determinism:** no sleeps, clocks, races, or timing assumptions. The tests compare journal depth and before/after table snapshots in one process.
- **Affected paths:** refusal, idempotent disarm, changed-state undo, repeated-arm ordering, schema validation, sibling MTS table verbs, and the GUI command caller are covered above. The optional installed-library branch was not run because the configured Linux environment reports MTS-ESP absent.
- **Real-time and saved data:** no audio-thread code, project serialization, or saved-data format changed. No new command, header, or product source file was added.
- **Registration:** `SessionTuningTest.cpp` is already in `tests/CMakeLists.txt`; its inherited-file modification is recorded in `tests/upstream-modifications.txt`. The all-sources manifest was regenerated by its own recipe to restore the declared third-party exclusion.
- **Files committed:** `docs/qwen-fixes-2026-10/cluster-new-p15-20-port.md`, `docs/qwen-fixes-2026-10/cluster-new-p15-20-port-port.md`, `src/core/ControlReversibilityTableMts.cpp`, `tests/all-sources.txt`, `tests/src/core/SessionTuningTest.cpp`, and `tests/upstream-modifications.txt`.
