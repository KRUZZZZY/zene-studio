# Cluster new-p12-166: middle-drag key anchoring

Verdict: **FIXED**

## Evidence

`NoteKeyboardView::mouseMoveEvent` in `src/gui/noteeditor/NoteEditorParts.cpp:127` now computes the scroll offset from the current pointer y. The regression in `tests/src/gui/CanvasKeyboardTest.cpp:147` middle-drags upward by eight pixels and checks that the key captured at the starting point remains under the moved pointer.

| Evidence | Result |
| --- | --- |
| RED command | `ctest -R '^CanvasKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/test-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` — EXIT=8. The moved pointer reported key 114; expected key 113. |
| GREEN command | `ctest -R '^CanvasKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/test-5.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` — EXIT=0. |
| Affected-family count | `CanvasKeyboardTest`: 1 CTest target. Its RED QtTest run reported 11 passed and 1 failed; the new regression was the failure. The GREEN CTest run passed the target. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/gate-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` — EXIT=0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/gate-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` — EXIT=0. |
| Files in commit | `src/gui/noteeditor/NoteEditorParts.cpp`, `tests/src/gui/CanvasKeyboardTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p12-166.md`. |

## Command record

Build commands ran from the worktree root. CTest commands ran from `build/tests`. All build commands exited 0; RED and GREEN results are recorded above and below.

| Step | Exact command | EXIT |
| --- | --- | ---: |
| Initial test build | `cmake --build build --target CanvasKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/build-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Initial RED | `ctest -R '^CanvasKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/test-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 8 |
| Refresh original app | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/build-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Rebuild original test | `cmake --build build --target CanvasKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/build-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Refreshed original RED | `ctest -R '^CanvasKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/test-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 8 |
| Build fixed app | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/build-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Build fixed test | `cmake --build build --target CanvasKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/build-5.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Initial GREEN | `ctest -R '^CanvasKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/test-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Remove product patch | `git diff -- src/gui/noteeditor/NoteEditorParts.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Build app without patch | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/build-6.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Build test without patch | `cmake --build build --target CanvasKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/build-7.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Patch-removed RED | `ctest -R '^CanvasKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/test-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 8 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Rebuild app with patch | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/build-8.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Rebuild test with patch | `cmake --build build --target CanvasKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/build-9.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Patch-restored GREEN | `ctest -R '^CanvasKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/test-5.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/gate-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-166.r1/logs/gate-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |

The `fff grep` tool was not available in this session. I searched the log folder with `rg -n` for `error`, `FAILED`, and `Assertion`; the failure matches are the expected RED runs.
