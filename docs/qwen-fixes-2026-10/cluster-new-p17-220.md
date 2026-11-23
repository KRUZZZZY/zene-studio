FIXED

Cluster new-p17-220: ALSA playback keeps running after an unrecoverable write error.

## Verdict and evidence

REAL. On this branch `AudioAlsa::run()` rendered a period, called `handleError()` on a
negative write result and then unconditionally `continue`d, discarding what the recovery
returned (`src/core/audio/AudioAlsa.cpp:311-313` before the change; `run()` is now at
:302). `handleError()` returns 0 for a recovered underrun/suspend and the original
negative code for anything it does not handle, so a removed device (`snd_pcm_writei` =
`-ENODEV`) was retried forever while the device's running flag stayed set. `handleError()`
also returned 0 when `snd_pcm_prepare()` itself failed for `-EPIPE`/`-ESTRPIPE`, so even a
failed recovery looked like success to the loop.

The fix, at the cause:

- `src/core/audio/AudioAlsa.cpp:317-324` — `run()` now tests `handleError()`'s result. A
  negative result records the errno and clears the running flag through
  `stopAfterBackendFailure(-err)`, then `break`s out of the loop. A recovered error (0)
  keeps the old behaviour.
- `src/core/audio/AudioAlsa.cpp:235-268` — `handleError()` now returns the failing prepare
  code instead of 0 for both recoveries (underrun at :246, suspend at :266), so its
  "0 = recovered" contract is honest and the loop sees a failed recovery.
- `include/AudioDevice.h:130` — `backendFailureCode()` (the positive errno, 0 while none),
  filled by the protected `stopAfterBackendFailure()` at :166-171 (one relaxed store, then
  the running flag's release clear; no allocation, lock, wait or log).
- `src/core/audio/AudioDevice.cpp:132` — `startProcessing()` clears the previous run's
  failure, so a restart is not reported with a stale ENODEV.
- `src/core/AudioDeviceDiagnostics.cpp:95-104,128` — `mixer.cpu_load`'s `audio_device` now
  carries `failure: {available, value (errno), unit: "errno", message}` plus the existing
  `running:false`, which is the device status the CPU diagnostics window and agents read.

## Test matrix

| Path | What proves it |
| --- | --- |
| Normal playback | `AudioAlsaWriteTest` (short-write helper) and `AudioDeviceDiagnosticsTest` (real null-PCM start/timing/xruns), both still pass. |
| Fatal failure | `AudioAlsaFatalWriteTest::fatalWriteErrorStopsPlaybackAndIsPublished`: first write returns `-ENODEV`; the loop must stop after exactly one write and publish ENODEV. |
| Recovered failure | `handleError(-EPIPE)` returns 0 with a successful prepare (`AudioDeviceDiagnosticsTest::alsaReportsWhatThePcmGrantedAndCountsUnderruns`); `AudioAlsaWriteTest` covers a completed period and the zero-progress/overlong `-EIO` path. |
| Exhaustion | `AudioAlsaWriteTest::completesPeriodAfterShortWrites` (the writer keeps going until the period is full; a stuck writer becomes `-EIO`, which the new `run()` path treats as fatal). |
| Reconfiguration | The test installs the ALSA device in the engine, stops it on the failure and calls `restoreAudioDevice()`; `AudioDevice::startProcessing()` resets the failure code. |
| Audio-thread constraints | The new audio-thread work is one relaxed atomic store and one release `m_running.clear()`; no allocation, free, lock, wait, signal or log. The fake write is called on the real audio thread and no printf is added by the fix. |
| Platform/build | The test lives in the existing `if(LMMS_HAVE_ALSA)` block of `tests/CMakeLists.txt`; it builds and passes with the ALSA backend enabled (the configured tree). |

## Commands and results (logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p17-220.r1/logs`)

| Step | Command | Exit | Result |
| --- | --- | ---: | --- |
| RED build (unfixed product code) | `cmake --build build --target AudioAlsaFatalWriteTest -j4 > .../logs/build-6-red.log 2>&1` | 0 | Test compiled against the unfixed tree. |
| RED test | `cd build/tests && ctest -R '^AudioAlsaFatalWriteTest$' --output-on-failure > .../logs/test-2-red.log 2>&1` | 8 | `FAIL! ... 'stopped' returned FALSE. (the loop kept running after a write error it could not recover from)`; 2 passed, 1 failed. |
| GREEN build (product fix) | `cmake --build build --target zene -j4 > .../logs/build-8-green-zene.log 2>&1` | 0 | Rebuilt every target including the changed `AudioDevice.h`. |
| GREEN test build | `cmake --build build --target AudioAlsaFatalWriteTest -j4 > .../logs/build-10-green-test.log 2>&1` | 0 | — |
| GREEN test | `cd build/tests && ctest -R '^AudioAlsaFatalWriteTest$' --output-on-failure > .../logs/test-4-green.log 2>&1` | 0 | 1/1 passed. |
| Family (build the rest, then all six) | `cmake --build build --target AudioDeviceDiagnosticsTest CpuDiagnosticsDialogTest AudioAlsaPollDescriptorTest AudioAlsaCaptureErrorTest -j4 > .../logs/build-11-family.log 2>&1` and `cmake --build build --target AudioAlsaWriteTest -j4 > .../logs/build-12-family.log 2>&1`; then `cd build/tests && ctest -R '^(AudioAlsa\|AudioDeviceDiagnostics\|CpuDiagnosticsDialog)' --output-on-failure > .../logs/test-7-family.log 2>&1` | 0 / 0 / 0 | 6/6 passed. |
| Reverse patch | `git diff -- include/AudioDevice.h src/core/audio/AudioDevice.cpp src/core/audio/AudioAlsa.cpp include/AudioDeviceDiagnostics.h src/core/AudioDeviceDiagnostics.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` | 0 / 0 | Only the product hunks removed; the test and its registration stayed. |
| Reverted rebuild | `cmake --build build --target zene AudioAlsaFatalWriteTest -j4 > .../logs/build-13-reversed.log 2>&1` | 0 | — |
| Reverted test | `cd build/tests && ctest -R '^AudioAlsaFatalWriteTest$' --output-on-failure > .../logs/test-8-reversed.log 2>&1` | 8 | Failed again with the same `stopped` assertion. |
| Restore patch | `git apply .../logs/fix.patch` | 0 | Product fix restored. |
| Restore rebuild | `cmake --build build --target zene AudioAlsaFatalWriteTest -j4 > .../logs/build-14-restored.log 2>&1` | 0 | — |
| Restore test | `cd build/tests && ctest -R '^AudioAlsaFatalWriteTest$' --output-on-failure > .../logs/test-9-restored.log 2>&1` | 0 | 1/1 passed. |
| Final family | `cmake --build build --target zene AudioAlsaFatalWriteTest AudioDeviceDiagnosticsTest CpuDiagnosticsDialogTest AudioAlsaWriteTest AudioAlsaPollDescriptorTest AudioAlsaCaptureErrorTest -j4 > .../logs/build-15-final.log 2>&1` and the ctest above into `.../logs/test-10-final-family.log` | 0 / 0 | 6/6 passed. |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p17-220 > .../logs/gate-1-feature-check.log 2>&1` | 0 | RESULT: PASS (all eight rows). |

## Registration

- `tests/fork-sources.txt` and `tests/all-sources.txt` — new test source
  `tests/src/core/AudioAlsaFatalWriteTest.cpp` (sorted after `AudioAlsaCaptureErrorTest.cpp`).
- `tests/upstream-modifications.txt` — reasons for the five inherited product files and
  the test registration change.
- `tests/CMakeLists.txt` — registers the test in the ALSA block.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it
  (exit codes).** Reverted: `test-8-reversed.log`, ctest exit 8,
  `'stopped' returned FALSE`. Restored: `test-9-restored.log`, ctest exit 0, 1/1 passed.
  The RED run's failure is the exact defect: the loop wrote again after `-ENODEV`.
- **The test is deterministic: no wall-clock timing, sleeps or races.** The seam is the
  fake `snd_pcm_writei` at `tests/src/core/AudioAlsaFatalWriteTest.cpp:68`: it injects
  `-ENODEV` on the first write and counts every later call. The assertion is
  `calls == 1` plus `AudioDevice::isRunning() == false`. The 5 s `QTest::qWaitFor` is only
  a liveness bound for the stop; the buggy loop retries immediately and deterministically
  (proved by the reversed run). No timestamps or sleeps are compared.
- **The fix is at the cause and complete: every caller and sibling checked.** The only
  caller of `handleError()` is `run()` (`rg -n handleError src/core/audio/AudioAlsa.cpp`);
  both of `handleError()`'s recovery branches now report a failed prepare instead of
  swallowing it. The capture sibling, `handleCaptureError()`, keeps its existing contract
  (park the failure, report it from the closing thread) by design from cluster 4584; it is
  a separate thread whose failure must not stop playback, and its loop already behaves
  that way. No other backend calls this function.
- **No collateral damage.** `AudioAlsaWriteTest`, `AudioDeviceDiagnosticsTest` (including
  its `handleError(-EPIPE) == 0` checks), `CpuDiagnosticsDialogTest`,
  `AudioAlsaPollDescriptorTest` and `AudioAlsaCaptureErrorTest` all still pass. The new
  `failure` field is additive to `mixer.cpu_load`; no existing reading changed, and a
  device with no failure reports `available:false` rather than a zero. No project, save or
  undo data is touched.
- **Verdict is REAL, not ALREADY-FIXED/FALSE-POSITIVE.** The unfixed line is quoted above
  and the reverted run reproduces the infinite retry. (The "ALREADY-FIXED/FALSE-POSITIVE
  code evidence" ask is therefore not applicable: this is a FIXED verdict.)
- **Sibling commands.** This fix changes no control command file: the commit
  (`4709129ec6`) touches only `include/AudioDevice.h`, `include/AudioDeviceDiagnostics.h`,
  `src/core/AudioDeviceDiagnostics.cpp`, `src/core/audio/AudioAlsa.cpp`,
  `src/core/audio/AudioDevice.cpp`, the three test-registration lists,
  `tests/CMakeLists.txt`, `tests/src/core/AudioAlsaFatalWriteTest.cpp` and this report. The
  `app.*`/`arrangement.*` siblings the checker lists come from the branch's merge history
  against the gate base (`fa4a5f2e7`), not from this cluster's commit, and no command id,
  schema or reply other than the additive `audio_device.failure` reading changed.

## Other findings

- `AudioOss::run()` (`src/core/audio/AudioOss.cpp:225-249`) breaks out of its loop on a
  failed write but leaves the device marked running and publishes nothing — the same class
  of defect on another backend, outside this finding; not changed here.
- `handleCaptureError()` returns 0 after a failed capture recovery by design (the error is
  parked in an atomic and reported when the capture closes, cluster 4584); left as is.
