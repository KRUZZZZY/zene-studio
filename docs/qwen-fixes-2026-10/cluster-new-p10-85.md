# Cluster new-p10-85

**Verdict: BLOCKED**

## Finding and implementation evidence

The finding is real. `include/ControlServer.h:92` sets `MaxClients` to 64. In `src/core/ControlServerWin32.cpp:275-287`, the Windows acceptor joins and removes completed client workers. At `src/core/ControlServerWin32.cpp:366-382`, it reaps those workers, closes an accepted pipe when 64 workers are still active, and starts a worker only if a slot is available. The worker list uses `std::unique_ptr<Win32ClientWorker>` and is bounded by the live-client cap (`include/ControlServer.h:242-250`).

`tests/src/core/ControlShutdownHookTest.cpp:230-279` adds a Windows-only regression. It connects 64 clients, checks that an excess client is disconnected, then closes an allowed client and checks that the freed slot can serve a replacement.

## Verification and platform block

This shell is Linux (`uname -a` reports `Linux kruzzzputer ... x86_64 GNU/Linux`). The configured build uses `/usr/bin/c++`, `/usr/lib/cmake/Qt6`, and Unix Makefiles. No Windows runner, Windows SDK, or MinGW compiler is available here. The Windows regression therefore skips in every local run. I cannot verify Windows compilation or produce behavioral RED/GREEN evidence in this environment.

| Evidence | Exact command and working directory | Exit | Result |
|---|---|---:|---|
| Fixed build before reversal (worktree root) | `cmake --build build --target zene ControlShutdownHookTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/build-1.log; exit "$rc"` | 0 | Linux `zene` and `ControlShutdownHookTest` targets built. |
| Fixed test before reversal (`build/tests`) | `ctest -R '^ControlShutdownHookTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/test-1.log; exit "$rc"` | 0 | 1/1 CTest tests passed; the Windows regression did not run. |
| QtTest details before reversal (`build/tests`) | `./ControlShutdownHookTest -v1 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/qt-test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/qt-test-1.log; exit "$rc"` | 0 | 12 passed, 0 failed, 1 skipped. The skipped case is `windowsCapsActivePipeClientsAndReusesFreedSlot`. |
| Create product-only reversal patch (worktree root) | `git diff HEAD^ HEAD -- include/ControlServer.h src/core/ControlServerWin32.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/fix.patch; rc=$?; printf 'DIFF_EXIT=%s\n' "$rc"; exit "$rc"` | 0 | Patch contains only the two product files. |
| Reverse product hunks (worktree root) | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/fix.patch; rc=$?; printf 'REVERSE_APPLY_EXIT=%s\n' "$rc"; exit "$rc"` | 0 | Product changes were temporarily reversed; the regression test source remained present. |
| Reversed-product build (worktree root) | `cmake --build build --target zene ControlShutdownHookTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/build-2.log; exit "$rc"` | 0 | Linux targets built with product hunks reversed. |
| RED attempt with product reversed (`build/tests`) | `ctest -R '^ControlShutdownHookTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/test-2.log; exit "$rc"` | 0 | Not RED: 1/1 CTest tests passed because the Windows regression skipped. |
| QtTest details with product reversed (`build/tests`) | `./ControlShutdownHookTest -v1 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/qt-test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/qt-test-2.log; exit "$rc"` | 0 | 12 passed, 0 failed, 1 skipped; this does not prove the bug is caught. |
| Restore product hunks (worktree root) | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/fix.patch; rc=$?; printf 'RESTORE_APPLY_EXIT=%s\n' "$rc"; exit "$rc"` | 0 | Restored the committed product changes. |
| Restored build (worktree root) | `cmake --build build --target zene ControlShutdownHookTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/build-3.log; exit "$rc"` | 0 | Linux targets rebuilt after restoration. |
| GREEN attempt after restoration (`build/tests`) | `ctest -R '^ControlShutdownHookTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/test-3.log; exit "$rc"` | 0 | 1/1 CTest tests passed, but the Windows regression skipped. This is not behavioral GREEN evidence. |
| QtTest details after restoration (`build/tests`) | `./ControlShutdownHookTest -v1 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/qt-test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/qt-test-3.log; exit "$rc"` | 0 | 12 passed, 0 failed, 1 skipped. The Windows regression executed zero times. |
| Complexity gate (worktree root) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/gate-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/gate-5.log; exit "$rc"` | 1 | Six over-limit functions are reported outside this cluster's changed files; no baseline was changed. |
| File-length gate (worktree root) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/gate-6.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r3/logs/gate-6.log; exit "$rc"` | 1 | The gate reports over-limit files outside this cluster's changed files; no baseline was changed. |

The affected CTest family contains 1 CTest test. Its QtTest executable reports 12 passed and 1 skipped on Linux, and the Windows-specific case ran zero times in both the reversed and restored states. The required Windows RED/GREEN and Windows build remain outstanding. Both gate outputs are saved in the run log folder. The `fff grep` integration was not exposed in this session; I searched the saved logs with `rg -n` for `error`, `FAILED`, and `Assertion`, and for the test and gate summaries.

## Files committed

The existing implementation commit `3ef17ccc3` includes:

- `include/ControlServer.h`
- `src/core/ControlServerWin32.cpp`
- `tests/src/core/ControlShutdownHookTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-85.md`

This blocked evidence refresh changes only `docs/qwen-fixes-2026-10/cluster-new-p10-85.md`; the implementation and test are unchanged.
