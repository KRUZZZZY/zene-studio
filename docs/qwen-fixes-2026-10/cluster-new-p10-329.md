# Cluster new-p10-329

**Verdict: FIXED**

`RenameDialog::textChanged()` writes edits into the referenced caller string. A window close rejects
the dialog, so `RenameDialog::reject()` now restores `m_originalString` before calling
`QDialog::reject()`. Escape keeps its existing behavior.

## File and line evidence

- [`include/RenameDialog.h`](../../include/RenameDialog.h:42) declares the `reject()` override.
- [`src/gui/modals/RenameDialog.cpp`](../../src/gui/modals/RenameDialog.cpp:75) restores the
  original string and delegates rejection to `QDialog`.
- [`tests/src/gui/CommandFormTest.cpp`](../../tests/src/gui/CommandFormTest.cpp:163) edits the
  visible dialog field, closes the window, and verifies that the caller's string is unchanged.
- [`tests/upstream-modifications.txt`](../../tests/upstream-modifications.txt:603) records
  both inherited production files with reasons.

## Verification evidence

Build commands ran from the worktree root. CTest commands ran from
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-329/build/tests`.
Each command redirected output to the named new log in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-329.r1/logs` and printed its exit code.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Build for initial RED run | `cmake --build build --target zene CommandFormTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-329.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 | Both targets built before the original-code regression run. |
| RED, before the fix | `ctest -R '^CommandFormTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-329.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | 1/1 CTest target failed; QtTest reported 16 passed and the close-window regression failed. |
| Build with fix | `cmake --build build --target zene CommandFormTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-329.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 | Fixed targets built. |
| GREEN, after the fix | `ctest -R '^CommandFormTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-329.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 | 1/1 CTest target passed. |
| Build with product patch reversed | `cmake --build build --target zene CommandFormTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-329.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 | Rebuilt both targets without the production fix. |
| Negative control, product patch reversed | `ctest -R '^CommandFormTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-329.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 | After rebuilding without the fix, the same close-window assertion failed; QtTest reported 16 passed and 1 failed. |
| Build after restoring product patch | `cmake --build build --target zene CommandFormTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-329.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 | Rebuilt both restored targets. |
| GREEN, product patch restored | `ctest -R '^CommandFormTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-329.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 | 1/1 CTest target passed. |
| Affected-family count | `ctest -R '^CommandFormTest$' --output-on-failure` | — | One registered CTest test target, `CommandFormTest`, executed on each run. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-329.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-329.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 | Passed. |

The intermediate `build-2.log` invocation also reached both targets, but its exit echo was not
captured by the tool; it is not used as exit-code evidence.

The negative control used
`git diff -- include/RenameDialog.h src/gui/modals/RenameDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-329.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-329.r1/logs/fix.patch; echo EXIT=$?` (exit 0), then restored it with
`git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-329.r1/logs/fix.patch; echo EXIT=$?` (exit 0).

The `fff grep` tool was not available in this session. Build, test, and gate logs were searched with
`rg`, one term at a time (`error`, `FAILED`, and `Assertion`). The expected failed assertions appear
in `test-1.log` and `test-3.log`.

## Files in this commit

- `include/RenameDialog.h`
- `src/gui/modals/RenameDialog.cpp`
- `tests/src/gui/CommandFormTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-329.md`
