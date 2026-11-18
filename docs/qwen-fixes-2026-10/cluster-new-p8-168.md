# Cluster new-p8-168

**Verdict: FIXED**

`ModulationLayerPublisher::publishTriggers()` cleared the voice pool only when every per-voice modulator disappeared. When one modulator changed tracks while another remained per-voice, notes from the abandoned track survived; their later note-offs were ignored. Publishing now scans the fixed-size voice pool and removes voices whose track no active per-voice source follows, while retaining voices on still-followed tracks. This is a bounded scan and does not allocate or lock.

## Evidence

| Evidence | Result |
| --- | --- |
| Production change | `src/core/ModulationLayerPublisher.cpp:43-59,94-95` prunes voices against the newly published trigger tracks. |
| Regression | `tests/src/core/ModulationAudioViewTest.cpp:183-214` starts voices on tracks 11 and 22, changes the first follower to 33, sends the now-ignored note-off for 11, switches back to 11, and asserts only track 22 remains. |
| RED before fix | `ctest -R '^ModulationAudioViewTest$' --output-on-failure` from `build/tests`; exit 8. The assertion `!staleVoice` failed. |
| GREEN after fix | `ctest -R 'Modulation|Modulator|ControlNoteExpressionCommandsTest' --output-on-failure` from `build/tests`; exit 0, 9/9 tests passed. |
| Patch-removal proof | The same focused regression failed with exit 8 after removing the production patch and rebuilding, then passed with exit 0 after restoring the patch and rebuilding. |
| Complexity gate | `bash tests/complexity-gate.sh --check`; exit 0. |
| File-length gate | `bash tests/file-length-gate.sh --check`; exit 0. |

The `fff` grep tool was not available in this session; build and test logs were inspected with `rg` searches. All build, test, and gate output is under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-168.r1/logs`.

## Commands and exit codes

Commands ran from the worktree root unless a different working directory is stated. The `ctest` commands ran from `build/tests`.

1. Initial build before the first RED run — exit 0:

   `cmake --build build --target zene ModulationAudioViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-168.r1/logs/build-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0`

2. Regression against the original production code — exit 8 (RED):

   `ctest -R '^ModulationAudioViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-168.r1/logs/test-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0`

3. Build `zene` and the first modulation family targets — exit 0:

   `cmake --build build --target zene ModulationLayerValueTest ModulationLayerTest ModulationAudioViewTest ControlModulatorCommandsTest ControlNoteExpressionCommandsTest ModulationLayerProjectRoundTripTest ModulatorToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-168.r1/logs/build-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0`

4. First family run — exit 8; 7 tests passed, while `ModulationRateTest` and `ModulatorPanelTest` were not run because their executables had not been built:

   `ctest -R 'Modulation|Modulator|ControlNoteExpressionCommandsTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-168.r1/logs/test-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0`

5. Build those two matched family targets — exit 0:

   `cmake --build build --target ModulationRateTest ModulatorPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-168.r1/logs/build-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0`

6. Complete family run — exit 0, 9/9 passed:

   `ctest -R 'Modulation|Modulator|ControlNoteExpressionCommandsTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-168.r1/logs/test-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0`

7. Save and remove only the production patch for the negative control — exit 0:

   `git diff -- src/core/ModulationLayerPublisher.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-168.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-168.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit 0`

8. Rebuild without the fix — exit 0:

   `cmake --build build --target zene ModulationAudioViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-168.r1/logs/build-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0`

9. Identical regression without the fix — exit 8 (RED):

   `ctest -R '^ModulationAudioViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-168.r1/logs/test-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0`

10. Restore the production patch — exit 0:

    `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-168.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit 0`

11. Rebuild with the fix restored — exit 0:

    `cmake --build build --target zene ModulationAudioViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-168.r1/logs/build-5.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0`

12. Identical regression with the fix restored — exit 0 (GREEN):

    `ctest -R '^ModulationAudioViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-168.r1/logs/test-5.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0`

13. Complexity gate — exit 0:

    `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-168.r1/logs/gate-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0`

14. File-length gate — exit 0:

    `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-168.r1/logs/gate-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0`

## Files in the fix commit

- `src/core/ModulationLayerPublisher.cpp`
- `tests/src/core/ModulationAudioViewTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-168.md`
