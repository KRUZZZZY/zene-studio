# Cluster new-p9-85

**Verdict: FIXED**

`ControlLayout::addItem()` accepts items whose `widget()` is null and stores them as `unnamed` (`src/gui/ControlLayout.cpp:113-117`). `pairAt()` then dereferenced `widget()` while checking whether each item was the search bar (`src/gui/ControlLayout.cpp:149-152`). It now checks the pointer before reading `objectName()`.

The regression in `tests/src/gui/TaskIndicatorTest.cpp:151-173` adds a spacer, checks `itemAt()` and `takeAt()`, and separately exercises layout destruction with a spacer still present.

## Evidence

All test commands below ran from `build/tests`. The ctest expression matched one registered test (`TaskIndicatorTest`) in each run.

| Stage | Command | Exit | Result |
|---|---|---:|---|
| Original code, RED | `ctest -R '^TaskIndicatorTest$' --output-on-failure` | 8 | 1/1 failed with SIGSEGV (signal 11) on a spacer item. Log: `test-1.log`. |
| Fix applied, GREEN | `ctest -R '^TaskIndicatorTest$' --output-on-failure` | 0 | 1/1 passed. Log: `test-2.log`. |
| Reverse production patch | `git diff -- src/gui/ControlLayout.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-85.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-85.r1/logs/fix.patch` | 0 | Fix removed for the proof. Log: `revert-1.log`. |
| Reversed patch, RED | `ctest -R '^TaskIndicatorTest$' --output-on-failure` | 8 | 1/1 failed again with SIGSEGV (signal 11). Log: `test-3.log`. |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-85.r1/logs/fix.patch` | 0 | Fix restored. Log: `reapply-1.log`. |
| Reapplied patch, GREEN | `ctest -R '^TaskIndicatorTest$' --output-on-failure` | 0 | 1/1 passed. Log: `test-4.log`. |

The affected build command was `cmake --build build --target zene TaskIndicatorTest`; it exited 0 before the initial GREEN run, the reversal RED run, and the reapply GREEN run (logs `build-4.log`, `build-5.log`, and `build-6.log`). The pre-fix test build, `cmake --build build --target TaskIndicatorTest`, also exited 0 (`build-3.log`).

Two target setup attempts were recorded before using the existing GUI test target: `cmake --build build --target ControlLayoutTest` exited 2 because that new target was not in the configured build (`build-1.log`); `cmake --build build --target TaskIndicatorTest/fast` exited 2 because its autogen outputs did not exist yet (`build-2.log`). No CMake configure command was run, and the final change does not modify `tests/CMakeLists.txt`.

| Gate | Command | Exit | Log |
|---|---|---:|---|
| Complexity | `bash tests/complexity-gate.sh --check` | 0 | `complexity-1.log` |
| File length | `bash tests/file-length-gate.sh --check` | 0 | `file-length-1.log` |

The log folder is `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-85.r1/logs`. The requested `fff` grep tool was not available in this session, so the logs were searched with `rg` for the required failure terms and exit summaries.

## Files committed

- `src/gui/ControlLayout.cpp`
- `tests/src/gui/TaskIndicatorTest.cpp`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-85.md`
