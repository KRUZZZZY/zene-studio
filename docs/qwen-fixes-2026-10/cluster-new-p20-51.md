new-p20-51: ALREADY-FIXED
Evidence: `src/core/HardwareInsert.cpp:409` checks `toFloat(&mixOk)` and falls back to `Settings{}.mix`; the existing regression is `tests/src/core/HardwareIoTest.cpp:465` (`unparsableSavedLevelsUseTheirDefaults`). Commit `0665cd09422a77cf219160a3a6bac66aa4679857` already introduced this correction.
RED/GREEN: Temporarily restored the old `toFloat()` parser; `HardwareIoTest::unparsableSavedLevelsUseTheirDefaults` failed (36 passed, 1 failed). Restored the checked parser; the test family passed.
Commands:
- `cmake --build build --target HardwareIoTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-51.r1/logs/015-build-mix-reversal.log 2>&1` — EXIT 0.
- From `build/tests`: `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-51.r1/logs/016-test-mix-reversal.log 2>&1` — EXIT 8; the malformed-mix assertion failed.
- `cmake --build build --target HardwareIoTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-51.r1/logs/017-build-mix-reapply.log 2>&1` — EXIT 0.
- From `build/tests`: `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-51.r1/logs/018-test-mix-reapply.log 2>&1` — EXIT 0.

new-p17-59: FIXED
Evidence: `include/HardwareInsert.h:86-89` carries measurement route/rate context. `src/core/HardwareInsert.cpp:140-159` validates that context when applying settings; `:216-245` invalidates a measurement when processing observes a different sample rate; `:393` saves `measured_rate`; `:409-414` loads it and discards a mismatched measurement. Legacy projects without the attribute keep the former interpretation by associating the measurement with the current rate. Regressions at `tests/src/core/HardwareIoTest.cpp:382`, `:408`, and `:428` cover save/load at a different rate, a live rate change, and both send/return route changes.
RED/GREEN: The final product-only reversal caused the three measurement-context tests to fail. Reapplying `fix.patch` made the same test family pass.
Commands:
- `git diff -- include/HardwareInsert.h src/core/HardwareInsert.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-51.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-51.r1/logs/fix.patch && cmake --build build --target HardwareIoTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-51.r1/logs/038-build-final-red.log 2>&1` — EXIT 0.
- From `build/tests`: `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-51.r1/logs/039-test-final-red.log 2>&1` — EXIT 8; 33 passed, 4 failed.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-51.r1/logs/fix.patch && cmake --build build --target HardwareIoTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-51.r1/logs/040-build-final-green.log 2>&1` — EXIT 0.
- From `build/tests`: `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-51.r1/logs/041-test-final-green.log 2>&1` — EXIT 0.

new-p14-119: FIXED
Evidence: `src/core/HardwareInsert.cpp:149-151` sends the ping to Idle when either route changes. The deterministic regression at `tests/src/core/HardwareIoTest.cpp:518` exercises send and return changes during Waiting, then verifies a late result from the old ping cannot publish into the new route's settings. It uses no sleep or race.
RED/GREEN: In the final product-only reversal, the late `publishPingResult` succeeded and the route-change assertion failed; after reapplying `fix.patch`, publication was refused and the family passed. The RED/GREEN commands and exit codes are the same final commands listed under `new-p17-59` because both fixes share `setSettings` and the product-only patch.
Commands:
- `git diff -- include/HardwareInsert.h src/core/HardwareInsert.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-51.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-51.r1/logs/fix.patch && cmake --build build --target HardwareIoTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-51.r1/logs/038-build-final-red.log 2>&1` — EXIT 0.
- From `build/tests`: `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-51.r1/logs/039-test-final-red.log 2>&1` — EXIT 8; the old-route response published successfully.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-51.r1/logs/fix.patch && cmake --build build --target HardwareIoTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-51.r1/logs/040-build-final-green.log 2>&1` — EXIT 0.
- From `build/tests`: `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-51.r1/logs/041-test-final-green.log 2>&1` — EXIT 0.

## Gate and family evidence

- Final family run: `ctest -R '^HardwareIoTest$' -V` from `build/tests` — EXIT 0; 1/1 ctest target and 37 QtTest cases passed (`042-test-final-verbose.log`).
- Final `feature_check.py` — EXIT 0; all eight rows PASS (see the final numbered feature-check log). The first run found the branch's inherited `tests/all-sources.txt` contained 1,148 vendored sfizz entries excluded by its own recipe. I re-derived that manifest from its header recipe and recorded the reason in `tests/upstream-modifications.txt`; the registration and reproduction gates then passed.
- Test matrix: `000-test-matrix.md` in the run log folder. No new audio-thread allocation, free, lock, wait, or log was added; sample-rate validation uses lock-free atomics. Existing settings snapshots retain route/rate context, so undo restores a measurement only when that context still matches.
- The existing product and test sources are already registered in `tests/fork-sources.txt`; no new product source file was added.

## Self-check

- Each finding has an independent verdict and regression: p20 uses the existing malformed-mix regression; p17 has route, saved-rate and live-rate assertions; p14 has a deterministic late-response check for both routes.
- The `HardwareIoTest` family ran 37 cases with no skips; product-only reversal failed the new p17/p14 cases, and restoration passed them.
- Existing saved projects without `measured_rate` retain their prior load behavior. Explicit latency overrides continue to take precedence, and negative unknown-measurement sentinels are preserved.
- Realtime review: `process()` only performs bounded atomic operations and scalar comparisons for the new invalidation/cancel paths. Persistence and settings normalization remain outside the audio thread.
- Feature gates and source registration were checked after the manifest was re-derived; no gate limits or baselines were changed.
