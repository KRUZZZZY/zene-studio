ALREADY-FIXED

# cluster ci-443a2931bcb5dacc-port — Port: CI regression: build / windows-arm64

ci-443a2931bcb5dacc-port: ALREADY-FIXED

## What the finding is

The finding carries only a title. The original cluster's report on this branch,
`docs/qwen-fixes-2026-10/cluster-ci-443a2931bcb5dacc.md`, names the failure. The hosted job
`build / windows-arm64` (run 43611324689) failed to link `plugins\nam_harness.exe` with
`ld.lld: error: undefined symbol: clock_gettime64`. The cause was a direct
`clock_gettime(CLOCK_THREAD_CPUTIME_ID, ...)` call in `plugins/NeuralAmp/tests/nam_harness.cpp`.
On MSYS2 clangarm64, `<pthread_time.h>` inlines that call to `clock_gettime64`, and the
link line does not link winpthread.

This job ports that fix onto this branch. The fix is already here.

## Evidence (HEAD e4194e0bf5)

- `plugins/NeuralAmp/tests/nam_harness.cpp:635,637,753,755` time both loops through
  `namThreadCpuMicroseconds()`. A search of `plugins/NeuralAmp/` finds no `clock_gettime`
  in `nam_harness.cpp`. The only hits are in `HarnessClockPosix.cpp:34`, `NamProfile.h:63`
  and `nam_profile.cpp:72`.
- `plugins/NeuralAmp/tests/NamHarnessCpuClock.h:4-15` wraps `lmms::nam::tryThreadCpuTimeUs()`
  from `plugins/NeuralAmp/tests/HarnessClock.h:40`.
- `plugins/NeuralAmp/tests/HarnessClockWindows.cpp:57-60` uses `GetThreadTimes()`.
  `HarnessClockPosix.cpp:31-34` uses `clock_gettime`.
- `plugins/NeuralAmp/tests/CMakeLists.txt:30-41` selects the Windows source under `WIN32`
  and publishes `ZENE_NAM_HARNESS_CLOCK_SOURCE`. `tests/CMakeLists.txt:3446-3448` gives
  `NeuralAmpWavInputTest` the same source, because that test includes the harness TU.
- The profiler path that still calls `clock_gettime` is `nam_profile`. It is built only
  `IF(NOT WIN32)` (`plugins/NeuralAmp/tests/CMakeLists.txt:98`), so it cannot reach the
  Windows link.
- The regression test `NamHarnessWindowsClockCheck` is registered at
  `tests/CMakeLists.txt:6617-6624` (`tests/nam-harness-windows-clock-check.py`).
- `git merge-base --is-ancestor <commit that added HarnessClock.h> HEAD` returned
  `ANCESTOR_EXIT=0`. The last commit to touch those files is 14e637c600, the train-sol
  portability consolidation.

No product change was needed, so this job adds no new regression test. The original
cluster's test, `NamHarnessWindowsClockCheck`, is present, and it passes on this branch
(below). Its RED/GREEN proof is in the original report.

## Commands and exit codes (logs in `/home/kruzzzzy/.config/kads/zene/state/runs/ci-443a2931bcb5dacc-port.r2/logs`)

| Command | EXIT | Result |
|---|---|---|
| `cmake --build build --target nam_harness NeuralAmpWavInputTest -j4 > logs/build-1.log 2>&1` | 0 | `Built target NeuralAmpWavInputTest` |
| `cd build/tests && ctest -N -R "NamHarnessWindowsClockCheck\|NeuralAmpWavInputTest\|Train43Portability\|Portability" > logs/test-0-list.log 2>&1` | 0 | 9 tests listed |
| `cd build/tests && ctest -R "NamHarnessWindowsClockCheck\|NeuralAmpWavInputTest" --output-on-failure > logs/test-1.log 2>&1` | 0 | `100% tests passed out of 2` |
| `cd build/tests && ctest -R "Train43Portability" --output-on-failure > logs/test-2.log 2>&1` | 0 | `100% tests passed out of 1` |
| `python3 .../feature_check.py <worktree> > logs/gates-1.log 2>&1` | 0 | `RESULT: PASS` (every row PASS) |

Not verified here: an actual windows-arm64 (MSYS2 clangarm64) link. This machine is Linux.
`NamHarnessWindowsClockCheck` reproduces the bug at the object-symbol level and compiles
the Win32 clock against a model of the Windows declarations. The hosted job is the
final judge.

## Self-check

- Verdict with evidence: ALREADY-FIXED. The file:line evidence and the ancestry check are above.
- Regression test: no new test, because no product code changed. The existing
  `NamHarnessWindowsClockCheck` covers the bug and passed (`test-1.log`).
- Fix at cause / siblings: `NeuralAmpWavInputTest` gets the same clock source
  (`tests/CMakeLists.txt:3446`). `nam_profile` is non-Windows only (`plugins/NeuralAmp/tests/CMakeLists.txt:98`).
- Collateral / realtime / undo / commands: nothing changed. This commit adds this report only.
- Gates: `feature_check.py` EXIT=0, `RESULT: PASS`.

Answers to `submit_check.py` (`logs/submit-1.log`, EXIT=0, `RESULT: PASS - submit`):

- *Regression test fails with the fix reverted and passes with it:* this job made no product
  change, so there is nothing to revert. The existing test has RED/GREEN proof in
  `cluster-ci-443a2931bcb5dacc.md`, from that run's `test-10-red-final.log` and
  `test-11-green-restored.log`. On this branch it passes: `NamHarnessWindowsClockCheck`
  Passed 1.81 sec, EXIT=0 (`test-1.log`).
- *Deterministic:* the test inspects `nm -u` symbols of the recompiled TU. It checks that the
  POSIX clock is non-decreasing across two immediate calls, with no threshold and no sleep.
  It also links the Win32 clock against a stub `GetThreadTimes` with injected values:
  1000 + 500 ticks must give 150 us, and a failed call must give 0. There is no wall-clock
  timing and no race.
- *Fix at the cause and complete, siblings checked:* the harness TU names no platform clock
  (`nam_harness.cpp:635,637,753,755`). The other TU that includes it, `NeuralAmpWavInputTest`,
  gets the same source (`tests/CMakeLists.txt:3446`). The remaining `clock_gettime` user,
  `nam_profile`, is `IF(NOT WIN32)` (`plugins/NeuralAmp/tests/CMakeLists.txt:98`).
- *No collateral damage:* this commit changes only this report. No project-load, saved-data
  or limit code changed.
- *ALREADY-FIXED evidence:* see "Evidence" above (`nam_harness.cpp:635`,
  `plugins/NeuralAmp/tests/CMakeLists.txt:30-41`, `HarnessClockWindows.cpp:57-60`, ancestry
  check EXIT=0).
- *Sibling commands (app.\*, arrangement.\*) and the listed test files (control-\*.py):*
  they show up because the check diffs the whole branch against its base (575 changed files,
  `gates-1.log`). This job did not touch them, and they have nothing to do with the NAM
  harness link. They are unaffected.

Delegation: Haiku ran the build and the three ctest invocations and reported the exit codes.
I checked the log tails myself. I did the diagnosis, the code reading, the gates and the
report myself, because each was smaller than writing a request. Sonnet and DeepSeek were
not used: there was no fix or test to write.

## Other findings

- `ctest -N` printed "Could not find executable .../VideoTestClipPortabilityTest" in this
  build tree (`test-0-list.log`). That target is not built here. It is unrelated, and I did
  not investigate it.
