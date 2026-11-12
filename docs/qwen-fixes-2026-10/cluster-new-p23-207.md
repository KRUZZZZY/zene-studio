FIXED

The finding is real. `PatternClipView::paintEvent` divides by `totalTracks` at `src/gui/clips/PatternClipView.cpp:155`. For an empty pattern store this evaluates `0.0f / 0`, raising `FE_INVALID`. `src/core/main.cpp:593-604` enables `FE_INVALID` traps under `LMMS_DEBUG_FPE`, so painting an empty pattern clip can raise `SIGFPE` in that supported debug configuration. The original FALSE-POSITIVE conclusion overlooked this path.

The fix guards the empty-store calculation and assigns zero empty-track height when `totalTracks == 0`. A new case in `tests/src/gui/ArrangerTimelineTest.cpp` renders an empty-store pattern clip and checks the floating-point exception flag. The existing non-empty preview case continues to cover the ordinary rendering path. The guard changes no visible output for the empty-store case and does not touch project data or audio-thread code.

| Evidence | Command | Result |
| --- | --- | --- |
| Build before product fix | `cmake --build build --target zene ArrangerTimelineTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-207.r1/logs/qa-build-red-1.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Regression before product fix | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-207.r1/logs/qa-test-red-1.log 2>&1; echo EXIT=$?` (from `build/tests`) | EXIT=8; 38 passed, 1 failed. `raisedExceptions` was 1 instead of 0. |
| Build with fix | `cmake --build build --target zene ArrangerTimelineTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-207.r1/logs/qa-build-green-1.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Focused regression with fix | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-207.r1/logs/qa-test-green-1.log 2>&1; echo EXIT=$?` (from `build/tests`) | EXIT=0; 1/1 CTest target passed (39 QtTest cases). |
| Reverse only the product change | `git show HEAD:src/gui/clips/PatternClipView.cpp > src/gui/clips/PatternClipView.cpp; echo EXIT=$?` | EXIT=0 |
| Build with product change reversed | `cmake --build build --target zene ArrangerTimelineTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-207.r1/logs/qa-build-reversed-1.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Regression with product change reversed | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-207.r1/logs/qa-test-reversed-1.log 2>&1; echo EXIT=$?` (from `build/tests`) | EXIT=8; same assertion failed, 38 passed and 1 failed. |
| Restore staged product fix | `git checkout -- src/gui/clips/PatternClipView.cpp; echo EXIT=$?` | EXIT=0 |
| Rebuild and retest after restore | `cmake --build build --target zene ArrangerTimelineTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-207.r1/logs/qa-build-restored-1.log 2>&1; echo EXIT=$?` then `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-207.r1/logs/qa-test-restored-1.log 2>&1; echo EXIT=$?` (ctest from `build/tests`) | EXIT=0 for both; 1/1 CTest target passed (39 QtTest cases). |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-207` | EXIT=0; all eight rows PASS. |

## Self-check

- **Regression fails with product change reversed and passes restored:** `ArrangerTimelineTest::anEmptyPatternStorePaintDoesNotRaiseInvalidFloatException`; reversed ctest EXIT=8 due to `raisedExceptions == 1`, restored ctest EXIT=0. Build EXIT=0 in both states.
- **Deterministic:** the test constructs a fixed empty pattern store and renders once; it uses no sleeps, wall-clock values, races, or random inputs.
- **Fix at the cause and complete:** the empty-store divisor is guarded at `PatternClipView.cpp:155`; the count-gated height calculations and zero-iteration track loop remain safe. The existing non-empty preview rendering test still passes.
- **No collateral damage:** this changes only the paint calculation when the pattern store is empty. It does not alter project serialization, saved data, undo, commands, or audio-thread behavior.
- **Portability review:** the product change uses ordinary C++ arithmetic and no platform-specific API. The regression uses standard `<cfenv>` exception flags and existing Qt test/render APIs.

Files committed: `src/gui/clips/PatternClipView.cpp`, `tests/src/gui/ArrangerTimelineTest.cpp`, `tests/upstream-modifications.txt`, and this report.
