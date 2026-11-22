FIXED

# Cluster 5275: take writes reported success after a flush/close failure

5275: FIXED

Round 3. This round replaces the round-2 report. Review found that the round-2 regression only exercised a hook-forced
branch, and that a failure in the real header finalize or close went undetected.

## Evidence (REAL)
`writeWindow()` (`src/core/RetroAudioCapture.cpp`) opens the take with `sf_open_fd(output.handle(), SFM_WRITE, &info, SF_FALSE)`.
It then used `sf_write_sync()` (which returns void) and `sf_close()`. With `SF_FALSE`, `sf_close()` returns 0 whatever
happened, so a failed header rewrite (a full or failing disk) still committed the take and returned `true`. The proof
for this is the RED run below. With the round-2 code (HEAD before this round), a descriptor whose writes really fail with
EBADF still gives `ok == true` (test-10-rows.log).

## Fix (src/core/RetroAudioCapture.cpp, include/RetroAudioCapture.h)
- `finalizeHeader()` (RetroAudioCapture.cpp:312) does the following while the handle is open:
  - It calls `sf_command(sf, SFC_UPDATE_HEADER_NOW, nullptr, 0)`.
  - It checks `sf_error(sf) == SF_ERR_NO_ERROR`. libsndfile records the failed write there.
  - Then it calls `sf_close`.
  - `sf_write_sync` is dropped.
- `readBackMatches()` (:328) is an independent check of the finished file:
  - It runs `lseek(fd, 0)` (`_lseek` on Windows), then `sf_open_fd(fd, SFM_READ, ..., SF_FALSE)`.
  - It requires `frames == window.size()`, `channels == 2` and `samplerate == sampleRate`.
- `finishSoundFile()` (:346) works in this order:
  - It runs the finalize step, then the read-back. If either fails, it returns "could not finalize the take header".
  - It then runs the existing `fsync`/`_commit` check (`flushDescriptor`, unchanged from round 2). If that fails, it
    returns "could not flush the written take: <strerror>".
- `writeWindow()`: when the finish step returns an error:
  - it calls `cancelWriting()`, sets a non-empty error and returns false;
  - `written` stays 0, and the destination is not created or replaced.
  - A short `sf_writef_float` still reports first, and now closes the handle itself.
- Close-failure signal: the `QSaveFile::commit()` result (`commitTake`), which is unchanged.
- Seam (`LMMS_TESTING` only, as before): a new `RetroTakeFinishStep::BeforeFinalize` is called with the take fd just before
  the header rewrite. Its return value is ignored, so the hook supplies no result. `Flush` and `Commit` behave as in round 2.

## Regression test
`tests/src/core/RetroAudioCaptureTest.cpp` `aFailedFlushOrCloseIsReportedAndNothingIsCommitted`. Every row checks:
`ok == false`, a non-empty error, `written == 0` and `!QFileInfo::exists(path)`. Then the same call with the hook cleared
succeeds with `written == 64`.
- `finalize` (new): at `BeforeFinalize` the hook does `open("/proc/self/fd/<fd>", O_RDONLY)`, `dup2` over the take fd
  and `close`. From then on every header write really fails with EBADF, as on a failing disk. The hook returns 0 for the
  Flush step, so only the finalize detection can catch the failure. The row calls `QSKIP` off Linux.
- `flush`: the hook replaces the fsync result with -1 (round 2).
- `commit`: kept as a guard only. It was already green before this cluster (round-2 report), and it stays green in
  every variant below.

## Commands and EXIT codes (logs in /home/kruzzzzy/.config/kads/zene/state/runs/5275.r3/logs)
Build = `cmake --build build --target zene RetroAudioCaptureTest -j4`. Test = `cd build/tests && ctest -R '^RetroAudioCaptureTest$' --output-on-failure`,
plus `./RetroAudioCaptureTest aFailedFlushOrCloseIsReportedAndNothingIsCommitted` for the per-row results.

| step | product code | build | ctest | finalize | flush | commit |
|---|---|---|---|---|---|---|
| 1 / 2 | fix (green) | 0 | 0 | PASS | PASS | PASS |
| 10 | `git apply -R logs/fix.patch` (round-2 code) | 0 | **8** | **FAIL** | PASS | PASS |
| 11 | `git apply logs/fix.patch` | 0 | 0 | PASS | PASS | PASS |
| 12 | sf_error check AND read-back removed | 0 | **8** | **FAIL** | PASS | PASS |
| 13 | only the read-back removed (sf_error kept) | 0 | 0 | PASS | PASS | PASS |
| 7 | only the sf_error check removed (read-back kept) | 0 | **8** | **FAIL** | PASS | PASS |
| 14 | fix restored (`cmp` with the fixed copy = 0) | 0 | 0 | PASS | PASS | PASS |

- FAIL message (steps 7, 10, 12): `'!ok' returned FALSE. (a failed flush/commit must not report success)` at
  `RetroAudioCaptureTest.cpp(350)`, with `Totals: 4 passed, 1 failed`.
- `fix.patch` is `git diff -- src/core/RetroAudioCapture.cpp`. The header is left out on purpose: the test refers to the
  new `BeforeFinalize` enumerator, so reverting the header too makes the test fail to compile rather than fail. With the
  header reverted as well (step 3), the build stopped at `RetroAudioCaptureTest.cpp:102`, and that run was discarded.
- In the variants, a removed check is neutralised in place, for example
  `if (!finalized || !(readBackMatches(...) || true))`. The function is still called, so `-Werror=unused-function`
  does not fire. Steps 5 and 6 of the first pass did not build for that reason and were discarded.
- Gate: `python3 .../feature_check.py <worktree>` gives EXIT=0 with no FAIL rows (feature-check.txt).

## What the proof shows
- The `sf_error` check after `SFC_UPDATE_HEADER_NOW` is enough on its own: step 13 is green, and step 7 is red without it.
- The read-back is not enough on its own for this injection: step 7 is red. libsndfile still reports the expected frame
  count on reopen even though the final header write failed. I did not find out why in this session. The read-back is kept
  as the checkpoint asked: it independently verifies frames, channels and rate. It does not reject any healthy take,
  because the whole test binary passes (test-15-full.log: `Totals: 17 passed, 0 failed`, EXIT=0).
- The fsync check comes from round 2 and is covered by the `flush` row.

## Honest limits
- The failure is injected by swapping the descriptor for a read-only one: real EBADF from the kernel, but no full disk.
- The Windows `_commit` and `_lseek` paths are not compiled here.
- `writeWindow()` runs on the command handler thread, not the audio thread. It adds one header rewrite and one short reopen
  per take.

## Self-check
- Fails without the fix and passes with it: steps 10/12/7 are RED with ctest EXIT=8; steps 11/13/14 are GREEN with EXIT=0.
- Deterministic: no timing, sleeps or threads in the test. Fault injection is through the LMMS_TESTING seam plus
  `dup2`, in a `QTemporaryDir`.
- Complete: `writeWindow()` is reached only through `writeRetroAudioTake()`. Its callers check the bool and the error and
  were not edited:
  - `ControlCommandsRecordingRetro.cpp:182`
  - `ControlCommandsSessionAudioRecord.cpp:293`
  - `ControlCommandsSamplerTake.cpp:331`
- No collateral damage: healthy takes are unchanged (the full test file is green), no load path is touched and no new limits are added.
- Realtime: nothing on the audio thread changed. No control commands were added.
- Registration: all three files are already fork sources (`tests/fork-sources.txt`), so no list change is needed.
- Seam: `setRetroTakeFinishTestHook` exists only under `#ifdef LMMS_TESTING`.

Delegation:
- Sonnet wrote the `finalize` test row and hook, and ran the first build and the green test.
- Haiku ran the red/green and variant proof matrix twice. The second run was needed after the variants were corrected to
  keep compiling.
- I designed and wrote the product change and the proof variants, reviewed the diffs, checked the logs and wrote this report.

## Other findings
- Other libsndfile writers, for example `src/core/ClipConsolidate.cpp`, which only checks `sf_close`, were not audited for
  the same ignored header-write failure.
