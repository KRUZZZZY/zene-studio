FIXED

# Cluster new-p9-16 - ESP32 service runner ignores task creation failure

## Verdict

**REAL, and FIXED on this branch.** The defect is in Ableton Link's ESP32 port, vendored at
`src/3rdparty/ableton-link`. **It is dormant in every Zene build:** no Zene target defines
`ESP_PLATFORM`, so `ableton/platforms/Config.hpp` never selects the port
(`src/3rdparty/ableton-link/link/include/ableton/platforms/Config.hpp:46`, `#elif defined(ESP_PLATFORM)`),
and before this change nothing outside `src/3rdparty/` and `docs/` named it except the
upstream-modifications register (`rg -l 'ESP_PLATFORM|esp32' --glob '!src/3rdparty/**' --glob '!docs/**'`
on `HEAD`'s tree). The fix is therefore verified on the host with a
FreeRTOS stand-in, not on hardware or with ESP-IDF.

Two corrections to the finding:
- The file is `Context.hpp`, not `Context.h`; the defect is the `xTaskCreatePinnedToCore` call at
  `Context.hpp:67` (pre-fix), which the finding placed at line 65.
- The same defect exists in the sibling `LockFreeCallbackDispatcher.hpp` (see "Sibling" below).

## Evidence (pre-fix code, `git show HEAD:...`)

| Claim | Location (pre-fix) |
| --- | --- |
| Return value of `xTaskCreatePinnedToCore` ignored | `Context.hpp:67-73` |
| `mTaskHandle` has no initializer | `Context.hpp:87` |
| Destructor always calls `vTaskDelete(mTaskHandle)` | `Context.hpp:76` |
| Runner is a function-local static, so a broken runner is cached for the process | `Context.hpp:216-220` |
| Failure would otherwise be silent: `async`/`makeTimer`/`openUnicastSocket` all use the runner | `Context.hpp:124,193,200` |

The `std::runtime_error` error model is already used by this file (`Context.hpp:146,186`,
"Unknown Protocol"), which is why the fix throws rather than inventing a new contract.

## Fix (`git diff -- src/3rdparty/ableton-link/link/include/ableton/platforms/esp32/`)

- `Context.hpp:73-82`: the creation result is checked against `pdPASS`; on failure the runner throws
  `std::runtime_error`. The member services and work guard are unique_ptrs, so nothing leaks.
- `Context.hpp:102`: `mTaskHandle = nullptr`.
- `Context.hpp:85-91`: the destructor deletes only a non-null handle.
- `LockFreeCallbackDispatcher.hpp:66-68` (sibling, in `stop()` at lines 56-70): `mTaskHandle = nullptr`
  (line 107), and `stop()` deletes only a non-null handle. Before this, destroying a dispatcher that was
  never started (reachable: a Link `Controller` destroyed without `start()`, `Controller.hpp:570,572`)
  called `vTaskDelete` on an uninitialized handle.
- `src/3rdparty/ableton-link/README.lmms`: the "Local modifications: none" statement is corrected. These
  are the first local changes to the vendored tree; `MANIFEST.tsv` still records the upstream blobs, so
  those two entries now intentionally differ from their upstream hashes.

## Regression test

`tests/src/core/AbletonLinkEsp32RunnerTest.cpp` (next to `AbletonLinkInteropTest.cpp`, the closest Link
area), compiled against `tests/src/core/esp32-freertos-host/freertos/{FreeRTOS,task}.h`, a host stand-in
that records calls and lets the test choose whether creation succeeds. No sleeps, clocks or threads.

| Test | Covers |
| --- | --- |
| `failedTaskCreationIsReported` | a failed creation on the first use throws; one creation attempted |
| `failedTaskCreationIsNotCachedAndRecoversOnRetry` | failure throws, a later attempt succeeds, exactly two creation attempts |
| `dispatcherNeverStartedDeletesNoTask` | destroying an unstarted dispatcher deletes no task (sibling) |

Not covered by a test, on purpose: the destructor's `vTaskDelete` path with a live handle (a
`ServiceRunner` is private; the static is never destroyed before exit), and the real FreeRTOS
out-of-memory condition.

## Commands and EXIT codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-16.r1/logs`. All commands run in the
worktree unless noted.

| # | Command | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `cmake -S . -B build` | 97 | `configure-1.log` (lane guard: "configuring is the parent's job"; not used) |
| 2 | `cmake --build build --target cmake_check_build_system` | 0 | `check-1.log` (regenerated the build files; the new target did not exist before this) |
| 3 | `cmake --build build --target AbletonLinkEsp32RunnerTest -j4` | 2 | `build-1.log` (ccache could not write its temp file under the sandbox `dsh` path) |
| 4 | `CCACHE_TEMPDIR=$PWD/build/ccache-tmp cmake --build build --target AbletonLinkEsp32RunnerTest -j4` | 0 | `build-2.log` |
| 5 | `cd build/tests && ctest -R AbletonLinkEsp32RunnerTest --output-on-failure` (**RED**, original code) | 8 | `test-red-1.log`: `failedTaskCreationIsReported` FAIL at test line 80; `failedTaskCreationIsNotCachedAndRecoversOnRetry` FAIL at line 90 |
| 6 | fix Context.hpp; rebuild (`build-3.log`) and `ctest -R AbletonLinkEsp32RunnerTest -V` (**GREEN**, Context only) | 0 / 0 | `test-green-2-verbose.log`: 4 passed |
| 7 | 4b, Context only: `git diff -- Context.hpp > fix.patch && git apply -R fix.patch`; rebuild; ctest | 0 / 0 / 8 | `build-4b-revert.log`, `test-4b-reverted.log`: the same two FAILs |
| 8 | `git apply fix.patch`; rebuild; ctest | 0 / 0 / 0 | `build-4b-fixed.log`, `test-4b-fixed.log`: 4 passed |
| 9 | sibling test added, Context fixed, sibling unfixed: rebuild; ctest (**RED** for sibling) | 0 / 8 | `build-5-sibling-red.log`, `test-5-sibling-red.log`: `dispatcherNeverStartedDeletesNoTask` Actual 1, Expected 0 (line 122) |
| 10 | sibling fixed; rebuild; ctest (**GREEN**) | 0 / 0 | `build-6-green.log`, `test-6-green.log`: 5 passed |
| 11 | final 4b, both product files: `git diff -- Context.hpp LockFreeCallbackDispatcher.hpp > fix.patch && git apply -R fix.patch`; rebuild; ctest | 0 / 0 / 8 | `build-7-final-revert.log`, `test-7-final-reverted.log`: 3 FAILs (the two Context tests and the sibling test), 2 pass |
| 12 | `git apply fix.patch`; rebuild; ctest | 0 / 0 / 0 | `build-8-final-fixed.log`, `test-8-final-fixed.log`: **5 passed, 0 failed** |
| 13 | family: `cmake --build build --target AbletonLinkSessionTest AbletonLinkInteropTest -j4`; `ctest -R AbletonLink -V` | 0 / 8 | `build-family.log`, `test-family.log` (see "Family") |
| 14 | feature gates, direct: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py $PWD` | 1 | `PermissionError`: the gate writes its log folder beside the worktree, outside the write-jail. Not evidence. |
| 15 | feature gates via `runs/new-p9-16.r1/logs/run_feature_check_logdir.py` (same script, one log-path line redirected) | 1 | `feature-check-1.log`: `fork-sources` and `all-sources-reproduce` FAIL, because `git ls-files` (the recipe) does not see untracked new files |
| 16 | `git add` the three new test files (explicit paths); re-run step 15 | 0 | `feature-check-2.log`: all 8 rows PASS |

Final gate run after the last edit: see "Self-check" and `feature-check-3.log`.

## Family (`ctest -R AbletonLink`, step 13)

- `AbletonLinkEsp32RunnerTest`: passed (5 cases).
- `AbletonLinkInteropTest`: ctest "Passed"; QTest totals `2 passed, 0 failed, 7 skipped`. Its log also
  contains `FAIL!` lines (`zeneJoinsTheLinkSession`, `zenesTempoReachesThePeer`, and others, lines
  1105-1121 of `test-family.log`) that the totals do not count. **Unresolved:** I did not establish why
  QTest reports them as non-failures.
- `AbletonLinkSessionTest`: **SEGFAULT** (SIGSEGV at a null address, in the third test case; ctest 1 of 3
  failed). **Not verified against a master baseline.** Argued unrelated: neither the Session nor the
  Interop translation unit contains an `esp32` reference, and the changed headers are compiled only under
  `ESP_PLATFORM` (`Config.hpp:46`), which no Zene build defines. So this diff cannot reach those binaries.
  I did not run the crash under a debugger, so its cause is unknown.

## Residual (not fixed here)

- `LockFreeCallbackDispatcher::start()` (`LockFreeCallbackDispatcher.hpp`, the `xTaskCreate` line) still
  ignores a failed creation. **NEEDS-CONTEXT.** Making it throw would propagate through
  `Controller::start()` (`Controller.hpp:570`), and I have not reviewed what Link's enable path does with
  an exception on ESP32. The guarded `stop()` makes a failed start safe to destroy; the callback simply
  never runs.
- The ESP-IDF build itself was not run; there is no ESP toolchain on this host. Exceptions: the port
  already throws `std::runtime_error` (`Context.hpp:146,186`), so this adds no new build requirement.
- Vendored-tree divergence: two files now differ from `MANIFEST.tsv` (recorded in `README.lmms`).

## Self-check

(to be filled from `submit_check.py`)
