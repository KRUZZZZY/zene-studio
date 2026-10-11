# Cluster new-p10-15

**Verdict: FIXED**

`gridLineOf()` in `src/gui/clips/ClipView.cpp:87-97` now forms the snap point, start, and end candidates in `std::int64_t`. It skips candidates outside `tick_t` before testing grid alignment or converting back. The arranger regression at `tests/src/gui/ArrangerTimelineTest.cpp:236-280` drags a clip to `max(tick_t) - 126` and checks that the preview line stays at the clip start when its end candidate overflows. The inherited source modification is recorded at `tests/upstream-modifications.txt:1006`.

## Regression evidence

The first harness run used a zoom below the arranger's supported minimum. It failed at the setup assertion (`movedStart`: 536870881 vs. 2147483521) and is not counted as bug evidence. After changing the setup to 4 px/bar, the original code failed the snap-line assertion: 44739587 vs. 44739578. Reversing the production patch reproduced that same assertion; restoring it passed.

| Stage | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Initial harness build | `cmake --build build --target zene ArrangerTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-15.r1/logs/build-1.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | Built zene and the test target. |
| Initial harness run | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-15.r1/logs/test-1.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 8 | Setup mismatch described above; not regression proof. |
| Corrected original-code build | `cmake --build build --target zene ArrangerTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-15.r1/logs/build-2.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | Rebuilt zene and the test target before the original-code run. |
| Original-code RED | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-15.r1/logs/test-2.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 8 | Snap-line assertion failed by 9 px; QtTest totals: 12 passed, 1 failed. |
| Fixed-code GREEN | `cmake --build build --target zene ArrangerTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-15.r1/logs/build-3.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | Rebuilt zene and the test target. |
| Fixed-code GREEN | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-15.r1/logs/test-3.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | CTest passed 1/1. |
| Create and reverse production patch | `git diff -- src/gui/clips/ClipView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-15.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-15.r1/logs/fix.patch; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | Removed the production fix for the proof run. |
| Reversed-patch build | `cmake --build build --target zene ArrangerTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-15.r1/logs/build-4.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | Rebuilt zene and the test target without the fix. |
| Reversed-patch RED | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-15.r1/logs/test-4.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 8 | Reproduced the same 9 px snap-line assertion failure; 12 passed, 1 failed. |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-15.r1/logs/fix.patch; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | Restored the production fix. |
| Restored-patch build | `cmake --build build --target zene ArrangerTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-15.r1/logs/build-5.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | Rebuilt zene and the test target. |
| Restored-patch GREEN | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-15.r1/logs/test-5.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | CTest passed 1/1. |

The affected CTest family ran **1 test executable** (`ArrangerTimelineTest`, CTest #247) per invocation. The red QtTest runs report 12 passing slots and the new regression failing.

## Gates and commit

| Check | Exact command | Exit |
| --- | --- | ---: |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-15.r1/logs/complexity-gate-2.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-15.r1/logs/file-length-gate-2.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| Whitespace check | `git diff --check` | 0 |

Files to commit: `src/gui/clips/ClipView.cpp`, `tests/src/gui/ArrangerTimelineTest.cpp`, `tests/upstream-modifications.txt`, and this report.
