# Cluster new-p9-561

Verdict: **FIXED**

## Evidence

`src/gui/ArrangerAids.cpp:136` now passes the gesture state and current zoom to `arrangerzoom::finishPinch`. `include/ArrangerZoom.h:80` declares that helper; `src/gui/ArrangerZoom.cpp:169` records the start zoom for a changed pinch on `GestureFinished` or `GestureCanceled`. The regression at `tests/src/gui/ArrangerTimelineTest.cpp:134` checks the canceled state through that helper, then invokes `arrangement.zoom_back`.

The Qt offscreen backend did not synthesize a `GestureCanceled` event from touch cancellation. The regression therefore passes `Qt::GestureCanceled` directly to the same helper used by the event filter, and verifies the user-visible zoom-back result.

Affected family: **1/1 CTest target** (`ArrangerTimelineTest`).

## RED and GREEN

| Phase | Exact command | Exit |
|---|---|---:|
| RED build | `cmake --build build --target zene ArrangerTimelineTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-561.r1/logs/build-14.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| RED test | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-561.r1/logs/test-14.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| GREEN build | `cmake --build build --target zene ArrangerTimelineTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-561.r1/logs/build-18.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| GREEN test | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-561.r1/logs/test-18.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

RED failed at the expected assertion: zoom-back returned 101 instead of the pinch start value 100. GREEN passed with 1/1 CTest target.

## Reverse-patch proof

| Step | Exact command | Exit |
|---|---|---:|
| Save and reverse the product fix | `git diff -- include/ArrangerZoom.h src/gui/ArrangerAids.cpp src/gui/ArrangerZoom.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-561.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-561.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild without the fix | `cmake --build build --target zene ArrangerTimelineTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-561.r1/logs/build-16.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Regression fails without the fix | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-561.r1/logs/test-16.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| Restore the fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-561.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild with the fix | `cmake --build build --target zene ArrangerTimelineTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-561.r1/logs/build-17.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Regression passes with the fix | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-561.r1/logs/test-17.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

## Ratchet gates

| Gate | Exact command | Exit |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-561.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-561.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

## Files committed

- `include/ArrangerZoom.h`
- `src/gui/ArrangerAids.cpp`
- `src/gui/ArrangerZoom.cpp`
- `tests/src/gui/ArrangerTimelineTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-561.md`
