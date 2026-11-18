# Cluster new-p12-67

Verdict: **FIXED**

## Finding and fix

The finding is real. `hasPendingReport()` treats any existing report path as pending (`src/core/CrashReporter.cpp:404-409`). If the bounded write stops early, the file could therefore be offered on the next launch.

`writeReportFile()` now unlinks the report with `unlinkat()` when fewer than the requested bytes were written (`src/core/CrashReporter.cpp:216-234`). `writeReportIfIdle()` keeps the report-directory descriptor open until that cleanup is complete (`src/core/CrashReporter.cpp:430-499`).

The regression at `tests/src/core/CrashReporterTest.cpp:158-186` forks a child, sets `RLIMIT_FSIZE` to zero, ignores `SIGXFSZ`, and verifies the failed write does not leave a report path or pending report. The pre-fix run failed at `!hasPendingReport()` with `returned FALSE`.

## Evidence

Build and test output was captured under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/`. Exit codes below are the underlying command results printed by the shell capture.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Initial RED, before production change | `ctest -R '^CrashReporterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` (from `build/tests`) | 8 | Regression failed at `!hasPendingReport()` as expected. |
| Reverse-patch RED, final production diff removed | `ctest -R '^CrashReporterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/test-6.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` (from `build/tests`) | 8 | Same assertion failed, proving the final fix is required. |
| Final GREEN, fix restored | `ctest -R '^CrashReporter(Arm)?Test$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/test-7.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` (from `build/tests`) | 0 | Both affected-family CTest tests passed: `CrashReporterArmTest` and `CrashReporterTest` (2 tests executed, both passed). |
| Final complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0 | Passed. |
| Final file-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/gate-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0 | Passed. |

Gate runs, including the initial complexity result, were:

| Log | Exact command | Exit |
|---|---|---:|
| `gate-1.log` | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 1 |
| `gate-2.log` | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0 |
| `gate-3.log` | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0 |
| `gate-4.log` | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/gate-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0 |

`gate-1.log` failed because `writeReportIfIdle()` measured CCN 12. The bounded write and cleanup were extracted into `writeReportFile()`; the final complexity gate passes without changing any gate or baseline.

Build commands and recorded exit codes:

| Log | Exact command | Exit |
|---|---|---:|
| `build-1.log` | `cmake --build build --target CrashReporterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0 |
| `build-2.log` | `cmake --build build --target zene CrashReporterTest CrashReporterArmTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0 |
| `build-3.log` | `cmake --build build --target zene CrashReporterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0 |
| `build-4.log` | `cmake --build build --target zene CrashReporterTest CrashReporterArmTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0 |
| `build-5.log` | `cmake --build build --target zene CrashReporterTest CrashReporterArmTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/build-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0 |
| `build-6.log` | `cmake --build build --target zene CrashReporterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/build-6.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0 |
| `build-7.log` | `cmake --build build --target zene CrashReporterTest CrashReporterArmTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/build-7.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0 |

CTest commands and recorded exit codes:

| Logs | Exact command (from `build/tests`) | Exit |
|---|---|---:|
| `test-1.log` | `ctest -R '^CrashReporterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 8 |
| `test-2.log` | `ctest -R '^CrashReporter(Arm)?Test$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0; 2/2 tests |
| `test-3.log` | `ctest -R '^CrashReporterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 8 |
| `test-4.log` | `ctest -R '^CrashReporter(Arm)?Test$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0; 2/2 tests |
| `test-5.log` | `ctest -R '^CrashReporter(Arm)?Test$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/test-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0; 2/2 tests |
| `test-6.log` | `ctest -R '^CrashReporterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/test-6.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 8 |
| `test-7.log` | `ctest -R '^CrashReporter(Arm)?Test$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/test-7.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0; 2/2 tests |

Final reverse-patch proof commands both returned 0:

- `git diff -- src/core/CrashReporter.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` — `EXIT=0`.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-67.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` — `EXIT=0`.

## Files committed

- `src/core/CrashReporter.cpp`
- `tests/src/core/CrashReporterTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-67.md`
