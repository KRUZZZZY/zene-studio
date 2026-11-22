FIXED

# Cluster 5275: take writes reported success after a flush/close failure

## Evidence (REAL)
Before the fix `writeWindow()` in `src/core/RetroAudioCapture.cpp` called
`sf_write_sync(sf); sf_close(sf);` and discarded both results, then returned success
from the `sf_writef_float()` count alone (old lines ~314-322, now `finishSoundFile()` and
the check at `src/core/RetroAudioCapture.cpp:~335-352`). Note `sf_write_sync()` returns
`void` (/usr/include/sndfile.h:770), so "check its return value" is done via the file's sticky
`sf_error(sf)`; `sf_close()` returns int (sndfile.h:762).

## Fix
`finishSoundFile(SNDFILE*)` flushes, reads `sf_error()`, always closes, and reports which step
failed; `writeWindow()` returns false with an error (and never calls `QSaveFile::commit()`, so
the destination is not replaced) when it fails. A short write is still reported first, as before.
Only the take writer changes; the one caller is `writeRetroAudioTake()`. No audio-thread code touched.

## Test seam
A healthy disk cannot be made to fail a flush deterministically, so
`setRetroTakeFinishTestHook()` (include/RetroAudioCapture.h) lets a test force a step to fail
after the real step ran (same atomic-function-pointer style as `setPushPauseTestHook`). Null by default.

## Regression test
`tests/src/core/RetroAudioCaptureTest.cpp`: `aFailedFlushOrCloseIsReportedAndNothingIsCommitted`
(rows `flush`, `close`): write must return false, error non-empty, frames written 0,
destination not created; the same call with the hook cleared succeeds.

## Commands (logs in /home/kruzzzzy/.config/kads/zene/state/runs/5275.r1/logs)
Order note: I wrote the seam and fix together first, then split them: the seam + test were
committed with the fix block removed, which gave the RED run below.
- RED (seam + test, no fix): `cmake --build build --target RetroAudioCaptureTest -j4` (build-1.log) EXIT=0;
  `cd build/tests && ctest -R RetroAudioCaptureTest --output-on-failure` (test-1-red.log) EXIT=8
  (2 failed: flush and close rows, "a failed flush/close must not report success").
- GREEN (fix applied): build-2.log EXIT=0; `ctest -R RetroAudio` (test-2-green.log) EXIT=0, RetroAudioCaptureTest passed (16 tests).
- 4b: `git diff -- src/core/RetroAudioCapture.cpp include/RetroAudioCapture.h > logs/fix.patch && git apply -R logs/fix.patch`
  REVERT=0; rebuild (build-3.log) EXIT=0; ctest (test-3-revert.log) EXIT=8 (same 2 failures);
  `git apply logs/fix.patch` REAPPLY=0; rebuild (build-4.log) EXIT=0; ctest (test-4-reapplied.log) EXIT=0.
- `cmake --build build --target zene -j4` (build-5-zene.log) EXIT=0 (header changed).
- Gate: `python3 .../feature_check.py <worktree>` (feature-check.txt) EXIT=0, all PASS.

## Not verified
No real full-disk/failing-device run (not reproducible headless); the failure is injected via the hook.
The `ctest -R RetroAudio` family matched only RetroAudioCaptureTest. Sibling writers
(`ClipConsolidate.cpp:126` already checks `sf_close`) were not changed.

## Other findings
- `src/core/ClipConsolidate.cpp` and other `sf_write_sync`-less writers were not audited for sticky-error handling.
