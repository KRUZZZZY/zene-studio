ALREADY-FIXED

# cluster ci-443a2931bcb5dacc-port — Port: CI regression: build / windows-arm64

ci-443a2931bcb5dacc-port: ALREADY-FIXED (product); regression coverage gap closed (round 3)

## Verdict

The hosted `build / windows-arm64` job could not link the NAM harness:
`ld.lld: undefined symbol: clock_gettime64`. The cause was a direct `clock_gettime` call in a
translation unit that includes `plugins/NeuralAmp/tests/nam_harness.cpp`. On MSYS2
clangarm64, `<pthread_time.h>` inlines that call to `clock_gettime64`, and the link line
has no winpthread. The original cluster `ci-443a2931bcb5dacc` fixed it, and that fix is on
this branch:

- `plugins/NeuralAmp/tests/nam_harness.cpp:635,637,753,755` time through
  `namThreadCpuMicroseconds()` (`plugins/NeuralAmp/tests/NamHarnessCpuClock.h:7-15`), which
  wraps `lmms::nam::tryThreadCpuTimeUs()` (`plugins/NeuralAmp/tests/HarnessClock.h:40`).
- `plugins/NeuralAmp/tests/CMakeLists.txt:30-41` selects `HarnessClockWindows.cpp`
  (`GetThreadTimes`, `:57-60`) under `IF(WIN32)` and `HarnessClockPosix.cpp` otherwise. It
  publishes the choice as `ZENE_NAM_HARNESS_CLOCK_SOURCE`.
- `tests/CMakeLists.txt:3446-3448`: `NeuralAmpWavInputTest`, which `#include`s the harness
  TU, takes that property as a source.
- `nam_profile` is the remaining `clock_gettime` user, and it is built only
  `IF(NOT WIN32)` (`plugins/NeuralAmp/tests/CMakeLists.txt:98`).

## Gap closed this round (reviewer point)

`NamHarnessWindowsClockCheck` checked only `nam_harness.cpp`. Nothing checked the second
compile unit (`tests/src/core/NeuralAmpWavInputTest.cpp`) or the
`ZENE_NAM_HARNESS_CLOCK_SOURCE` wiring. I extended `tests/nam-harness-windows-clock-check.py`
(499 lines):

- `check_harness_symbols(entry, scratch, relative)` now takes the TU path. `main()` runs it
  for `HARNESS` and for `WAV_TEST = tests/src/core/NeuralAmpWavInputTest.cpp`. Each TU is
  recompiled from its `compile_commands.json` line and must show no
  `clock_gettime`/`clock_gettime32`/`clock_gettime64` in `nm -u`. A missing
  compile-commands entry is a FAIL (exit 2).
- `check_wav_test_clock_wiring(build, root)` has two halves:
  - **Native half:** `compile_commands.json` must compile `HarnessClockPosix.cpp` into
    `NeuralAmpWavInputTest.dir` and must not compile `HarnessClockWindows.cpp` there.
  - **Windows half (static, since WIN32 cannot be configured here):** the `IF(WIN32)` branch
    must set `HarnessClockWindows.cpp` and the `ELSE()` branch `HarnessClockPosix.cpp`. The
    property must come from `${NEURALAMP_HARNESS_CLOCK_SOURCE}`. `tests/CMakeLists.txt` must
    `get_property` it and list `"${NAM_HARNESS_CLOCK_SOURCE}"` in
    `add_executable(NeuralAmpWavInputTest ...)`.

The test is deterministic. It does symbol inspection and text matching only, with no
timing and no sleeps. The existing halves (detector self-test, POSIX monotonic check, Win32
stub with injected 1000+500 ticks) are unchanged. No product file changed.

## RED / GREEN (logs in `/home/kruzzzzy/.config/kads/zene/state/runs/ci-443a2931bcb5dacc-port.r3/logs`)

The product fix is already in the tree, so each RED reintroduces the defect temporarily.
Each file was restored by copying it back; `git diff --stat` on it was then empty.

| Run | Command | EXIT | Key line |
|---|---|---|---|
| RED A: `clock_gettime(CLOCK_THREAD_CPUTIME_ID, …)` appended to `tests/src/core/NeuralAmpWavInputTest.cpp` | `cd build/tests && ctest -R NamHarnessWindowsClockCheck --output-on-failure > logs/test-1-red-wavtu.log 2>&1` | 8 | `FAIL: tests/src/core/NeuralAmpWavInputTest.cpp itself references a POSIX clock: clock_gettime` |
| RED B: `"${NAM_HARNESS_CLOCK_SOURCE}"` dropped from `add_executable(NeuralAmpWavInputTest` | `cd build/tests && ctest -R NamHarnessWindowsClockCheck --output-on-failure > logs/test-2-red-wiring.log 2>&1` | 8 | `FAIL: tests/CMakeLists.txt: add_executable(NeuralAmpWavInputTest ...) must list ${NAM_HARNESS_CLOCK_SOURCE} as a source` |
| RED C: `IF(WIN32)` branch switched to `HarnessClockPosix.cpp` | `python3 tests/nam-harness-windows-clock-check.py --source-root . --build-dir build > logs/script-red-static.log 2>&1` | 1 | `FAIL: plugins/NeuralAmp/tests/CMakeLists.txt: the IF(WIN32) branch must SET(...HarnessClockWindows.cpp)` |
| GREEN script | `python3 tests/nam-harness-windows-clock-check.py --source-root . --build-dir build > logs/script-green-0.log 2>&1` | 0 | all 7 halves `PASS` |
| GREEN ctest | `cd build/tests && ctest -R "NamHarnessWindowsClockCheck\|NeuralAmpWavInputTest" --output-on-failure > logs/test-3-green.log 2>&1` | 0 | `100% tests passed out of 2` |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-1.log 2>&1` | 0 | `RESULT: PASS` (complexity and file-length: no new item) |

In RED A, the pre-existing `nam_harness.cpp` half still printed PASS. Only the new
`NeuralAmpWavInputTest.cpp` half failed, which shows that this is the gap the reviewer named.

Not verified: a real windows-arm64 link. This box is Linux, so the hosted job remains the
final check.

## Self-check

- *RED with the fix reverted, GREEN with it:* the defect was reintroduced three ways (RED A,
  B and C above), with exits 8, 8 and 1. After the restore, GREEN gave 0 (script and ctest).
  No product file changed in this job, so `fix.patch` would be empty. The reverts were
  applied as temporary edits to the defect sites and are recorded in the table.
- *Deterministic:* the test uses `nm -u` symbol inspection of recompiled objects, regex
  checks of CMake text, and the existing Win32 stub with injected tick values. There are no
  sleeps, no wall clock and no races.
- *At the cause and complete, siblings checked:* both TUs that compile the harness are now
  checked (`nam_harness.cpp`, `NeuralAmpWavInputTest.cpp`). The clock selection and its only
  consumer are checked. `nam_profile` is excluded on Windows
  (`plugins/NeuralAmp/tests/CMakeLists.txt:98`).
- *No collateral damage:* only the test script changed. No product, project-load, saved-data
  or limit code was touched, and no control commands were added.
- *ALREADY-FIXED evidence:* see the file:line list under "Verdict".
- *Sibling commands (app.\*, arrangement.\*) listed by submit_check:* the checker reports them
  from the branch-wide diff against its base (577 changed files, `gates-1.log`). This job
  changed no command file, so they are unaffected.

Delegation: Sonnet designed-to-spec and wrote the two new checks, then ran the green script
and RED C (and an extra consumer red, `script-red-consumer.log`). Haiku ran RED A, RED B
and GREEN through ctest and restored the files. I did the diagnosis, the design, the diff
review, the log checks, the gates, the report and the commit. DeepSeek was not needed.

## Other findings

- `ctest -N` in this tree reports a missing `VideoTestClipPortabilityTest` executable
  (seen in round 2). It is unrelated and I did not investigate it.
