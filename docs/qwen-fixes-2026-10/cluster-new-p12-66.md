# Cluster new-p12-66

**Verdict: FIXED**

`install()` ignored a failed `sigaction()` call and marked a partial installation as successful. It now records the prior disposition for each signal, restores already changed dispositions when a later installation fails, closes the report-directory fd, and returns `false`. `s_installed` is set only after every requested handler is installed.

## Evidence

- [src/core/CrashReporter.cpp:99](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-66/src/core/CrashReporter.cpp:99) defines the fixed capacity for the signal list; [src/core/CrashReporter.cpp:264](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-66/src/core/CrashReporter.cpp:264) stores prior dispositions and checks each installation result. Lines 271–276 restore earlier dispositions and fail the install; line 280 sets `s_installed` after the loop.
- [tests/src/core/CrashReporterArmTest.cpp:183](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-66/tests/src/core/CrashReporterArmTest.cpp:183) injects a `SIGBUS` installation failure after `SIGSEGV` has been changed. It checks the failure result, the installed state, and restoration of the prior `SIGSEGV` and `SIGBUS` dispositions.

## Commands and results

All logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-66.r1/logs`.

| Purpose | Exact command | Working directory | Exit | Log / result |
|---|---|---|---:|---|
| Build regression target before production fix | `cmake --build build --target CrashReporterArmTest` | worktree root | 0 | `build-1.log` |
| RED on original implementation | `./CrashReporterArmTest install_failure_rolls_back_partial_handlers` | `build/tests` | 1 | `test-1.log`; `!installed` failed because `install()` returned true |
| GREEN with fix | `./CrashReporterArmTest install_failure_rolls_back_partial_handlers` | `build/tests` | 0 | `test-2.log`; 3 Qt test points passed (init, slot, cleanup) |
| Reverse production patch | `git diff -- src/core/CrashReporter.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-66.r1/logs/fix.patch` then `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-66.r1/logs/fix.patch` | worktree root | 0 | patch created and reversed |
| Rebuild without fix | `cmake --build build --target zene CrashReporterArmTest` | worktree root | 0 | `build-4.log` |
| RED with fix reversed | `./CrashReporterArmTest install_failure_rolls_back_partial_handlers` | `build/tests` | 1 | `test-5.log`; same `!installed` assertion failed |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-66.r1/logs/fix.patch` | worktree root | 0 | patch reapplied |
| Rebuild with fix restored | `cmake --build build --target zene CrashReporterArmTest` | worktree root | 0 | `build-6.log` |
| GREEN after patch restoration | `./CrashReporterArmTest install_failure_rolls_back_partial_handlers` | `build/tests` | 0 | `test-6.log`; 3 Qt test points passed |
| Final build | `cmake --build build --target zene CrashReporterArmTest CrashReporterTest` | worktree root | 0 | `build-7.log` |
| Final affected-family run | `ctest -R '^CrashReporter(Arm)?Test$' --output-on-failure` | `build/tests` | 0 | `test-7.log`; 2/2 ctest tests passed |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | worktree root | 0 | `gate-5.log` |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | worktree root | 0 | `gate-6.log` |

The affected family contains two ctest tests: `CrashReporterArmTest` and `CrashReporterTest`. Both passed in the final run. The changed production source is already registered in `tests/fork-sources.txt`; no source-registration file needed a change.

## Files committed

- `src/core/CrashReporter.cpp`
- `tests/src/core/CrashReporterArmTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-66.md`
