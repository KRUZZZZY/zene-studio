FIXED

# Cluster new-p10-477-port-port

## Decision and code evidence

The finding is real. Before the fix, `XyPadArea::moveTo()` reported only the new point and `XyPadDialog::send()` passed that point to `rack.macro_set_pair`, so an X-only pointer move resent the stale Y coordinate. The regression failed on that exact Y assertion before any product edit (`test-1-red.log`) and failed again when the live-axis override was reversed (`test-3-reversed.log`).

The fix carries changed axes from input to `send()`. Pointer axes compare at pixel resolution in `src/gui/XyPad.cpp:180-191`; the drag tracks the previous pointer point separately from the displayed dot in `src/gui/XyPad.cpp:252-262`. Key events identify their own axis in `src/gui/XyPad.cpp:159-177`. `XyPadDialog::send()` reads the rack's current value for each untouched axis before making the same single pair command in `src/gui/XyPad.cpp:390-410`. The moved callback declaration and matching send declaration are in `include/XyPad.h:52-60,68-84,118`.

## Regression and verification evidence

The primary regression at `tests/src/gui/XyPadPairTest.cpp:314-344` opens a real channel pad, changes Y through `rack.macro_set`, then sends a horizontal mouse press at the stale displayed Y. Its Y-value assertion failed before the fix and passed afterward. Four sibling regressions cover multi-event horizontal drags, repeated keyboard moves with external Y changes, a key after a pointer move, and pixel-resolution rounding (`tests/src/gui/XyPadPairTest.cpp:346-460`). The event tests use a 201x201 pad and explicit Qt mouse/key events; they use no sleeps, timers, wall-clock reads, or races.

| Run | Command | Exit | Result |
| --- | --- | ---: | --- |
| Initial RED build (worktree root) | `cmake --build build --target XyPadPairTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-477-port-port.r1/logs/build-1-red.log 2>&1; echo EXIT=$?` | 0 | Test target rebuilt. |
| Initial RED test (`build/tests`) | `ctest -R '^XyPadPairTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-477-port-port.r1/logs/test-1-red.log 2>&1; echo EXIT=$?` | 8 | The Y-preservation assertion failed in `horizontalPointerMovePreservesAnExternalChangeToY`. |
| GREEN build (worktree root) | `cmake --build build --target zene XyPadPairTest CanvasKeyboardTest ItemListButtonsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-477-port-port.r1/logs/build-2-green.log 2>&1; echo EXIT=$?` | 0 | Product and all targets including the changed header rebuilt. |
| GREEN family (`build/tests`) | `ctest -R '^(XyPadPairTest|CanvasKeyboardTest|ItemListButtonsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-477-port-port.r1/logs/test-2-green.log 2>&1; echo EXIT=$?` | 0 | 3/3 CTest targets passed. |
| Reverse patch | `git diff -- include/XyPad.h src/gui/XyPad.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-477-port-port.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-477-port-port.r1/logs/fix.patch` | 0 | Reversed the send-time live-axis behavior while keeping the changed-axis input API so the proof build remains compatible. |
| Reversed build (worktree root) | `cmake --build build --target zene XyPadPairTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-477-port-port.r1/logs/build-4-reversed.log 2>&1; echo EXIT=$?` | 0 | Rebuilt the product and regression target with the behavior reverted. |
| Reversed test (`build/tests`) | `ctest -R '^XyPadPairTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-477-port-port.r1/logs/test-3-reversed.log 2>&1; echo EXIT=$?` | 8 | The same Y-preservation assertion failed. |
| Restore patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-477-port-port.r1/logs/fix.patch` | 0 | Restored the fix. |
| Restored build (worktree root) | `cmake --build build --target zene XyPadPairTest CanvasKeyboardTest ItemListButtonsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-477-port-port.r1/logs/build-5-restored.log 2>&1; echo EXIT=$?` | 0 | Rebuilt all affected targets. |
| Restored family (`build/tests`) | `ctest -R '^(XyPadPairTest|CanvasKeyboardTest|ItemListButtonsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-477-port-port.r1/logs/test-4-restored.log 2>&1; echo EXIT=$?` | 0 | 3/3 CTest targets passed. |
| Feature gates (worktree root) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-477-port-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-477-port-port.r1/logs/gate-1-feature.log 2>&1; echo EXIT=$?` | 0 | All gates passed. |

An earlier reverse-build attempt stopped with EXIT 2 because the reversed helper was unused under `-Werror`; it is recorded in `build-3-reversed.log`. The compatible reverse proof above retained the helper call and discarded its value, then demonstrated the regression failure.

## Gate table

| Gate | Result |
| --- | --- |
| fork-sources | PASS |
| all-sources-reproduce | PASS |
| no-upstream-regression | PASS |
| check-namespace | PASS |
| no-tautology | PASS |
| doc-figures | PASS |
| complexity | PASS — no new item in changed files |
| file-length | PASS — no new item in changed files |
| Overall | PASS, feature_check EXIT 0 |

## Self-check

- The test fails with the changed behavior reversed and passes with it restored: reversed build EXIT 0 / ctest EXIT 8; restored build EXIT 0 / related-family ctest EXIT 0. The first red run before the product change was build EXIT 0 / ctest EXIT 8.
- The test is deterministic: it uses injected rack macro values, a fixed 201x201 widget, and explicit Qt input events. No sleeps, timers, wall-clock timing, or races are used.
- The fix is at the cause and complete: pointer movement reports changed axes at the pixel resolution of the widget; keyboard movement reports its named axis; `send()` reads current rack values for untouched axes. All `XyPadArea::moved` callback callers were searched and updated in `src/gui/XyPad.cpp`, `tests/src/gui/XyPadPairTest.cpp`, and `tests/src/gui/CanvasKeyboardTest.cpp`. Header consumers were rebuilt with `zene`, `XyPadPairTest`, `CanvasKeyboardTest`, and `ItemListButtonsTest`.
- No project, serialization, or saved-data path changed. The pad still invokes one `rack.macro_set_pair` per two-macro move (or `rack.macro_set` when both axes select one macro), preserving existing undo coalescing. No audio-thread path or control command changed.
- The verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE: the original code's Y overwrite is shown by the RED assertion in `tests/src/gui/XyPadPairTest.cpp:342` and by its repeated failure under the reverse proof.

## Files committed

- `include/XyPad.h`
- `src/gui/XyPad.cpp`
- `tests/src/gui/XyPadPairTest.cpp`
- `tests/src/gui/CanvasKeyboardTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-477-port-port.md`
