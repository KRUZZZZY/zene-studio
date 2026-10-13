# Cluster new-p12-167

**Verdict: FIXED**

`NoteCanvas::nudgeForKey` previously received only a Shift flag, so any arrow key that reached it could nudge the selection even when another modifier was held. The reported standalone Ctrl and Alt arrows are consumed earlier by the note editor's resize/velocity and cursor-navigation handlers (`NoteCanvasAccess.cpp:105-143`). The existing contract explicitly keeps Ctrl+Alt arrows as nudges (`NoteCanvasAccess.cpp:75`). The uncovered fall-through was Meta+Up, which changed the selected note's pitch.

The handler now receives all modifiers, ignores the keypad flag, and accepts only no modifier, Shift, Ctrl+Alt, or Ctrl+Alt+Shift. Other modifier combinations are left unhandled. The regression test checks Meta+Up is ignored and Ctrl+Alt+Up still nudges.

Evidence locations: [`NoteCanvasKeys.cpp`](../../src/gui/noteeditor/NoteCanvasKeys.cpp:101), [`NoteCanvas.h`](../../include/NoteCanvas.h:191), [`NoteCanvasAccess.cpp`](../../src/gui/noteeditor/NoteCanvasAccess.cpp:75), and [`CanvasKeyboardTest.cpp`](../../tests/src/gui/CanvasKeyboardTest.cpp:190).

## Regression and build evidence

All commands below ran in the specified working directory. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs`.

| Phase | Exact command | EXIT | Result |
|---|---|---:|---|
| Original test build (cwd: worktree) | `cmake --build build --target CanvasKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | Test target built before RED run. |
| Original code RED (cwd: `build/tests`) | `ctest -R '^CanvasKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 | `unsupportedModifiersDoNotNudgeTheSelection` failed because Meta+Up moved the note; 11 QTest cases passed and 1 failed. |
| Fixed build (cwd: worktree) | `cmake --build build --target zene NoteEditorFlTest LaneReadoutTest NoteEditorTest CanvasKeyboardTest ItemListPanelTest PianoRollBenchmarkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | Rebuilt `zene` and all six test targets including `NoteCanvas.h`. |
| Fixed code GREEN (cwd: `build/tests`) | `ctest -R '^CanvasKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | CTest reported 1/1 test passed. |
| Reverse-patch build (cwd: worktree) | `cmake --build build --target zene NoteEditorFlTest LaneReadoutTest NoteEditorTest CanvasKeyboardTest ItemListPanelTest PianoRollBenchmarkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/build-4.log; echo EXIT=$rc; exit $rc` | 0 | Rebuilt dependents with the production fix reversed. |
| Reverse-patch RED (cwd: `build/tests`) | `ctest -R '^CanvasKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/test-3.log; echo EXIT=$rc; exit $rc` | 8 | The same Meta+Up assertion failed again. |
| Restored-patch build (cwd: worktree) | `cmake --build build --target zene NoteEditorFlTest LaneReadoutTest NoteEditorTest CanvasKeyboardTest ItemListPanelTest PianoRollBenchmarkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/build-5.log; echo EXIT=$rc; exit $rc` | 0 | Rebuilt all dependents after restoring the fix. |
| Restored-patch GREEN (cwd: `build/tests`) | `ctest -R '^CanvasKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/test-4.log; echo EXIT=$rc; exit $rc` | 0 | CTest reported 1/1 test passed. |

The affected CTest family matched one test, `CanvasKeyboardTest` (CTest test #231). Its executable contains 12 QTest cases, shown by the RED run's `Totals: 11 passed, 1 failed`; the two GREEN runs each passed the one CTest test.

The inverse-patch proof used the required commands, both with EXIT=0:

```sh
git diff -- include/NoteCanvas.h src/gui/noteeditor/NoteCanvasKeys.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/fix.patch
```

## Ratchet gates

| Gate | Exact command | EXIT |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/gate-1.log; echo EXIT=$rc; exit $rc` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-167.r1/logs/gate-2.log; echo EXIT=$rc; exit $rc` | 0 |

## Files in the commit

- `include/NoteCanvas.h`
- `src/gui/noteeditor/NoteCanvasKeys.cpp`
- `tests/src/gui/CanvasKeyboardTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-167.md`
