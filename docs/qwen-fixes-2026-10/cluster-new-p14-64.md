**Verdict: FIXED**

# Cluster new-p14-64: Applying a selection with no surviving clips leaves old clips selected

**Verdict: FIXED.** In `src/gui/ControlCommandsViewMemory.cpp:366`, `applySelection` computed `clipsRestored = !live.isEmpty() && selectInArranger(live)`, so when no saved clip id still resolves, `selectInArranger` was never called and the arranger's previous selection stayed highlighted. The control surface's selected clip was cleared via `selectClip(QString())`, but the arranger was not. Fix: call `selectInArranger(live)` unconditionally so an empty `live` clears every clip view; keep `clipsRestored` false for the empty case.

Regression test added to `tests/src/gui/ArrangerKeysNavigatorTest.cpp` (`applyingSelectionWithNoSurvivingClipsClearsArrangerSelection`): saves a selection for one clip, deletes that clip, highlights a different clip in the arranger, applies the saved selection, and asserts no clip view remains selected. It FAILS on the original code and PASSES after the fix.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-64.r1/logs`

| Step | Command | EXIT |
| --- | --- | --- |
| build (with fix) | `cmake --build build --target zene ArrangerKeysNavigatorTest` (`build-test-1.log`) | 0 |
| GREEN test | `cd build/tests && ./ArrangerKeysNavigatorTest -v2 applyingSelectionWithNoSurvivingClipsClearsArrangerSelection` (`test-green-1.log`) | 0 |
| 4b revert | `git apply -R .../logs/fix.patch` then `cmake --build build --target zene ArrangerKeysNavigatorTest` (`build-red-1.log`) | 0 |
| RED test | `cd build/tests && ./ArrangerKeysNavigatorTest -v2 applyingSelectionWithNoSurvivingClipsClearsArrangerSelection` (`test-red-1.log`) | 1 |
| 4b reapply | `git apply .../logs/fix.patch` then build (`build-green-2.log`) | 0 |
| GREEN test | `cd build/tests && ./ArrangerKeysNavigatorTest -v2 applyingSelectionWithNoSurvivingClipsClearsArrangerSelection` (`test-green-2.log`) | 0 |
| family | `cd build/tests && ctest -R ArrangerKeysNavigatorTest --output-on-failure` (`ctest-family-1.log`) | 0 |
| gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`gate-1.log`) | 0 |

## Self-check

- Affected family: 1 test executed, 1 passed.
- Fix touches only `src/gui/ControlCommandsViewMemory.cpp`; test touches `tests/src/gui/ArrangerKeysNavigatorTest.cpp`.
- No new control commands; no audio-thread changes.
