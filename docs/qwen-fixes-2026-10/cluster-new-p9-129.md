FIXED — `m_needsUpdate` has no initializer, so the first `processImpl()` reads an indeterminate bool (cluster new-p9-129)

## Decision

REAL. Fixed by initialising the member to `false`, the state the constructor leaves.

## Evidence (file:line, current branch base)

- `plugins/DynamicsProcessor/DynamicsProcessor.h:65` declared `bool m_needsUpdate;` with no initializer. It is now `bool m_needsUpdate = false;`.
- `plugins/DynamicsProcessor/DynamicsProcessor.cpp:63-73` (constructor) never assigns `m_needsUpdate`. `new DynProcEffect(...)` does not zero members, so the first read is an indeterminate `bool`, which is undefined behaviour.
- `plugins/DynamicsProcessor/DynamicsProcessor.cpp:117` reads the flag at the top of `processImpl()`, before any write on the first period.
- The only writers are `plugins/DynamicsProcessor/DynamicsProcessorControls.cpp:63` (sets `true` on `sampleRateChanged`) and `DynamicsProcessor.cpp:123` (clears it after the update branch). Both stay as they are.
- The constructor already sizes the RMS helpers and computes the attack/release coefficients for the current rate (`DynamicsProcessor.cpp:69-72`), so "no pending update" is the state it leaves. That is `false`.
- Observable before the fix: with the allocation poisoned (`0xA5`), the flag reads 165 right after construction.

Impact note: at an unchanged sample rate, a true flag takes the update branch. `RmsHelper::setSize` with the same size reuses its buffer (`include/RmsHelper.h:86-91`), so the recomputed values are identical and the normal output does not change. The defect is an undefined read of a control flag on the audio path, and the fix removes it.

## Regression test

`tests/src/plugins/PluginPortsMigrationTest.cpp`, new test `dynamicsProcessorNeedsUpdateStartsCleared`:
- Poisons the `DynProcEffect` allocation with the existing `allocationSizeToPoison` seam (same pattern as `dualFilterCutoffCachesStartInitialized`).
- Loads the migrated module with `QLibrary`, skipped when `testHostCanLoadPluginModules()` is false (same as the existing `dynamicsProcessorRejectsShortWaveShape`).
- Reads the raw byte of `m_needsUpdate` through `DynProcEffectTestAccess::needsUpdateByte`. That accessor is defined in the test and reaches the private member via `#define private public` around the header include, the same pattern as the MultitapEcho include in the same file.
- Deterministic: no timing, sleeps or races.

## Commands and EXIT codes (logs in the LOG FOLDER)

| Step | Command (run from the stated directory) | EXIT | Log |
| --- | --- | --- | --- |
| RED build (fix absent, test added) | `env CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target PluginPortsMigrationTest -j4` | 0 | build-2-red.log |
| RED run | `./PluginPortsMigrationTest dynamicsProcessorNeedsUpdateStartsCleared` (in build/tests) | **1** — `Actual 165, Expected 0`, Loc PluginPortsMigrationTest.cpp(3769) | test-1-red.log |
| GREEN build (fix) | same cmake command | 0 | build-3-green.log |
| GREEN run | same test binary | **0** — 3 passed, 0 failed | test-2-green.log |
| 4b save fix | `git diff -- plugins/DynamicsProcessor/DynamicsProcessor.h > logs/fix.patch` | 0 | — |
| 4b revert | `git apply -R logs/fix.patch` | 0 | — |
| 4b RED build | cmake build as above | 0 | build-4-unfixed.log |
| 4b RED run | test binary | **1** — `Actual 165` | test-3-red-reverted.log |
| 4b restore | `git apply logs/fix.patch` | 0 | — |
| 4b GREEN build | cmake build as above | 0 | build-5-fixed.log |
| 4b GREEN run | test binary | **0** | test-4-green-reapplied.log |

Affected family and related checks:

| Check | Command | EXIT | Result |
| --- | --- | --- | --- |
| ctest family | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (build/tests) | 0 | `100% tests passed out of 1` (test-6-family-after-swh.log) |
| Full binary | `./PluginPortsMigrationTest` (build/tests) | 0 | 84 passed, 0 failed, 2 skipped (test-7-family-totals.log) |
| DynamicsProcessor reference names | `ctest -N -R DynamicsProcessor` (build/tests) | — | `Total Tests: 0` for the reference names (`DynamicsProcessorControls`, `DynamicsProcessor`, `DynamicsProcessorControlDialog`): none are registered in this build, so none were run |

Earlier family run (test-5-family.log) exited 8: `nonRealtimeLadspaEffectsAreInvalid` reported the SWH Decimator fixture as not loaded. `build/plugins/ladspa/decimator_1202.so` was missing because my first build attempt hit ccache permission errors on the SWH LADSPA targets. I built that target (build-6-swh-decimator.log, EXIT=0) and the rerun passed. Not a product change.

Gates:

| Gate | Command | EXIT |
| --- | --- | --- |
| feature_check.py (whole table) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 — could not run: it writes logs to `<worktree>/../<name>-feature-check`, which is outside the write-jail (`PermissionError`, gates-1.log) |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity ratchet | `bash tests/complexity-gate.sh --check` | 1 — tree-wide regressions (7 REGRESSION lines) in files this branch did not change |
| file-length ratchet | `bash tests/file-length-gate.sh --check` | 1 — tree-wide regressions (22 REGRESSION lines) in files this branch did not change |

For the two ratchets, feature_check only fails on REGRESSION items in changed files. None of the lines mention `DynamicsProcessor.h`, `PluginPortsMigrationTest.cpp` or `upstream-modifications.txt`. The test file and header add no new function over CCN 10 and no new file. Logs: gate-complexity.log, gate-file-length.log.

Files committed: `plugins/DynamicsProcessor/DynamicsProcessor.h`, `tests/src/plugins/PluginPortsMigrationTest.cpp`, `tests/upstream-modifications.txt` (new line for the header, with reason), `docs/qwen-fixes-2026-10/cluster-new-p9-129.md`.

Prior attempts: `git log` on this branch shows no earlier commits for cluster new-p9-129. The checkpoint recorded no file changes from the earlier rounds, so there was nothing to reconcile.

## Deviations from the agreed test matrix

1. **MemorySanitizer not run.** The build tree has no MSan configuration and `valgrind` is not installed. The test uses the deterministic poisoned allocation and a raw byte read instead. It fails with the bug and passes with the fix, as the RED/GREEN runs show.
2. **First-block render, allocation-failure and realtime-watch checks not added.** Before the fix, at an unchanged rate, the first block produces the same output and makes no allocations (the RMS helper reuses its buffer). Those checks would pass on the buggy code too, so they cannot prove the regression. The observable defect is the indeterminate flag, which the test checks directly. The fix itself adds an initializer only: no new code on the audio path and no change to construction-time allocation.
3. **Not in-process; Windows not exercised.** `DynamicsProcessor` is built as a plugin module (`plugins/DynamicsProcessor/CMakeLists.txt`, `BUILD_PLUGIN`) and is not linked into `lmmsobjs`, so the test loads it through `QLibrary`. It is skipped on hosts where `testHostCanLoadPluginModules()` is false, the same as the existing DynamicsProcessor test. I did not build or run anything on Windows.
4. **Sibling not fixed (out of scope by rule).** `tests/reference/DynamicsProcessor/DynamicsProcessor.h:66` and `.cpp:117-123` are a reference mirror of the plugin with the same uninitialised `bool m_needsUpdate;`. The brief said to keep `tests/reference` as it is, so it is unchanged. Whoever owns the reference mirror should apply the same one-line initializer.
5. **Build workaround (environment, not product).** ccache's temp directory is set to a path this sandbox denies, so the builds used `env CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp`.

## Self-check

- **Test fails with the product change reverted and passes with it:** yes. RED `EXIT=1` (test-1-red.log, test-3-red-reverted.log); GREEN `EXIT=0` (test-2-green.log, test-4-green-reapplied.log). Exit codes above.
- **Deterministic:** the test uses the existing allocation-poisoning seam and a byte read of the flag. No timing, sleeps or races.
- **Fix at the cause and complete:** the defect is the missing initial value of the single flag. Its readers and writers (`DynamicsProcessor.cpp:117,123`, `DynamicsProcessorControls.cpp:63`) are consistent with an initial `false`, because the constructor already computed the state for the current rate. The reference mirror is not changed (see deviation 4).
- **No collateral damage:** the change only affects the initial value of one private flag. No saved data, limits or formats change. The full `PluginPortsMigrationTest` binary passes (84 passed, 0 failed, 2 skipped, EXIT 0).
- **Audio thread and undo/commands:** no new code runs on the audio thread. The fix is an initializer. No command or undo behaviour changed.
- **Old projects and saved data load as before:** yes, nothing in the load path changed.
- **Portability:** plain C++ member initializer, no platform code. The test is skipped on non-module hosts, as its neighbours are (deviation 3).
- **Sibling commands:** not applicable (no command code changed).
- **Gates:** six of eight gate scripts exit 0. feature_check.py itself could not write its log folder. The two ratchet scripts exit 1 on tree-wide items in files this branch did not change (see the gates table).
