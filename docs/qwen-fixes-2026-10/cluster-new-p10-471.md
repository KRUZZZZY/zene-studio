# Cluster new-p10-471

Verdict: **FIXED**

The `SA_DEBUG` analyzer timer compared a truncated 32-bit counter against a five-billion threshold. The timestamp is now a `std::chrono::steady_clock::time_point`; the report predicate compares elapsed time with `std::chrono::seconds{5}`. Execution time is measured as a chrono duration in milliseconds. Debug counters are initialized, and an empty interval does not divide by zero.

## File evidence

- `plugins/SpectrumAnalyzer/Analyzer.h:67-71` defines the tested five-second steady-clock predicate.
- `plugins/SpectrumAnalyzer/Analyzer.h:88-93` stores the previous report time as a steady-clock time point.
- `plugins/SpectrumAnalyzer/Analyzer.cpp:67-72` initializes the debug timestamp and counters.
- `plugins/SpectrumAnalyzer/Analyzer.cpp:90-115` calls the predicate and measures execution in milliseconds.
- `tests/src/plugins/AudioPluginTest.cpp:92-102,465-470` verifies that four seconds is below the threshold and six seconds reaches it.

The configured CTest tree has no `Analyzer` entry (402 tests total), despite the supplied test pointer. The regression is in the existing `AudioPluginTest` executable; the selected family contains one registered CTest test, which passed after the fix.

## Regression evidence

The first timestamp-type regression failed on the original code with exit 8: the field was `int`. The final predicate regression was then run with the production patch reversed; it failed at six seconds, and passed with the patch restored.

| Result | Exact command | Exit |
| --- | --- | ---: |
| RED, original code, timestamp assertion | `ctest -R '^AudioPluginTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-471.r1/logs/test-red-1.log 2>&1; echo EXIT=$?` | 8 |
| RED, production patch reversed, final predicate test | `ctest -R '^AudioPluginTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-471.r1/logs/test-red-2.log 2>&1; echo EXIT=$?` | 8 |
| GREEN, production patch restored | `ctest -R '^AudioPluginTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-471.r1/logs/test-green-2.log 2>&1; echo EXIT=$?` | 0 |

The final RED log records `analyzerDebugTimerReportsAfter<Analyzer>(std::chrono::seconds{6})` returning false. The final GREEN log records `1/1 Test #323: AudioPluginTest Passed` and `100% tests passed`.

## Build and patch-reversal evidence

Commands below were run from the worktree root unless stated otherwise.

| Check | Exact command | Exit |
| --- | --- | ---: |
| Initial RED test build | `cmake --build build --target AudioPluginTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-471.r1/logs/build-red-1.log 2>&1; echo EXIT=$?` | 0 |
| Initial RED CTest | `ctest -R '^AudioPluginTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-471.r1/logs/test-red-1.log 2>&1; echo EXIT=$?` | 8 |
| Initial GREEN build (timestamp regression iteration) | `cmake --build build --target zene analyzer AudioPluginTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-471.r1/logs/build-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Initial GREEN CTest (timestamp regression iteration) | `ctest -R '^AudioPluginTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-471.r1/logs/test-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Reverse production patch | `git diff -- plugins/SpectrumAnalyzer/Analyzer.cpp plugins/SpectrumAnalyzer/Analyzer.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-471.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-471.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild reversed code | `cmake --build build --target zene analyzer AudioPluginTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-471.r1/logs/build-red-2.log 2>&1; echo EXIT=$?` | 0 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-471.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Final rebuild | `cmake --build build --target zene analyzer AudioPluginTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-471.r1/logs/build-green-2.log 2>&1; echo EXIT=$?` | 0 |
| Compile the `SA_DEBUG` branch with the configured analyzer flags | `make -C build -f sa-debug-check.mk debug-check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-471.r1/logs/build-debug-1.log 2>&1; echo EXIT=$?` | 0 |

`ctest -R '^AudioPluginTest$' --output-on-failure` was run from `build/tests` for each CTest row above. The test discovery command `ctest -N > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-471.r1/logs/discovery-1.log 2>&1; echo EXIT=$?` exited 0 and reported 402 tests with no `Analyzer` entry.

No `fff` grep tool or command was available in this session, so the log folder was searched with `rg` for `error`, `FAILED`, and `Assertion`. The `error` search only matched the `-Werror` compiler flag in the debug compile command; `FAILED` appears only in the two intentional RED CTest logs, and `Assertion` had no matches.

## Required gates

| Gate command | Exit |
| --- | ---: |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-471.r1/logs/gate-complexity-2.log 2>&1; echo EXIT=$?` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-471.r1/logs/gate-file-length-1.log 2>&1; echo EXIT=$?` | 0 |

## Files committed

- `plugins/SpectrumAnalyzer/Analyzer.cpp`
- `plugins/SpectrumAnalyzer/Analyzer.h`
- `tests/src/plugins/AudioPluginTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-471.md`
