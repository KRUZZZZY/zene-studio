FIXED

Cluster 5490: ALSA shutdown could hang during suspend recovery.

## Verdict and evidence

REAL. On this branch the capture `-ESTRPIPE` recovery retried `snd_pcm_resume()` while it
returned `-EAGAIN`, one second per retry, with no bound and without reading the capture stop
flag (`src/core/audio/AudioAlsa.cpp:572-575` before the change; the branch is now at :585).
`closeCapture()` sets that flag and then JOINS the capture thread
(`src/core/audio/AudioAlsa.cpp:519` stores `m_captureStop`, `:530` joins), so a device that
stays suspended kept the join waiting for as long as the device answered `-EAGAIN`. The
playback sibling in `handleError()` had the identical unbounded loop (`:253-257` before the
change).

The fix, at the cause:

- `include/AudioAlsaRecovery.h:51-73` — `alsa::detail::resumePcmWithBudget()`: retries
  `snd_pcm_resume()` at most `attempts` times, checks the caller's stop predicate before
  every attempt, and skips the wait after the last attempt. The wait is injected by the
  caller, so the policy is testable without a clock or a suspended device. No allocation,
  lock or log is added (two lambdas and a `sleep_for` the old loop already did).
- `src/core/audio/AudioAlsa.cpp:58-59` — the shared budget: 20 attempts, 100 ms between them
  (2 s), after which the recovery falls back to `snd_pcm_prepare()`, the documented ALSA
  recovery for a device that will not resume.
- `src/core/audio/AudioAlsa.cpp:585-614` — the capture recovery passes
  `m_captureStop.load(acquire)` as the stop predicate. When it is set the recovery returns
  without preparing or parking an error: the capture is closing, there is nothing to
  recover.
- `src/core/audio/AudioAlsa.cpp:262-287` — the playback sibling uses the same helper with
  `!AudioDevice::isRunning()` as the predicate, so stopping playback no longer waits out the
  suspend loop either. Healthy playback (resume succeeds) and a spent budget (prepare
  fallback) are unchanged.

## Test matrix

| Path | What proves it |
| --- | --- |
| Normal path | `AudioAlsaCaptureErrorTest`, `AudioAlsaSuspendShutdownTest` and `AudioDeviceDiagnosticsTest` start and stop the real ALSA "null" device; `AudioAlsaWriteTest`/`AudioAlsaPollDescriptorTest` keep the playback and descriptor paths. All passed (7/7, `test-5-family.log`). |
| Failure (the finding) | `AudioAlsaSuspendShutdownTest::suspendRecoveryStopsWhenTheCaptureCloses`: the fake capture PCM reports `-ESTRPIPE` on its first read and `-EAGAIN` from every `resume()`; `closeCapture()`'s `drop()` marks the shutdown. The test counts `resume()` calls made after the drop: the fixed recovery makes at most one, the old loop made four before the fake's terminal error ended it. |
| Exhaustion / timeout | `AudioAlsaResumeRecoveryTest::givesUpAfterTheBudget`: a resume that never stops answering `-EAGAIN` is attempted exactly `attempts` times, the wait runs `attempts - 1` times (no sleep after the last attempt), and the outcome is `-EAGAIN` so the caller prepares. |
| Other policy paths | `retriesUntilTheDeviceResumes` (success ends the retry), `stopsWhenTheCallerIsStopping` (predicate wins), `checksTheStopBeforeTheFirstAttempt` (zero resume calls), `reportsATerminalErrorWithoutWaiting` (non-`-EAGAIN` error returned at once). |
| Reconfiguration | The fix changes no configuration, project, save or undo data. The start/stop (reconfiguration) path is exactly the one driven by both ALSA tests: `startProcessing()` opens the capture thread, `stopProcessing()` closes and joins it. |
| Audio-thread constraints | The recovery runs on the capture thread (and playback's on the render thread): the fix adds two lambdas, an `int` attempt counter and the same `sleep_for` wait the old `sleep(1)` was. No allocation, free, lock, log, QString or signal; no new atomic beyond the existing `m_captureStop` load. |
| Platform/build | Only the `#ifdef ESTRPIPE` branches and the new header are affected; both tests are registered inside the existing `if(LMMS_HAVE_ALSA)` block of `tests/CMakeLists.txt`, and the configured tree builds them. |

## Commands and results (logs: `/home/kruzzzzy/.config/kads/zene/state/runs/5490.r1/logs`)

| Step | Command | Exit | Result |
| --- | --- | ---: | --- |
| Existing ALSA test (environment check) | `cmake --build build --target AudioAlsaCaptureErrorTest -j4 > .../logs/build-1-existing.log 2>&1` | 0 | — |
| Existing ALSA test run | `cd build/tests && ctest -R '^AudioAlsaCaptureErrorTest$' --output-on-failure > .../logs/test-1-existing.log 2>&1` | 0 | 1/1 passed. |
| Configure attempt (refused) | `cmake -S . -B build > .../logs/configure-1.log 2>&1` | 97 | `bunny-guard: configuring is the parent's job; the lane build tree is already configured.` Not repeated; the next row is the supported way to pick up `tests/CMakeLists.txt`. |
| Regenerate the build system | `cmake --build build --target cmake_check_build_system > .../logs/configure-2.log 2>&1` | 0 | CMake re-ran and generated the build files with the new test targets. |
| RED build attempt | `cmake --build build --target AudioAlsaSuspendShutdownTest -j4 > .../logs/build-3-red.log 2>&1` | 2 | The new test called `isRunning()` unqualified on an `AudioAlsa*` (ambiguous between `QThread` and `AudioDevice`); the test was corrected to read it through `const AudioDevice&`. |
| RED build (unfixed product) | `cmake --build build --target AudioAlsaSuspendShutdownTest -j4 > .../logs/build-4-red.log 2>&1` | 0 | — |
| RED test (unfixed product) | `cd build/tests && ctest -R '^AudioAlsaSuspendShutdownTest$' --output-on-failure > .../logs/test-2-red.log 2>&1` | 8 | `FAIL! ... 'retriesWhileClosing <= 1' returned FALSE. (the suspend recovery retried snd_pcm_resume() 4 times after the capture started closing ...)` |
| GREEN build | `cmake --build build --target AudioAlsaSuspendShutdownTest -j4 > .../logs/build-5-green.log 2>&1` | 0 | — |
| GREEN test | `cd build/tests && ctest -R '^AudioAlsaSuspendShutdownTest$' --output-on-failure > .../logs/test-3-green.log 2>&1` | 0 | 1/1 passed, 0.24 s. |
| Policy-test build | `cmake --build build --target AudioAlsaResumeRecoveryTest -j4 > .../logs/build-6-green-policy.log 2>&1` | 0 | — |
| Both new tests | `cd build/tests && ctest -R '^AudioAlsa(ResumeRecovery\|SuspendShutdown)Test$' --output-on-failure > .../logs/test-4-green-both.log 2>&1` | 0 | 2/2 passed. |
| Rebuild zene + family | `cmake --build build --target zene AudioAlsaPollDescriptorTest AudioAlsaCaptureErrorTest AudioAlsaFatalWriteTest AudioAlsaWriteTest AudioDeviceDiagnosticsTest -j4 > .../logs/build-7-green-zene-family.log 2>&1` | 0 | — |
| Family tests | `cd build/tests && ctest -R '^(AudioAlsa\|AudioDeviceDiagnostics)' --output-on-failure > .../logs/test-5-family.log 2>&1` | 0 | 7/7 passed. |
| Reverse-patch generation | `git add -N include/AudioAlsaRecovery.h src/core/audio/AudioAlsa.cpp && git diff -- <the two product files> > .../logs/fix.patch` | 0 | 172-line patch of the product files only. |
| Apply reverse (all product files) | `git apply -R .../logs/fix.patch` | 0 | Reverted `AudioAlsa.cpp` and deleted the new header. |
| Reverted build attempt | `cmake --build build --target AudioAlsaSuspendShutdownTest -j4 > .../logs/build-8-reversed.log 2>&1` | 2 | AUTOMOC's cached info still names `include/AudioAlsaRecovery.h`: `AutoGen error ... The header file "SRC:/include/AudioAlsaRecovery.h" does not exist.` |
| Regenerate after the deletion | `cmake --build build --target cmake_check_build_system > .../logs/configure-4-reversed.log 2>&1` | 0 | Ran, but the autogen info was not refreshed (empty log); the next build failed the same way. |
| Reverted build, second attempt | `cmake --build build --target AudioAlsaSuspendShutdownTest -j4 > .../logs/build-9-reversed.log 2>&1` | 2 | Same AUTOMOC error. |
| Restore, then reverse only the source | `git apply .../logs/fix.patch` (0), then `git apply -R --exclude=include/AudioAlsaRecovery.h .../logs/fix.patch` (0) | 0 / 0 | The header (a helper with no behaviour of its own) stays so AUTOMOC's glob is satisfied; the reverted product source is what the test drives. |
| Reverted build | `cmake --build build --target AudioAlsaSuspendShutdownTest -j4 > .../logs/build-10-reversed.log 2>&1` | 0 | — |
| Reverted test | `cd build/tests && ctest -R '^AudioAlsaSuspendShutdownTest$' --output-on-failure > .../logs/test-6-reversed.log 2>&1` | 8 | Same `FAIL! ... retried snd_pcm_resume() 4 times` assertion after 4.14 s. |
| Restore patch | `git apply --exclude=include/AudioAlsaRecovery.h .../logs/fix.patch` | 0 | Reverted hunk restored (the header was never removed in the final cycle). |
| Restored build | `cmake --build build --target AudioAlsaSuspendShutdownTest -j4 > .../logs/build-11-restored.log 2>&1` | 0 | — |
| Restored tests | `cd build/tests && ctest -R '^AudioAlsa(SuspendShutdown\|ResumeRecovery)Test$' --output-on-failure > .../logs/test-7-restored.log 2>&1` | 0 | 2/2 passed. |
| Registration: all sources | `bash tests/all-sources-reproduce.sh > .../logs/gate-1-all-sources.log 2>&1` | 0 | `REPRODUCES`. |
| Registration: fork sources | `bash tests/fork-sources-gate.sh > .../logs/gate-2-fork-sources.log 2>&1` | 0 | `PASS: every tracked source in scope is registered`. |
| No tautologies | `bash tests/no-tautology-gate.sh > .../logs/gate-3-no-tautology-default.log 2>&1` | 0 | Both new test files: slots with assertions, no tautology. |
| Namespace check | `tests/scripted/check-namespace > .../logs/gate-4-namespace.log 2>&1` | 0 | 0 errors. |
| Gates | `FEATURE_CHECK_LOG_DIR=.../logs/feature-check-precommit python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gate-5-feature-check-precommit.log 2>&1` | 0 | RESULT: PASS (all eight rows). |
| Gates (committed tree) | `FEATURE_CHECK_LOG_DIR=.../logs/feature-check-final python3 .../feature_check.py <worktree> > .../logs/gate-6-feature-check-final.log 2>&1` | 0 | RESULT: PASS (all eight rows; the committed tree). |
| Completion check | `python3 .../submit_check.py <worktree> 5490 > .../logs/submit-check-2-final.log 2>&1` | 0 | `RESULT: PASS - submit`, working tree clean. |

## Registration

- `tests/fork-sources.txt` and `tests/all-sources.txt` — the new product header
  `include/AudioAlsaRecovery.h` and the two new test sources
  (`AudioAlsaResumeRecoveryTest.cpp`, `AudioAlsaSuspendShutdownTest.cpp`), in sorted place.
- `tests/upstream-modifications.txt` — the reason for the inherited
  `src/core/audio/AudioAlsa.cpp` change.
- `tests/CMakeLists.txt` — both tests registered in the ALSA block.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.**
  Reverted (product source only; the helper header has no behaviour of its own and stays for
  the AUTOMOC glob, see the commands table): rebuilt (`build-10-reversed.log`, exit 0), test
  `test-6-reversed.log`, exit 8, `retried snd_pcm_resume() 4 times after the capture started
  closing`. Restored: rebuilt (`build-11-restored.log`, exit 0), `test-7-restored.log`, exit
  0, 2/2 passed. The RED run fails on the exact defect: retries after `closeCapture()` began.
- **The test is deterministic: no wall-clock timing, sleeps or races.** The seam is the fake
  capture PCM in `tests/src/core/AudioAlsaSuspendShutdownTest.cpp`: `snd_pcm_readi` returns
  `-ESTRPIPE` once, `snd_pcm_resume` counts calls and answers `-EAGAIN`, and
  `snd_pcm_drop` marks the shutdown because that is the call `closeCapture()` makes after it
  sets `m_captureStop`. The assertion is a call count (`resume()` calls after the drop must
  be at most one), never a duration. The fake answers a terminal `-EIO` after three
  post-shutdown retries only so a loop that ignores the flag still ends; the failing count,
  not a timeout, is what fails. The 5 s `tryAcquire` is a liveness bound for the real capture
  thread to reach the recovery, as in `AudioAlsaCaptureErrorTest`.
- **The fix is at the cause and complete: every caller and sibling checked.**
  `handleCaptureError()` has one caller, `captureLoop()`
  (`rg -n handleCaptureError src/core/audio/AudioAlsa.cpp`), which only reacts to `0`; the
  recovery therefore keeps its "0 = handled" contract whether it resumed, gave up to
  `prepare`, or stopped for the shutdown. The sibling is playback's `handleError()`, whose
  `-ESTRPIPE` branch had the same unbounded loop; it now uses the same helper with
  `AudioDevice::isRunning()` as the stop predicate, so both recoveries are bounded and
  shutdown-aware and no other caller of `handleError()` changes.
- **No collateral damage.** The change touches no project, save or undo data and adds no
  limit a caller could exceed; the only new bound is the recovery's retry count, whose
  fallback is the standard `snd_pcm_prepare()`. The normal paths are re-tested by the whole
  `AudioAlsa*`/`AudioDeviceDiagnosticsTest` family (7/7). Nothing is added to the audio
  thread that allocates, frees, locks, logs or waits beyond the sleep the old loop already
  performed (now 100 ms slices instead of a flat second).
- **Verdict is REAL, not ALREADY-FIXED/FALSE-POSITIVE.** The unfixed loop is quoted above and
  its behaviour is reproduced by the RED run, so no code-reference closure applies.
- **Sibling commands.** This commit changes no control-command file: `git show --stat HEAD`
  lists only `include/AudioAlsaRecovery.h`, `src/core/audio/AudioAlsa.cpp`, the two new test
  sources, the three registration lists (`tests/CMakeLists.txt`,
  `tests/fork-sources.txt`, `tests/all-sources.txt`), the divergence ledger and this report.
  The `app.*`/`arrangement.*` siblings the checker lists come from the branch's merge history
  against the gate base `fa4a5f2e7`, not from cluster 5490; no command id, schema, reply or
  `commands_snapshot.json` entry changed.

## Other findings

- `captureLoop()` still discards `handleCaptureError()`'s return value for the
  `snd_pcm_wait`/`snd_pcm_readi` failures (cluster 5489 in
  `docs/bunny-fixes-2026-10-05/engine/batch-22.md`): a permanently failing capture PCM is
  retried forever and capture stays reported open. That is a different defect and a
  different fix; not changed here.
