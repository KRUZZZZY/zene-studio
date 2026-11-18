# Cluster new-p9-480-port

**Verdict: FIXED** (ported by hand onto `050/m0-train-5` from `bugs/qwen-new-p9-480`, commit `9684b2f5f`).
The newest line did not already fix it (the constructor had no cleanup path), so the fix was re-applied.

The newest line already carried `tests/src/core/ThreadPoolTest.cpp` (cluster new-p9-478), which is why the
original merge conflicted. The regression was therefore added to that file, not copied over it.

## Evidence

- [src/core/ThreadPool.cpp:34-58](../../src/core/ThreadPool.cpp#L34-L58): the worker launch loop is in a `try`.
  On any exception the constructor sets `m_done`, notifies, joins every started worker, and rethrows.
  Before the fix the constructor had no `try` (the original loop was at lines 34-37).
- [tests/src/core/ThreadPoolTest.cpp:73-96](../../tests/src/core/ThreadPoolTest.cpp#L73-L96): glibc `pthread_create`
  is interposed (only when the binary exports its symbols). The child process allows one successful creation,
  then the second returns `EAGAIN`.
- [tests/src/core/ThreadPoolTest.cpp:99-116](../../tests/src/core/ThreadPoolTest.cpp#L99-L116): child body. It builds
  `lmms::ThreadPool pool{2}` and requires `std::system_error` plus exactly one successful and one injected create
  (exit 0). Exit 3 means no exception; exit 2 means wrong counts.
- [tests/src/core/ThreadPoolTest.cpp:160-177](../../tests/src/core/ThreadPoolTest.cpp#L160-L177): parent test runs the
  child as a `QProcess` and requires a normal exit within 10 s and exit code 0.
- [tests/CMakeLists.txt:1461-1466](../../tests/CMakeLists.txt#L1461-L1466): Linux-only `ENABLE_EXPORTS ON` and
  `${CMAKE_DL_LIBS}` for `ThreadPoolTest`, so the interposer binds.
- Registration: `tests/upstream-modifications.txt` gets one line for `src/core/ThreadPool.cpp` and one for
  `tests/CMakeLists.txt`. `ThreadPoolTest.cpp` is already in `tests/fork-sources.txt` (no new file is added).
- No control command is added, so the `ControlCommandsSnapshot` canary is untouched.

## Commands and exit codes

`$LOGS` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-480-port.r1/logs`.
Builds were run from the worktree root; tests from `build/tests`.

| Step | Command | EXIT |
|---|---|---:|
| Build, first try (RED, environment) | `cmake --build build --target zene ThreadPoolTest -j2 > $LOGS/build-1-red.log 2>&1; echo EXIT=$?` | 2 |
| Cause | ccache could not write `/run/user/1000/ccache-tmp` (permission denied), see `build-1-red.log`. Not a code error. | |
| Build, RED (unfixed product + new test) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-newp9 cmake --build build --target zene ThreadPoolTest -j2 > $LOGS/build-2-red.log 2>&1; echo EXIT=$?` | 0 |
| **RED test** | `ctest -R '^ThreadPoolTest$' --output-on-failure > $LOGS/test-1-red.log 2>&1; echo EXIT=$?` | **8** |

RED assertion (`test-1-red.log`):
`FAIL! : ThreadPoolTest::partialStartupFailureStopsStartedWorkers() The child did not unwind after a worker-start failure.`
`Totals: 3 passed, 1 failed`. The child did not exit within 10 s.

Direct run of the unfixed child: `timeout 5 ./ThreadPoolTest --thread-pool-failure-child` → EXIT=124 (still running at the
timeout). `gdb -p` could not attach (`ptrace_scope` 1), so the mechanism is not verified. CrashReporter's SIGABRT path
was not confirmed as the cause.

Mutation proof (step 4b, product file only, `fix.patch` = `git diff -- src/core/ThreadPool.cpp`):

| Step | Command | EXIT |
|---|---|---:|
| Revert fix | `git apply -R $LOGS/fix.patch` | 0 |
| Rebuild (mutant) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-newp9 cmake --build build --target zene ThreadPoolTest -j2 > $LOGS/build-4-mutant.log 2>&1; echo EXIT=$?` | 0 |
| **Mutant test** | `ctest -R '^ThreadPoolTest$' --output-on-failure > $LOGS/test-3-mutant.log 2>&1; echo EXIT=$?` | **8** |

Mutant assertion (`test-3-mutant.log`):
`FAIL! : ThreadPoolTest::partialStartupFailureStopsStartedWorkers() 'child.exitStatus() == QProcess::NormalExit' returned FALSE. (The child crashed instead of unwinding.)`

| Step | Command | EXIT |
|---|---|---:|
| Re-apply fix | `git apply $LOGS/fix.patch` | 0 |
| Rebuild (fixed) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-newp9 cmake --build build --target zene ThreadPoolTest -j2 > $LOGS/build-5-green-again.log 2>&1; echo EXIT=$?` | 0 |
| **GREEN test** | `ctest -R '^ThreadPoolTest$' --output-on-failure > $LOGS/test-4-green-again.log 2>&1; echo EXIT=$?` | **0** |
| GREEN, direct binary | `./ThreadPoolTest > $LOGS/direct-green-1.log 2>&1; echo DIRECT_EXIT=$?` | 0 |
| Stability | `ctest -R '^ThreadPoolTest$'` run 5 times | 0, 0, 0, 0, 0 |

**Affected-family count:** `ThreadPoolTest` is the only ctest in the family (1 test executed). The binary runs 4
QTest functions (`initTestCase`, `destructionDrainsQueuedTasks`, `partialStartupFailureStopsStartedWorkers`,
`cleanupTestCase`): `Totals: 4 passed, 0 failed, 0 skipped` (`direct-green-1.log`). No warnings from the changed files
(`build-3-green.log`).

Observed variance: the first RED run hit the 10 s budget (no exit). The mutant run crashed in 282 ms. The direct
child run of the unfixed code also hit the timeout. Both outcomes are failures of the same kind (no normal exit), but
the reason for the difference is not root-caused.

## Ratchet gates

| Command | EXIT |
|---|---:|
| `bash tests/complexity-gate.sh --check > $LOGS/gate-complexity-1.log 2>&1; echo EXIT=$?` | 1 |
| `bash tests/file-length-gate.sh --check > $LOGS/gate-filelength-1.log 2>&1; echo EXIT=$?` | 1 |

Both gates are red **on the base tree too**, and their REGRESSION lines are identical. I ran both on a `git archive HEAD`
export in `/tmp/newp9-head-gates` (`gate-complexity-head.log` EXIT=1, `gate-filelength-head.log` EXIT=1). Neither list
names `ThreadPool.cpp`, `ThreadPoolTest.cpp` or `tests/CMakeLists.txt`.
- Complexity: `dotCentreAlong` (XyPadPairTest), `parseNotes` (DawProjectReadTracks), `adjust`, `setStrip`,
  `rotateProjectRevision`, `importedOverrides` (CCN 11 to 16).
- File length: 14 files between 504 and 752 lines (e.g. `SmfInterchangeReader.cpp` 504, `DeviceDspTest.cpp` 752).

The gates were not re-anchored and no baselines or limits were changed. Clearing the gates means refactoring
about 20 unrelated files, which is out of scope for this port, so I did not do it.

## Files changed

- `src/core/ThreadPool.cpp`: constructor cleanup on worker-start failure.
- `tests/src/core/ThreadPoolTest.cpp`: partial-startup regression (child process plus `pthread_create` interposer; custom `main`).
- `tests/CMakeLists.txt`: Linux-only `ENABLE_EXPORTS` and `CMAKE_DL_LIBS` for `ThreadPoolTest`.
- `tests/upstream-modifications.txt`: entries for the two inherited files above.
- `docs/qwen-fixes-2026-10/cluster-new-p9-480-port.md`: this report.
