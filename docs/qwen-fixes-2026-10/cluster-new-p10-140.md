# Cluster new-p10-140: DrumSynth noise-band step

**Verdict: FIXED**

The finding is real. With the default noise-band frequency and `dF=110`, the derived `BFStep` can be zero. An enabled band then evaluates `t % BFStep` during rendering. Both derived steps are now clamped to at least one before their modulo use.

## Evidence

- [DrumSynth.cpp:487](../../src/core/DrumSynth.cpp#L487) derives `BFStep`; [line 488](../../src/core/DrumSynth.cpp#L488) clamps it. The second band is clamped at [line 502](../../src/core/DrumSynth.cpp#L502). The modulo uses are at [lines 670](../../src/core/DrumSynth.cpp#L670) and [696](../../src/core/DrumSynth.cpp#L696).
- [ImportDetectionTest.cpp:206](../../tests/src/core/ImportDetectionTest.cpp#L206) writes a minimal DS fixture with the noise band enabled, default frequency, and `dF=110`, then calls `DrumSynth::GetDSFileSamples`.
- [upstream-modifications.txt:1054](../../tests/upstream-modifications.txt#L1054) records the inherited source change and its regression.

The ctest family executed **1 test out of 1 registered**. The pre-fix test and the proof run with the production patch removed both terminated with `SIGFPE`; the fixed test passed.

## Commands and recorded results

Build commands ran from the worktree root. Ctest commands ran from `build/tests`. Build and test output was written to the specified run log folder.

| Phase | Exact command | Exit | Result |
|---|---|---:|---|
| Original-code test build | `cmake --build build --target ImportDetectionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-140.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result` | 0 | Built the regression before the production fix. |
| RED, before fix | `ctest -R ImportDetectionTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-140.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result` | 8 | `SIGFPE` while rendering. |
| Fixed zene build | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-140.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |  |
| Fixed test build | `cmake --build build --target ImportDetectionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-140.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result` | 0 |  |
| GREEN | `ctest -R ImportDetectionTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-140.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result` | 0 | 1/1 test passed. |
| Save patch and remove fix | `git diff -- src/core/DrumSynth.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-140.r1/logs/fix.patch; patch_status=$?; echo DIFF_EXIT=$patch_status; git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-140.r1/logs/fix.patch; reverse_status=$?; echo REVERSE_EXIT=$reverse_status` | 0 / 0 | Captured the product patch and applied it in reverse. |
| Reverse-proof zene build | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-140.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result` | 0 |  |
| Reverse-proof test build | `cmake --build build --target ImportDetectionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-140.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result` | 0 |  |
| RED, fix removed | `ctest -R ImportDetectionTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-140.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result` | 8 | `SIGFPE`; confirms the regression catches the defect. |
| Reapply fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-140.r1/logs/fix.patch; result=$?; echo APPLY_EXIT=$result` | 0 |  |
| Restored zene build | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-140.r1/logs/build-6.log 2>&1; result=$?; echo EXIT=$result` | 0 |  |
| Restored test build | `cmake --build build --target ImportDetectionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-140.r1/logs/build-7.log 2>&1; result=$?; echo EXIT=$result` | 0 |  |
| GREEN, fix restored | `ctest -R ImportDetectionTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-140.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result` | 0 | 1/1 test passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-140.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result` | 0 |  |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-140.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |  |

## Files in the commit

- `src/core/DrumSynth.cpp`
- `tests/src/core/ImportDetectionTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-140.md`
