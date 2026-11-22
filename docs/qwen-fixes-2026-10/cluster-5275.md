FIXED

# Cluster 5275: take writes reported success after a flush/close failure

Round 2: Luna's review of round 1 showed the round-1 detection was inert. This report replaces it.

## Evidence (REAL)
Original defect: `writeWindow()` (`src/core/RetroAudioCapture.cpp`) ignored `sf_write_sync()` and `sf_close()` and
returned success from the `sf_writef_float()` count alone.

Round 1 "fixed" it with `sf_error()` after `sf_write_sync()` and `sf_close() != 0`. Neither can fire:
`sf_write_sync()` returns void and libsndfile's `psf_fsync` discards the fsync result, and the file is opened with
`sf_open_fd(..., SF_FALSE)` so `sf_close()` does not close the descriptor and returns 0 (per Luna's probe; not
re-derived from libsndfile sources in this session, but confirmed by the LD_PRELOAD runs below: the old checks
passed a failing fsync, see round 1 review; the new check fails it).

## Fix (src/core/RetroAudioCapture.cpp)
- `flushDescriptor(fd)`: explicit `::fsync` (POSIX) / `_commit` (Windows, `<io.h>`) on the take's `QSaveFile`
  descriptor; non-zero is a failure.
- `finishSoundFile()`: `sf_write_sync`, `sf_close` (header rewrite), then `flushDescriptor`; returns false on failure.
- `writeWindow()`: a short write still reports first; then a failed flush calls `cancelWriting()` and returns false with
  `could not flush the written take ...: <strerror>`; nothing replaces the destination. The `sf_error`/`sf_close` checks are gone.
- Close failure signal: the `QSaveFile::commit()` result (existing check, unchanged), routed through `commitTake()`.
- Test seam `setRetroTakeFinishTestHook()` is now under `#ifdef LMMS_TESTING` like the push-pause hook. For the Flush step the
  hook's return value REPLACES the real flush result (the real fsync is skipped); for Commit a non-zero return fails the commit.

## Regression test
`tests/src/core/RetroAudioCaptureTest.cpp` `aFailedFlushOrCloseIsReportedAndNothingIsCommitted`, rows `flush` and `commit`:
write returns false, error non-empty, written==0, destination not created; the same call with the hook cleared succeeds.

## Commands (logs in /home/kruzzzzy/.config/kads/zene/state/runs/5275.r2/logs)
Order: seam + test committed first with the flush result ignored (RED), then the check (GREEN).
- RED: build-1.log EXIT=0; `cd build/tests && ctest -R '^RetroAudioCaptureTest$' --output-on-failure` (test-1-red.log) EXIT=8.
- GREEN: build-2.log EXIT=0; test-2-green.log EXIT=0.
- 4b: `git diff -- src/core/RetroAudioCapture.cpp include/RetroAudioCapture.h > logs/fix.patch && git apply -R logs/fix.patch` REVERT=0;
  build-4.log EXIT=0; ctest (test-4-revert.log) EXIT=8; `git apply logs/fix.patch` REAPPLY=0; build-5.log (RetroAudioCaptureTest + zene) EXIT=0;
  ctest (test-5-reapplied.log) EXIT=0. build-3.log (test + zene with fix) EXIT=0, test-3-green.log EXIT=0.
- Real failing fsync (LD_PRELOAD `libfailfsync.so`, source failfsync.c, makes fsync/fdatasync return -1/EIO) on the unmodified
  `theTakeWriterWritesTheRetainedWindow`: with the fix EXIT=1 ("could not flush the written take ...: Input/output error",
  preload-fix.log); with the fix reverted EXIT=0, i.e. success reported (preload-nofix.log).
- Gate: feature_check.py (feature-check.txt) EXIT=0, all PASS.

## Honest limits
- Only the `flush` row goes RED without the fix (test-4-revert.log: 15 passed, 1 failed). The `commit` row passes with and without it,
  because the commit-failure check pre-dates this work; the row guards that behaviour and the "destination untouched" contract.
  The review asked for both rows red; I could not make the commit row red without inventing a new commit check, so it stays a guard.
- fsync failure is injected (hook, and LD_PRELOAD); no real full disk was exercised.
- Windows `_commit` path is not compiled here.

## Self-check
- Fails without / passes with: flush row RED EXIT=8 (test-1-red.log, test-4-revert.log), GREEN EXIT=0 (test-2/3/5); plus the LD_PRELOAD run on an unmodified test (EXIT=1 with fix, 0 without), which exercises the real fsync call.
- Deterministic: no timing, sleeps or threads. Seam: `setRetroTakeFinishTestHook()` supplies the flush/commit result; QTemporaryDir.
- Complete: `writeWindow()` is reached only through `writeRetroAudioTake()`; its callers `src/core/ControlCommandsRecordingRetro.cpp:182`, `ControlCommandsSessionAudioRecord.cpp:293`, `ControlCommandsSamplerTake.cpp:331` already test the bool and `error`, unedited.
- No collateral damage: healthy writes unchanged (the other 14 tests in the file pass); no data is loaded; one extra fsync per take, off the audio thread (command handler). No new limits.
- Verdict FIXED, so no ALREADY-FIXED/FALSE-POSITIVE reference needed.
- Sibling commands: I edited no command files and added no control command.
- Realtime: nothing on the audio thread changed.

## Other findings
- Other libsndfile writers (e.g. `src/core/ClipConsolidate.cpp`, which uses `sf_close` only) were not audited for the same fsync-ignored pattern.
