# Cluster new-p8-311: A large valid zoom step can overflow

Verdict: **FIXED**

## Evidence

- In `src/gui/ControlCommandsArrangerView.cpp:145-152`, `requestedZoom()` previously added the current zoom and `step` as signed `int`s before clamping. A valid `INT_MAX` step overflowed when the current zoom was positive. The calculation now uses `long long`, clamps against widened model bounds, and converts to `int` only after the result is in range.
- `tests/src/gui/ArrangerTimelineTest.cpp:203-210` adds `anOversizedZoomStepClampsToMaximum()`. It invokes the registered `arrangement.zoom` command with `std::numeric_limits<int>::max()` and checks both the reply and model equal the maximum zoom.
- The test is in the fork-created `ArrangerTimelineTest.cpp` (absent at fork base `4e677cb6c6ab`). The changed product source is already registered in `tests/fork-sources.txt:1642`; no inherited-file ledger entry or new source registration was needed.

## Regression and gates

The affected ctest family is `ArrangerTimelineTest`: **1 registered ctest test**. The RED runs executed 13 Qt test cases (12 passed and the new assertion failed); the GREEN runs passed the registered test (1/1).

The test failed before the fix with ctest exit 8, passed with the fix with exit 0, failed again after removing only the production patch, and passed after reapplying it. The complexity and file-length gates both exited 0.

The session did not expose an `fff grep` tool, so log checks used focused `rg` searches against the requested log folder rather than reading complete logs.

## Commands and exit codes

All build, test, and gate output was written to a new log in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-311.r1/logs`; each log includes its captured `EXIT=` code.

| Phase | Command | EXIT |
| --- | --- | ---: |
| Pre-fix build | `cmake --build build --target zene ArrangerTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-311.r1/logs/build-1.log 2>&1` | 0 |
| Initial RED | `cd build/tests && ctest -R ArrangerTimelineTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-311.r1/logs/test-1.log 2>&1` | 8 |
| Fixed build | `cmake --build build --target zene ArrangerTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-311.r1/logs/build-2.log 2>&1` | 0 |
| Initial GREEN | `cd build/tests && ctest -R ArrangerTimelineTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-311.r1/logs/test-2.log 2>&1` | 0 |
| Remove production patch | `git diff -- src/gui/ControlCommandsArrangerView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-311.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-311.r1/logs/fix.patch` | 0 |
| Proof RED build | `cmake --build build --target zene ArrangerTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-311.r1/logs/build-3.log 2>&1` | 0 |
| Proof RED | `cd build/tests && ctest -R ArrangerTimelineTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-311.r1/logs/test-3.log 2>&1` | 8 |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-311.r1/logs/fix.patch` | 0 |
| Proof GREEN build | `cmake --build build --target zene ArrangerTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-311.r1/logs/build-4.log 2>&1` | 0 |
| Proof GREEN | `cd build/tests && ctest -R ArrangerTimelineTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-311.r1/logs/test-4.log 2>&1` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-311.r1/logs/complexity-gate-1.log 2>&1` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-311.r1/logs/file-length-gate-1.log 2>&1` | 0 |
