FIXED

# Cluster 5275: take writes reported success after a flush/close failure

5275: FIXED

## Evidence (REAL)
Pre-cluster `writeWindow()` (`git show 1d9f160c89^:src/core/RetroAudioCapture.cpp`, lines 314-315) did
`sf_write_sync(sf); sf_close(sf);` and ignored both. Success then came only from `sf_writef_float()`'s count.
`sf_write_sync()` returns void, and `sf_close()` on a descriptor opened with `sf_open_fd(..., SF_FALSE)` returns 0
whatever happened. So a header write that failed, or a durable flush that failed, still committed the take and
returned `true`.

## Fix (round 5: rewritten from the pre-cluster files at the reviewer's checkpoint, 35 product lines)
`git diff --numstat 1d9f160c89^ -- src/core/RetroAudioCapture.cpp include/RetroAudioCapture.h` gives `29 2` and `6 0`.
- `src/core/RetroAudioCapture.cpp` `writeWindow()`: `sf_write_sync` is removed. The code then runs, in order:
  - `sf_command(sf, SFC_UPDATE_HEADER_NOW, nullptr, 0)`
  - `finished = sf_error(sf) == SF_ERR_NO_ERROR`, then `finished = (sf_close(sf) == 0) && finished`
  - only if finished, `fsync(output.handle())`. On Windows this is `_commit`, from the `#ifdef _WIN32` include block.

  The existing short-write check comes next, unchanged. If anything failed, `output.cancelWriting()` runs, `*error` is set
  to "could not finalize/flush the take <path>", and the function returns false. Nothing is committed.
- **`sf_error()` after `SFC_UPDATE_HEADER_NOW` is the check that catches a failed header write.** libsndfile records the
  system error there, and `sf_close()` cannot report it.
- Test seam, `LMMS_TESTING` only (`include/RetroAudioCapture.h`): `enum class RetroTakeFinishStep { BeforeFinalize, Flush }`
  and `setRetroTakeFinishTestHook()`.
  - The hook is a plain static pointer, set only from the single test thread while no write is running.
  - It is called at BeforeFinalize with its result ignored.
  - At Flush, its result replaces the fsync result.
- **Removed on purpose to meet the 40-line limit:** the round-3/4 read-back of the finished header and the Commit-step
  guard. The commit row and `failCommit` were deleted from the test with them, because the Commit step no longer exists.
  `QSaveFile::commit()` failure is still reported by the existing check.

## Regression test
`tests/src/core/RetroAudioCaptureTest.cpp` `aFailedFlushOrCloseIsReportedAndNothingIsCommitted` has no `QSKIP` on any
platform. Every row asserts:
- `!ok`, a non-empty error, `written == 0` and `!QFileInfo::exists(path)`;
- then, with the hook cleared, the same call succeeds with 64 frames.

The rows:
- `finalize`: on Linux, the real EBADF regression. At BeforeFinalize the hook does `open("/proc/self/fd/<fd>", O_RDONLY)`
  and `dup2`s it over the take fd. Off Linux the row uses the portable stand-in below.
- `finalize-portable` (every platform): a valid 64-frame `stand-in.wav` is written first. At BeforeFinalize the hook
  `swapInReadOnlyStandIn` `dup2`s a read-only descriptor of it over the take fd (`_wopen`/`_dup2` on Windows). The real
  header rewrite then fails with EBADF and `sf_error()` sees it. If the check is ignored, the take commits and the row
  fails.
- `flush`: the hook replaces the fsync result with -1.

## Commands and EXIT codes (logs in /home/kruzzzzy/.config/kads/zene/state/runs/5275.r5/logs)
`B` stands for `cmake --build build --target zene RetroAudioCaptureTest -j4`. ctest runs from build/tests.

The mutants:
- `mutant-sferror.diff` replaces `bool finished = sf_error(sf) == SF_ERR_NO_ERROR;` with `bool finished = true;`. This is
  the sf_error product check reversed.
- `mutant-nofix.diff` replaces `if (flushed != 0)` with `if (false && flushed != 0)`. Every finish failure is ignored, as
  before the fix.

| # | Command | EXIT | Result |
|---|---|---|---|
| 1 | `B > build-1.log` | 0 | |
| 2 | `ctest -R RetroAudioCaptureTest -V > test-1-green.log` | 0 | Totals: 17 passed, 0 failed, 0 skipped. finalize, finalize-portable and flush PASS. |
| 3 | `git apply mutant-sferror.diff`; `B > build-2-sferror.log` | 0, 0 | |
| 4 | `ctest -R RetroAudioCaptureTest --output-on-failure > test-2-red-sferror.log` | **8** | FAIL! `(finalize)` and `(finalize-portable)` `'!ok' returned FALSE`. 15 passed, 2 failed. |
| 5 | `git apply -R mutant-sferror.diff` | 0 | |
| 6 | `git apply mutant-nofix.diff`; `B > build-3-nofix.log` | 0, 0 | |
| 7 | `ctest ... > test-3-red-nofix.log` | **8** | FAIL! `(finalize)`, `(finalize-portable)` and `(flush)`. 14 passed, 3 failed. |
| 8 | `git apply -R mutant-nofix.diff`; `B > build-4.log` | 0, 0 | |
| 9 | `ctest -R RetroAudioCaptureTest -V > test-4-green.log` | 0 | Totals: 17 passed, 0 failed, 0 skipped. |
| 10 | `python3 .../feature_check.py <worktree> > gates-1.log` | 0 | No FAIL rows. |

A literal `git apply -R fix.patch` against the pre-cluster code cannot be built: the test needs the seam, which is part of
the fix. The two mutants above take its place. They keep the seam and remove exactly the checks.

## Honest limits
- The failures are injected by swapping in a read-only descriptor: a real EBADF from the OS, but not a full disk.
- The Windows `_wopen`/`_dup2`/`_commit` branches are not compiled on this machine.
- `writeWindow()` runs on the command-handler thread, never the audio thread. It adds one header rewrite and one fsync per
  take.

## Self-check
- **Fails without the fix, passes with it:** test-2 and test-3 are RED (EXIT=8); test-1 and test-4 are GREEN (EXIT=0).
  The reversed sf_error check fails both finalize rows, as the reviewer asked.
- **Deterministic:** no timing, sleeps or threads in this test. Faults come in through the `LMMS_TESTING` seam
  `setRetroTakeFinishTestHook`, plus `dup2` and a pre-written stand-in file in a `QTemporaryDir`.
- **Seam thread safety:** the seam is a plain pointer. It is written only from the test thread while no write is running.
  `concurrentTakesToOnePathRemainIndependent` only reads it, as null, so there is no data race. This is reasoned from the
  code; TSan is not run on this box.
- **Complete:** `writeWindow()` is reached only through `writeRetroAudioTake()`. Its callers check the bool and the
  error, and they are unchanged:
  - `src/core/ControlCommandsRecordingRetro.cpp:182`
  - `src/core/ControlCommandsSessionAudioRecord.cpp:293`
  - `src/core/ControlCommandsSamplerTake.cpp:331`
- **No collateral damage:** healthy takes are unchanged (all 17 cases green, including the bit-exact round-trip and
  concurrent-writer tests). No load path is touched and no limit is added.
- **Realtime and commands:** nothing on the audio thread changed. No control command was added.
- **Registration:** `include/RetroAudioCapture.h` (fork-sources.txt:1344) and `src/core/RetroAudioCapture.cpp` (:2193)
  are already fork sources.
- **ALREADY-FIXED / FALSE-POSITIVE reference:** not applicable. The verdict is FIXED.
- **Sibling commands (app.*, arrangement.* in the check's list):** no command file is changed. The diff touches only
  RetroAudioCapture.{h,cpp}, its test and this report. Those commands do not reach `writeRetroAudioTake()`.

Delegation:
- Haiku ran every build and the green / two-mutant red / green ctest matrix, and reported the exit codes and the FAIL!
  rows. I re-checked them in the logs.
- No Sonnet was used. The product fix (35 lines, prescribed by the checkpoint) and the test edits (removing one row and
  one hook) were smaller than writing a request.
- I wrote the code, the mutants, this report and the manifest.

## Other findings
- Other libsndfile writers, for example `src/core/ClipConsolidate.cpp`, which only checks `sf_close`, were not audited for
  the same ignored header-write failure.
