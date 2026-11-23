FIXED

# Cluster 5275-port — Audio take writes can report success after a flush or close failure

5275-port: FIXED

## Verdict: REAL

Before the fix, the retrospective audio take writer `writeWindow()` (`src/core/RetroAudioCapture.cpp`)
called `sf_write_sync(sf); sf_close(sf);` and threw away both results. `sf_write_sync()` returns
void, so a failure there only shows up in `sf_error()`, and `sf_close()` returns an int that was
never read. The function then checked only the count that `sf_writef_float()` returned, called
`QSaveFile::commit()` and returned `true`. So a take whose header rewrite, flush or close failed
(a full disk, a failing device) was reported as written and replaced the destination file. The
earlier lane (`docs/bunny-fixes-2026-10-05/core/batch-28.md`, "Cluster 5275") confirmed the
defect but left it unfixed because it had no way to inject the fault.

## Fix

- `src/core/RetroAudioCapture.cpp:278` `finishEncoding()` flushes the file, reads `sf_error()`,
  closes the file, and returns the libsndfile error code of the first step that failed.
- `src/core/RetroAudioCapture.cpp:334` `writeWindow()` keeps that code. After the existing
  short-write check, `src/core/RetroAudioCapture.cpp:345` refuses the take before `commit()` and
  reports `could not finish writing <path>: <sf_error_number>`. `QSaveFile` throws away its
  temporary file, so the destination keeps its old contents and `framesWritten` stays 0.
- Test seam: `retroaudiotesting::failNextTakeFinish()`, inside `#ifdef LMMS_TESTING` only
  (`include/RetroAudioCapture.h:209`, `src/core/RetroAudioCapture.cpp:271` and its definition after
  the anonymous namespace). It makes the next finish report `SF_ERR_SYSTEM`. A real flush or close
  failure cannot be forced headlessly after every frame was accepted (see batch-28), so the test
  uses this seam.
- I committed the seam and the test first (32bb837d5d, behaviour-neutral: the result is still
  ignored there), so that `fix.patch` holds only the check.

Callers and siblings: `writeWindow()` has one caller, `writeRetroAudioTake()`, which already
passes its `false` and error text through. Successful writes behave exactly as before. Audio
thread: nothing changes. This is a command-handler path, and it adds no control command and no
undo step.

## Regression test

`tests/src/core/RetroAudioCaptureTest.cpp`,
`RetroAudioCaptureTest::aFailedFlushOrCloseIsReportedAndKeepsTheOldTake`. The test:
- arms the seam over an existing take file;
- checks that the write returns false with an error and `written == 0`;
- checks that the old file's bytes are unchanged;
- checks that the next write, with the seam no longer armed, succeeds with 64 frames.

It uses no timing and no sleeps.

## Commands (logs in /home/kruzzzzy/.config/kads/zene/state/runs/5275-port.r6/logs)

| # | Command | EXIT |
|---|---|---|
| 01 | `cmake --build build --target zene RetroAudioCaptureTest -j4 > 01-build-red.log 2>&1` (seam + test, no fix) | 0 |
| 02 | `cd build/tests && ctest -R '^RetroAudioCaptureTest$' --output-on-failure > 02-test-red.log 2>&1` | 8 (`FAIL! ... aFailedFlushOrCloseIsReportedAndKeepsTheOldTake() '!ok' returned FALSE`, 14 passed 1 failed) |
| 03 | `cmake --build build --target zene RetroAudioCaptureTest -j4 > 03-build-green.log 2>&1` | 0 |
| 04 | `cd build/tests && ctest -R 'RetroAudio' --output-on-failure > 04-test-green.log 2>&1` | 0 (100% tests passed out of 1) |
| 4b | `git diff -- src/core/RetroAudioCapture.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 05 | rebuild, `05-build-4b-red.log` | 0 |
| 06 | `ctest -R '^RetroAudioCaptureTest$' --output-on-failure > 06-test-4b-red.log 2>&1` | 8 (same FAIL line, 14 passed 1 failed) |
| 4b | `git apply logs/fix.patch` | 0 |
| 07 | rebuild, `07-build-4b-green.log` | 0 |
| 08 | `ctest -R '^RetroAudioCaptureTest$' --output-on-failure > 08-test-4b-green.log 2>&1` | 0 (100% tests passed out of 1) |
| 09 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > 09-feature-check.log 2>&1` | 0 (every row PASS, `RESULT: PASS`) |

The `ctest -R 'RetroAudio'` family matches one test, `RetroAudioCaptureTest`.

Registration: `src/core/RetroAudioCapture.cpp` and `include/RetroAudioCapture.h` are already fork
sources (`tests/fork-sources.txt:1344`, `:2193`), so no list change is needed. The fork-sources
gate passed.

Delegation:
- Sonnet added the seam and wrote the regression test to my design, then built and ran the RED run (logs 01-02).
- Haiku ran the green build, the 4b revert and re-apply cycle, and the feature check (logs 03-09).
- I diagnosed the bug, designed the fix, wrote the 9-line check, reviewed the diffs, and wrote the commits and this report.

## Self-check

The answers to the completion check's "?" items are added below after it ran.

## Other findings

Not fixed here, because they are outside this cluster:
- `TrackRecorder::disarm()` (`src/core/audio/TrackRecorder.cpp`, the `sf_write_sync(m_sf); sf_close(m_sf);`
  block after the writer join) also ignores flush and close failures. Its `bool` return currently
  means "journal retired". Fixing it is a contract decision: arguably a failed close should keep
  the recovery journal.
- `TakeRecorder::stop()` (`src/core/TakeRecorder.cpp`, `sf_close(m_file)`) is `void` and ignores
  `sf_close()`.
- Cluster 5276 (rename after remove in `writeWindow()`) no longer applies in that form: the
  writer now uses `QSaveFile` to replace the destination atomically.
