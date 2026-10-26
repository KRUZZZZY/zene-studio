ALREADY-FIXED
new-p23-150: ALREADY-FIXED

Evidence: `src/gui/MicrotunerConfig.cpp:113` connects the Apply button to `applyScale()`, and `src/gui/MicrotunerConfig.cpp:396-399` calls `validateScaleForm()` before parsing or constructing intervals. The guard is present in the branch before this batch. The new `manualApplyRejectsInvalidIntervals` test drives the button with `abc` and an out-of-range cents value, checks that a validation message appears, and verifies that the Song/session scale is unchanged.

RED/GREEN proof: Because the guard was already in the branch, a temporary controlled mutant removed that call. The mutant build succeeded and the test failed because validation did not open its message. Reversing the mutant patch restored the guard; the rebuilt test passed.

- `git diff -- src/gui/MicrotunerConfig.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/150-mutant.patch` — EXIT=0.
- `cmake --build build --target zene MicrotunerScaleFileTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/19-build-150-mutant.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/19-build-150-mutant.log; echo EXIT=$rc` — EXIT=0.
- `./MicrotunerScaleFileTest manualApplyRejectsInvalidIntervals > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/20-manual-apply-150-mutant-red.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/20-manual-apply-150-mutant-red.log; echo EXIT=$rc` (from `build/tests`) — EXIT=1; the assertion reports that no validation message appeared.
- `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/150-mutant.patch` — EXIT=0.
- `cmake --build build --target zene MicrotunerScaleFileTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/22-build-final.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/22-build-final.log; echo EXIT=$rc` — EXIT=0.
- `./MicrotunerScaleFileTest manualApplyRejectsInvalidIntervals > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/23-manual-apply-final-green.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/23-manual-apply-final-green.log; echo EXIT=$rc` (from `build/tests`) — EXIT=0.

new-p23-153: FIXED

Evidence: `src/gui/MicrotunerConfig.cpp:515-548` now snapshots the form before loading a parsed scale, restores it if `applyScale()` fails, and writes a reason when the Song is absent. The existing contract remains intact for validation failures: the validator shows its own message, so the caller's error stays empty. `zLoadingScaleWithoutSongReportsFailureAndPreservesForm` verifies the valid-file/no-Song case returns false with an error and leaves the form unchanged.

RED/GREEN proof: The same test failed on the original implementation because the error was empty. The required reverse-patch proof also failed with the fix removed and passed after it was restored.

- `./MicrotunerScaleFileTest zLoadingScaleWithoutSongReportsFailureAndPreservesForm > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/5-load-no-song-red.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/5-load-no-song-red.log; echo EXIT=$rc` (from `build/tests`, before the production fix) — EXIT=1; the assertion reports the empty error.
- `git diff -- src/gui/MicrotunerConfig.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/fix.patch` — EXIT=0.
- `cmake --build build --target zene MicrotunerScaleFileTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/12-build-4b-reversed.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/12-build-4b-reversed.log; echo EXIT=$rc` — EXIT=0.
- `./MicrotunerScaleFileTest zLoadingScaleWithoutSongReportsFailureAndPreservesForm > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/13-load-no-song-4b-red.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/13-load-no-song-4b-red.log; echo EXIT=$rc` (from `build/tests`) — EXIT=1.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/fix.patch` — EXIT=0.
- `cmake --build build --target zene MicrotunerScaleFileTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/15-build-4b-restored.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/15-build-4b-restored.log; echo EXIT=$rc` — EXIT=0.
- `./MicrotunerScaleFileTest zLoadingScaleWithoutSongReportsFailureAndPreservesForm > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/16-load-no-song-4b-green.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-150.r1/logs/16-load-no-song-4b-green.log; echo EXIT=$rc` (from `build/tests`) — EXIT=0.

## Test matrix

| Path | Test/evidence |
| --- | --- |
| Normal load | `aValidScaleFileStillLoads` passes; the file still updates the Song scale and session scale. |
| Parser failures | Existing empty-file and missing-degree-count cases remain refused without changing the scale state. |
| Manual validation failures | `manualApplyRejectsInvalidIntervals` exercises an unparseable ratio and out-of-range cents, with the actual Apply button. |
| Post-parse apply failure and form recovery | The valid-file/no-Song case returns an error and restores the old name and interval text. |
| Exhaustion/reconfiguration | These functions have no bounded resource pool to exhaust. Repeated invalid manual inputs are isolated by restoring the original Song/session scale; a failed file load restores the previous form. |
| Audio thread | These are GUI-only paths. No render, process, mixer, or note-play code changed; the new form copies and messages run only during the GUI call. |
| Platform/build | Built `zene` and `MicrotunerScaleFileTest` in the configured Linux Qt build with the test's offscreen platform. No MSVC or macOS runner was available; changes use existing Qt Widgets/Qt Test APIs and add no platform-specific code. |

## Verification

- Final family command: `ctest -R '^MicrotunerScaleFileTest$' --output-on-failure` from `build/tests` — EXIT=0, 1/1 ctest targets passed. The direct test binary reports 7 passed, 0 failed (log `31-test-binary-full.log`).
- Final gate command: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-150` — EXIT=0; all 8 rows PASS in `32-feature-check-final-precommit.log`.
- Completion command: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-150 new-p23-150` — EXIT=0, PASS in `33-submit-check-first.log`.
- The first gate run exited 1 because `tests/all-sources.txt` contained vendored sfizz paths excluded by its own recipe. Re-deriving the manifest from that published recipe exited 0; the final fork-sources and all-sources-reproduce checks pass. The manifest update is recorded in `tests/upstream-modifications.txt`.
## Self-check

- `? Regression red/green`: new-p23-150's branch guard is already present at `src/gui/MicrotunerConfig.cpp:398`; removing it made the new test fail (EXIT=1), restoring it made the test pass (EXIT=0). For new-p23-153, reversing the only production hunk made the no-Song test fail (EXIT=1), and applying it again made the test pass (EXIT=0).
- `? Determinism`: the manual Apply test uses a zero-delay Qt event callback to close the validator's modal message inside its nested event loop; it uses no sleep or wall-clock wait. The no-Song condition is injected by `Engine::destroy()` after the valid test file is written.
- `? Completeness`: `applyScale()` callers include the Apply button, `loadScaleFile()`, and `saveScaleToFile()`; the existing validation guard remains shared. `loadScaleFile()` distinguishes a no-Song failure from validation failures already reported by their dialog and restores the prior form for every apply failure.
- `? Collateral behavior`: valid .scl loading and empty/name-only rejection still pass. No project serialization format, scale format, or control command changed; the failed-load rollback only affects a refused GUI load.
- `? Undo/realtime/commands`: no new command or undo step was added. No audio-thread code was changed; form snapshot/restore and error reporting happen on the GUI path.
- `? Platform`: verified on the configured Linux Qt build. No Qt 6-only API or platform-specific calls were introduced; MSVC and macOS were not built in this environment.
- Files committed: `src/gui/MicrotunerConfig.cpp`, `tests/src/gui/MicrotunerScaleFileTest.cpp`, `tests/upstream-modifications.txt`, `tests/all-sources.txt`, and this report.
