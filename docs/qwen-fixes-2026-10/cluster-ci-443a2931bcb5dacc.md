FIXED — cluster ci-443a2931bcb5dacc (CI regression: build / windows-arm64)

## Verdict and cause

REAL on this branch. The defect is in the headless NAM harness's own test source, not in
the CI infrastructure and not in the plugin's runtime path.

The hosted `build / windows-arm64` job (run 43611324689, job 132496796733) stopped at
`[2690/6489] Linking CXX executable plugins\nam_harness.exe`:

    FAILED: [code=1] plugins/nam_harness.exe
    ld.lld: error: undefined symbol: clock_gettime64
    >>> referenced by C:/a/_temp/msys64/clangarm64/include/pthread_time.h:102
    >>>               plugins/NeuralAmp/tests/CMakeFiles/nam_harness.dir/nam_harness.cpp.obj:(main)
    >>> referenced 1 more times

The link line in that log names exactly three objects — `nam_harness.cpp.obj`,
`NamModel.cpp.obj`, `NamModelLoader.cpp.obj` — and no `-lwinpthread`. The failing object is
the harness's own translation unit:

- At `c20ef591b5`, `plugins/NeuralAmp/tests/nam_harness.cpp:34` includes `<ctime>` and
  :634, :636, :754 and :756 call `clock_gettime(CLOCK_THREAD_CPUTIME_ID, ...)` directly.
- On MSYS2's mingw-w64, `<time.h>` includes `<pthread_time.h>` (upstream
  `mingw-w64-headers/crt/time.h:203`). That header defines `clock_gettime()` as an inline
  that calls `clock_gettime64()` when `WINPTHREADS_TIME_BITS == 64` — the line the linker
  names is `pthread_time.h:102`.
- `nam_harness` links only `${NEURALAMP_M_LIB} ${CMAKE_DL_LIBS}`
  (`plugins/NeuralAmp/tests/CMakeLists.txt:72`). GCC's mingw64 driver links winpthread
  implicitly, so mingw64 linked the same source; clangarm64's driver does not, so only
  windows-arm64 failed.
- The profiler is not the cause: `plugins/NeuralAmp/nam/NamProfile.h:63` and
  `plugins/NeuralAmp/tests/nam_profile.cpp:72` also name `clock_gettime`, but only under
  `NAM_PROFILE_LAYERS`, which is defined solely for the `nam_profile` target
  (`plugins/NeuralAmp/tests/CMakeLists.txt:94-107`), and that target is `IF(NOT WIN32)`.
  The harness's object proved free of the symbol after the fix (below).

It is not ALREADY-FIXED: the symbol is still referenced by the pre-fix harness source, and
the regression reproduces that reference on Linux (`test-1-red.log`). It is not a
FALSE-POSITIVE: the hosted linker output names the harness object at `main`, not a
dependency artefact.

## Fix

One platform clock behind a header, selected at configure time:

- `plugins/NeuralAmp/tests/HarnessClock.h` (new): declares
  `lmms::nam::threadCpuTimeUs()` in microseconds, returning 0 on failure, documented as an
  offline measurement that must not run on the audio thread (`HarnessClock.h:38-41`).
- `plugins/NeuralAmp/tests/HarnessClockPosix.cpp` (new, `:31-37`): the same
  `clock_gettime(CLOCK_THREAD_CPUTIME_ID, ...)` arithmetic as before, now in its own
  translation unit. POSIX semantics are unchanged.
- `plugins/NeuralAmp/tests/HarnessClockWindows.cpp` (new, `:57-65`): `GetThreadTimes()` on
  `GetCurrentThread()`; kernel and user `FILETIME`s are 100 ns ticks, `ticks / 10` is
  microseconds (`:47-53`); a failed call returns 0. It uses the Win32 API directly, so the
  harness no longer depends on winpthreads at all (MSVC has none either).
- `plugins/NeuralAmp/tests/CMakeLists.txt:30-41` selects the implementation (`WIN32` ->
  Windows, otherwise Posix), adds the chosen source and the header to `nam_harness`
  (:56-61), and publishes the choice as global property `ZENE_NAM_HARNESS_CLOCK_SOURCE`.
- `plugins/NeuralAmp/tests/nam_harness.cpp:27,636-638,753-755` calls the interface at both
  timed loops. The measured quantity and the reported `cpu per block` are unchanged
  (`run-1-harness-cpu.log`: finite output, `cpu per block: 5.54 us`, all checks PASS).

Sibling and completeness:

- `tests/src/core/NeuralAmpWavInputTest.cpp:24` includes `nam_harness.cpp` directly, so
  `NeuralAmpWavInputTest` compiles the harness TU too. It failed to link with
  `undefined reference to lmms::nam::threadCpuTimeUs()` (`build-4-missing.log`) until
  `tests/CMakeLists.txt:3290-3296` gave it the same platform source through the exported
  property. That target now builds and passes.
- A tree-wide search finds `clock_gettime` only in the new POSIX clock, the Windows-only
  profiler path above, and the new regression; no other target compiles the harness TU.
- Linking `-lwinpthread` was rejected: it would leave the CLI source naming a POSIX clock
  and would not cover MSVC. `GetThreadTimes` is the native equivalent of
  `CLOCK_THREAD_CPUTIME_ID`.

No product runtime, audio-thread path, control command, undo step or saved-data path is
touched: the diff is confined to the offline harness, its CMake source selection, CTest
registration and the project's registration ledgers.

## Regression test and RED/GREEN proof

`tests/nam-harness-windows-clock-check.py` (new, 426 lines; CTest
`NamHarnessWindowsClockCheck` registered at `tests/CMakeLists.txt:9055-9062`, Linux-only).
It has four deterministic halves:

1. **Detector self-test.** A fixture that calls `clock_gettime` must be seen in `nm -u`, and
   a clean fixture must stay unseen. A check that cannot fail therefore fails this test.
2. **The real harness TU.** `nam_harness.cpp` is recompiled with the configured tree's own
   recorded command from `build/compile_commands.json`, and its undefined symbols are
   inspected. Any `clock_gettime`, `clock_gettime32` or `clock_gettime64` is a failure. On
   Linux this reproduces the hosted Windows link failure at the object level.
3. **The POSIX implementation.** `HarnessClockPosix.cpp` is compiled and run; the value
   must be finite, non-negative and non-decreasing across two immediate calls. No sleeps
   and no wall-clock threshold.
4. **The Win32 implementation.** `HarnessClockWindows.cpp` is compiled against a model of
   the Microsoft declarations it uses and linked with a stub `GetThreadTimes`: 1000 + 500
   ticks must yield exactly 150 us, and a failed call must yield 0.

It also reads the generated `build/tests/CTestTestfile.cmake` and requires its own
registration, so a tree that silently drops the CTest fails the check.

Reverted-state output, from `test-10-red-final.log`:

    PASS: the detector sees clock_gettime in a red fixture and stays silent on a clean one
    FAIL: plugins/NeuralAmp/tests/nam_harness.cpp itself references a POSIX clock: clock_gettime
          The Windows jobs cannot link that (MSYS2 clangarm64's <pthread_time.h>
          calls clock_gettime64 and the link line has no libwinpthread); use
          lmms::nam::threadCpuTimeUs() from HarnessClock.h instead.

Restored-state output, from `test-11-green-restored.log`:

    PASS: the detector sees clock_gettime in a red fixture and stays silent on a clean one
    PASS: plugins/NeuralAmp/tests/nam_harness.cpp references no POSIX clock symbol
    PASS: POSIX clock: finite, non-negative, non-decreasing thread CPU time
    PASS: Win32 clock: kernel+user 100 ns ticks are microseconds; failure yields 0
    PASS: the configured tree registered NamHarnessWindowsClockCheck

## Test matrix

Every ctest row below was run from `build/tests`; every direct script row from the worktree
root. `EXIT` is the command's own exit code.

| Requirement | Test / command | Result |
|---|---|---|
| Harness TU names no POSIX clock | `python3 tests/nam-harness-windows-clock-check.py --source-root . --build-dir build` after the fix (`test-9-green-final.log`, `test-11-green-restored.log`) | PASS, EXIT 0 |
| Same check with the product change reverted | `git apply -R fix.patch` then the script (`test-10-red-final.log`) | FAIL, EXIT 1 |
| POSIX clock still measures this thread | POSIX probe inside `NamHarnessWindowsClockCheck` | PASS |
| Win32 clock arithmetic and failure path | Win32 probe against the `<windows.h>` model | PASS |
| Sibling target that includes the harness | `NeuralAmpWavInputTest` after the source was added (`build-5-siblings.log`; ctest family) | PASS |
| Harness output and CPU report unchanged | `nam_harness --model ... --cpu` (`run-1-harness-cpu.log`) | PASS, EXIT 0 |
| NAM harness/loader family | `ctest -R "NeuralAmpWavInputTest|AudioPluginTest|NAMHarnessWavWriteFailure|NeuralAmpCompareRateTest|NamLoaderTest_|NeuralAmpWavRunSampleRateWarning|NeuralAmpHarnessRateValidation|NamHarnessWindowsClockCheck|NeuralAmpHarnessBlockCount" --output-on-failure` (`test-12-family-final.log`) | PASS, 18/18, EXIT 0 |
| Native windows-arm64 link | needs the hosted runner; see "Blocked capability" | not runnable here |

## Evidence (exact commands and exit codes)

Worktree:
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-ci-443a2931bcb5dacc`.
`LOGS` below is
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-443a2931bcb5dacc.r1/logs`.

| Step | Command | Exit |
|---|---|---|
| RED before the fix | `python3 tests/nam-harness-windows-clock-check.py --source-root . --build-dir build` (`test-1-red.log`) | 1 |
| Build the fixed harness | `cmake --build build --target nam_harness` (`build-1.log`) | 0 |
| GREEN after the fix | the script (`test-2-green.log`) | 0 |
| Family build (NAM/AudioPlugin targets) | `cmake --build build --target ...` (`build-3-audioplugintest.log`, `build-5-siblings.log`) | 0 |
| Sibling failure before the source was added | `NeuralAmpWavInputTest` link (`build-4-missing.log`) | 2 (undefined `lmms::nam::threadCpuTimeUs()`) |
| Sibling fixed | `cmake --build build --target nam_harness NeuralAmpWavInputTest` (`build-5-siblings.log`) | 0 |
| Family suite before the 4b pass | `/usr/bin/ctest -R ...` from `build/tests` (`test-4-family-green.log`) | 0 (18/18) |
| 4b: reverse the product change | `git apply -R "$LOGS/fix.patch"` (`proof-3-reverse.log`) | 0 |
| 4b: RED after the reverse | the script (`test-10-red-final.log`) | 1 |
| 4b: restore the product change | `git apply "$LOGS/fix.patch"` (`proof-4-restore.log`) | 0 |
| 4b: GREEN after the restore | the script (`test-11-green-restored.log`) | 0 |
| Final rebuild of the rebuilt targets | `cmake --build build --target nam_harness NeuralAmpWavInputTest` (`build-8-final.log`) | 0 |
| Final family suite | `/usr/bin/ctest -R ...` from `build/tests` (`test-12-family-final.log`) | 0 (18/18) |
| Harness behavioural sanity | built harness on the minimal model (`run-1-harness-cpu.log`) | 0 |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`feature-check-2-with-report.log`) | 0 (all eight rows PASS) |
| Pre-submit check | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> ci-443a2931bcb5dacc` (`submit-check-1.log`) | 0 (no FAIL; every ASK answered below) |

`fix.patch` in `LOGS` is the product diff (`nam_harness.cpp`, the platform source selection,
and the three new `HarnessClock*` files). The registration test and the report are not in
it, so the 4b pass reverses only the product change and leaves the regression in place.

## Blocked capability

- **Hosted windows-arm64 native link.** This workspace is Linux; an MSYS2 clangarm64 link
  cannot run here. The object-level detector reproduces the hosted condition on this host
  (the pre-fix TU names `clock_gettime`), and the Win32 implementation's arithmetic and
  failure path are proven against the Microsoft declarations. Native confirmation still
  needs the hosted job; that is the job this change unblocks.

## Self-check

- **The regression FAILS with the product change reverted and PASSES with it.** Reverted:
  direct script EXIT 1 with the exact finding above (`test-10-red-final.log`), after
  `git apply -R fix.patch` exited 0. Restored: `git apply fix.patch` exited 0, direct script
  EXIT 0 (`test-11-green-restored.log`), CTest family 18/18 EXIT 0
  (`test-12-family-final.log`). The detector self-test also proves the pre-fix fixture is
  seen, so the check cannot pass by failing to detect the defect.
- **Deterministic.** No wall-clock threshold, sleep, network or race. The check recompiles
  fixed sources with the build's recorded command, inspects `nm -u` names, and runs two
  probes with injected values (Win32: 1000 + 500 ticks -> exactly 150 us, then a failed
  call -> 0; POSIX: finite, non-negative, non-decreasing across two immediate calls, with
  equality allowed). It also reads the generated `CTestTestfile.cmake`. Nothing depends on
  machine speed or on a binary built earlier.
- **The fix is at the cause and complete: every caller and sibling was checked.**
  `nam_harness.cpp` had four direct calls in two loops; both now call
  `threadCpuTimeUs()`. Its only other compilation is `NeuralAmpWavInputTest`, which
  `#include`s the TU (`tests/src/core/NeuralAmpWavInputTest.cpp:24`); it now compiles the
  same platform source, and its earlier undefined reference (`build-4-missing.log`) is
  fixed. The only other `clock_gettime` users in the tree are
  `NamProfile.h:63`/`nam_profile.cpp:72`, which are behind `NAM_PROFILE_LAYERS` for the
  POSIX-only `nam_profile` target; the harness does not define that macro. No other target
  compiles the harness TU.
- **No collateral damage.** The change is confined to the offline harness and its CTest:
  no product source, no saved-data format, no project load/render path and no control
  command changes. No limit or check was added to product code. The harness's measured
  quantity is unchanged (same thread CPU microseconds), as the rebuilt harness shows
  (`run-1-harness-cpu.log`: `cpu per block: 5.54 us`, output finite and non-zero), and the
  18-test NAM/AudioPlugin family passes unchanged.
- **Verdict is REAL, not ALREADY-FIXED or FALSE-POSITIVE.** The pre-fix harness source
  references the clock (RED above), the hosted linker names that object at `main`, and the
  fix removes the reference rather than masking the link.
- **Registration and gates.** The new header, both implementations, the regression script
  and this report are registered in `tests/fork-sources.txt`; the inherited
  `nam_harness.cpp`, `plugins/NeuralAmp/tests/CMakeLists.txt` and `tests/CMakeLists.txt`
  changes are recorded in `tests/upstream-modifications.txt`, in the same commit.
  `feature_check.py` exits 0 with every row PASS (`feature-check-2-with-report.log`); the
  complexity gate's first run flagged the regression's first `main` at CCN 27, which was
  split into helpers (largest CCN now 9), not waived. `submit_check.py` reports no FAIL.

## Files committed

- `plugins/NeuralAmp/tests/nam_harness.cpp` (both timed loops use `threadCpuTimeUs()`)
- `plugins/NeuralAmp/tests/HarnessClock.h` (new interface)
- `plugins/NeuralAmp/tests/HarnessClockPosix.cpp` (new POSIX implementation)
- `plugins/NeuralAmp/tests/HarnessClockWindows.cpp` (new Win32 implementation)
- `plugins/NeuralAmp/tests/CMakeLists.txt` (per-platform source selection, global property)
- `tests/CMakeLists.txt` (`NeuralAmpWavInputTest` clock source; `NamHarnessWindowsClockCheck`)
- `tests/nam-harness-windows-clock-check.py` (regression; new)
- `tests/fork-sources.txt`, `tests/upstream-modifications.txt` (same-commit registrations)
- `docs/qwen-fixes-2026-10/cluster-ci-443a2931bcb5dacc.md` (this report)
