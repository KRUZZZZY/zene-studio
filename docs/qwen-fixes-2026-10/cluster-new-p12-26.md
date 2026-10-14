# Cluster new-p12-26 — FIXED

## Finding and evidence

`meter.measure_file` rejected every path that did not start with `/`, so native
Windows drive paths and UNC paths were rejected before QFileInfo could inspect
them. The validator now uses `QFileInfo::isAbsolute()` for the running platform,
also recognizes Windows drive and UNC syntax for cross-platform validation, and
explicitly rejects `:/` and `qrc:/` resource paths.

- [ControlCommandsMeterFile.cpp:143](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-26/src/core/ControlCommandsMeterFile.cpp:143) rejects Qt resource paths. The drive and UNC helpers are at lines 148 and 154; `isAbsoluteMeterFilePath` combines them with Qt's platform-aware check at line 167, and `meterMeasureFile` calls it at line 182.
- [control-meter-commands.py:529](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-26/tests/control-meter-commands.py:529) checks drive and UNC paths reach the filesystem check. On Windows the UNC probe is skipped to avoid contacting a remote host; the drive probe runs on all platforms. The resource-path refusals are checked at line 544.

## Verification commands and results

Logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-26.r1/logs`.
The final reverse-patch proof used the same `ControlMeterCommands` ctest before
and after applying the production patch. CTest executed **1/1 test**.

| Purpose | Exact command | Exit |
|---|---|---:|
| Initial binary rebuild | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-26.r1/logs/build-1.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| First integration attempt (prerequisite missing) | `cd build/tests && ctest -R ControlMeterCommands --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-26.r1/logs/test-1.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 8 |
| Build the integration prerequisite | `cmake --build build --target audiofileprocessor > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-26.r1/logs/build-3.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| Save the product-only patch | `git diff -- src/core/ControlCommandsMeterFile.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-26.r1/logs/fix.patch; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| Remove fix for RED | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-26.r1/logs/fix.patch; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| Rebuild original behavior | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-26.r1/logs/build-7.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| RED regression run | `cd build/tests && ctest -R ControlMeterCommands --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-26.r1/logs/test-6.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 8 |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-26.r1/logs/fix.patch; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| Rebuild fixed behavior | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-26.r1/logs/build-8.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| GREEN regression run | `cd build/tests && ctest -R ControlMeterCommands --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-26.r1/logs/test-7.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| Complexity gate (final) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-26.r1/logs/gate-3.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| File-length gate (final) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-26.r1/logs/gate-4.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |

The RED log records both the Windows drive and UNC assertions as FAILED; the
GREEN log records `1/1 Test #377: ControlMeterCommands ... Passed`. An earlier
complexity-gate run exited 1 because the first validator version scored CCN 14;
its checks were split into small helpers, and the final gate above passes. That
initial gate command was `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-26.r1/logs/gate-1.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` (EXIT=1). The first ctest attempt exited 8 before reaching the checks because the build lacked `audiofileprocessor`; the recorded module build then exited 0 and the regression runs completed normally.

The final committed files are:

- `src/core/ControlCommandsMeterFile.cpp`
- `tests/control-meter-commands.py`
- `docs/qwen-fixes-2026-10/cluster-new-p12-26.md`
