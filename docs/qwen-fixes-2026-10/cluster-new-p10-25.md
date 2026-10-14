# Cluster new-p10-25: config save write failure

**Verdict: FIXED**

## Evidence

`src/core/ConfigManager.cpp:682-688` now writes through `QSaveFile`, disables direct-write fallback, opens unbuffered so the write result reflects I/O failure, checks the complete byte count and `commit()`, and cancels the temporary write on failure. The existing UI/headless error path now reports a save failure.

`tests/src/core/ConfigMigrationTest.cpp:209-225` seeds an existing config, limits file writes to zero bytes, calls `saveConfigFile()`, and checks that the original bytes remain. With the production patch reversed, it fails at line 225: actual contents are empty. With the patch reapplied, it passes. The injected failure uses POSIX `RLIMIT_FSIZE`; the case skips on non-Unix platforms.

`tests/upstream-modifications.txt:677` records the change to inherited `ConfigManager.cpp`.

The first QSaveFile attempt used buffered output. On this Qt 6.4.2 build, the syscall failed during close/commit and the trace showed the empty temporary file being renamed. Opening with `QIODevice::Unbuffered` makes the checked `write()` report that failure before `commit()`.

## Required regression proof

| State | Exact command | Exit and evidence |
|---|---|---|
| RED, production patch reversed | `cmake --build build --target zene ConfigMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-25.r1/logs/build-6.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| RED, production patch reversed | `cd build/tests && ctest -R '^ConfigMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-25.r1/logs/test-8.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 8; 1/1 CTest test failed at `failedConfigWritePreservesExistingFile`, actual file empty |
| GREEN, production patch restored | `cmake --build build --target zene ConfigMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-25.r1/logs/build-7.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| GREEN, production patch restored | `cd build/tests && ctest -R '^ConfigMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-25.r1/logs/test-9.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0; 1/1 CTest test passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-25.r1/logs/gate-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-25.r1/logs/gate-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | 0 |

Affected family: `ConfigMigrationTest`, 1/1 registered CTest test passed. Its seven test slots ran; the RED replay reported 9 QtTest outcomes including init and cleanup.

The reverse proof used `git diff -- src/core/ConfigManager.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-25.r1/logs/fix.patch` (exit 0), followed by `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-25.r1/logs/fix.patch` (exit 0). The patch was restored with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-25.r1/logs/fix.patch` (exit 0).

## Command record

All builds returned 0:

- `cmake --build build --target ConfigMigrationTest` — `build-1.log`, `build-2.log`, `build-3.log`.
- `cmake --build build --target zene` — `build-4.log`.
- `cmake --build build --target zene ConfigMigrationTest` — `build-5.log`, `build-6.log`, `build-7.log`.

`cd build/tests && ctest -R '^ConfigMigrationTest$' --output-on-failure` returned 8 for `test-1.log` through `test-4.log` and `test-8.log`; it returned 0 for `test-7.log` and `test-9.log`. The strace-wrapped ctest commands for `test-5.log` and `test-6.log` each returned 8. `test-1.log` records an earlier fixture iteration; `test-2.log` is the initial RED with the final regression fixture. `test-3.log` and `test-4.log` are the buffered QSaveFile attempt. `test-5.log` and `test-6.log` are diagnostic `strace` runs (both exit 8); `trace-2.log` records the failed write and subsequent rename. `test-7.log` is the first GREEN with unbuffered output.

The two diagnostic commands were:

- `cd build/tests && strace -f -e trace=openat,rename,renameat,unlink,unlinkat,truncate,ftruncate -o /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-25.r1/logs/trace-1.log ctest -R '^ConfigMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-25.r1/logs/test-5.log 2>&1` — exit 8.
- `cd build/tests && strace -f -e trace=openat,write,writev,pwrite64,rename,unlink,unlinkat,truncate,ftruncate -o /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-25.r1/logs/trace-2.log ctest -R '^ConfigMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-25.r1/logs/test-6.log 2>&1` — exit 8.

The fff grep tool was not available in this session, so numbered logs were searched with `rg`.

## Files to commit

- `src/core/ConfigManager.cpp`
- `tests/src/core/ConfigMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-25.md`
