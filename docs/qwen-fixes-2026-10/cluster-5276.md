ALREADY-FIXED

# Cluster 5276 — A failed rename can destroy an existing retrospective take

5276: ALREADY-FIXED

The defect was real when the finding was written: `writeWindow()` removed the destination
(`QFile::remove(filePath)`) and only then moved the staged `.partial` over it
(`QFile::rename(partial, filePath)`), so a failed move reported the failure after deleting the old
take (`src/core/RetroAudioCapture.cpp:184-189` at `c2f2b48c2c^`). On this branch that sequence is
gone: `c2f2b48c2c` ("fix: concurrent retrospective takes (cluster new-p9-400)") replaced it with
`QSaveFile` staging, disabled QSaveFile's direct-write fallback, and checks `commit()`. That commit
is an ancestor of HEAD (`git merge-base --is-ancestor c2f2b48c2c HEAD` → 0).

This round adds the missing proof: a deterministic, portable fault seam at the final move and one
regression test that starts from a **regular take written by the real writer**, forces the move to
fail, and fails under the historical remove-then-rename writer while passing under the current one.

## What changed since the send-back

- Deleted the two round-1 tests (`aTakeThatCannotBeStagedKeepsTheExistingTake`,
  `aFailedCommitKeepsWhatTheDestinationHeld`) and their `QSKIP` paths. `grep -c QSKIP` on the test
  file now returns 0.
- Added a portable, deterministic one-shot seam, `retroaudiotesting::failNextTakeMove()`
  (`include/RetroAudioCapture.h:215-220`, `src/core/RetroAudioCapture.cpp:272`, `:383-386`), which
  makes the next call refuse the final move. It is inside `#ifdef LMMS_TESTING`, like the existing
  `failNextTakeFinish()` seam, so release builds are unchanged; it uses an atomic flag, no
  permissions, no timing and no platform guard, so it runs as root and on every supported platform.
- Added one test, `RetroAudioCaptureTest::aFailedTakeMoveKeepsTheExistingTake`
  (`tests/src/core/RetroAudioCaptureTest.cpp:435-479`): it writes a complete 32-frame take with the
  real writer, keeps its bytes, arms the seam, writes a second 64-frame take, and asserts the
  second write fails with an error and `written == 0`, the destination still holds the first take
  byte for byte, no staging sibling is left, and the seam was one shot (the next write lands).
- The seam sits exactly where the move is attempted: in the current writer immediately before
  `output.commit()` (`src/core/RetroAudioCapture.cpp:350-364`); in the historical writer used for
  the RED run, immediately after `QFile::remove(filePath)` and before
  `QFile::rename(partial, filePath)`. So the test exercises the reported window, not a stand-in.

## Evidence (file:line)

| # | Claim | Source | What it shows |
|---|---|---|---|
| E1 | The destructive sequence is gone | `src/core/RetroAudioCapture.cpp:305-307` | `QSaveFile output(filePath); output.setDirectWriteFallback(false);` — the staged file is a sibling temporary; the destination is not touched at open |
| E2 | Nothing touches the destination before the move | `src/core/RetroAudioCapture.cpp:309-360` | open failure, short write and flush/close failure all `return false` before any replace; only `commit()` can replace the destination |
| E3 | The move is the checked `commit()` | `src/core/RetroAudioCapture.cpp:361-366` | `if (!output.commit()) { *error = "could not commit the written take to %1: %2"; return false; }` |
| E4 | The seam injects a refused move at the move site | `src/core/RetroAudioCapture.cpp:350-360`, `include/RetroAudioCapture.h:215-220` | `if (s_failNextTakeMove.exchange(false, …)) { … return false; }` immediately before `commit()`; one-shot atomic, `LMMS_TESTING` only |
| E5 | The fix that removed the sequence is in this branch | `git show c2f2b48c2c -- src/core/RetroAudioCapture.cpp`; `git merge-base --is-ancestor c2f2b48c2c HEAD` | the `-` lines are `QFile::remove(filePath)` and `QFile::rename(partial, filePath)`; the `+` lines are the `QSaveFile` block; ancestor EXIT=0 |
| E6 | The test starts from a real existing take | `tests/src/core/RetroAudioCaptureTest.cpp:441-450` | the destination is written by `writeRetroAudioTake` itself (32 frames), then read back and kept as `previous`; the second write is armed with the seam (`:456-462`) |
| E7 | All callers propagate the failure and none deletes the destination | `src/core/ControlCommandsRecordingRetro.cpp:182`, `src/core/ControlCommandsSamplerTake.cpp:331`, `src/core/ControlCommandsSessionAudioRecord.cpp:293` | each returns `ControlResult::failure(...)`; the retro default path (`zene-retro-take.wav`) is reused by every take, which is the finding's case |
| E8 | The portability/skip complaint is gone | test file `:435-479` | no `QSKIP`, no `<unistd.h>`, no permission trick, no directory stand-in: the failure comes from the atomic seam |

## RED / GREEN proof (logs in `runs/5276.r2/logs`)

`fix.patch` is the fix, not the defect: it is `git diff -R -- src/core/RetroAudioCapture.cpp`
taken while the tree held the historical writer, so it turns the historical writer
(`.partial` + `QFile::remove(filePath)` + `QFile::rename(partial, filePath)`) into the current one.
Reversing it (`git apply -R`) restores the historical order and moves the same seam call after the
remove; applying it restores the current order.

| Step | Command | EXIT | Result |
|---|---|---|---|
| reverse | `git apply -R fix.patch` | 0 | historical writer in the tree |
| build | `cmake --build build --target RetroAudioCaptureTest -j4 > 16-build-4b-red.log 2>&1` | 0 | linked |
| RED | `cd build/tests && ./RetroAudioCaptureTest aFailedTakeMoveKeepsTheExistingTake > 17-test-4b-red.log 2>&1` | **1** | `FAIL! … 'after.open(QIODevice::ReadOnly)' returned FALSE` — the old take was deleted by the remove, the refused move reported failure; `Totals: 2 passed, 1 failed` |
| apply | `git apply fix.patch` | 0 | current writer restored; `git status` shows only the report |
| build | `cmake --build build --target zene RetroAudioCaptureTest -j4 > 19-build-4b-green.log 2>&1` | 0 | current writer rebuilt |
| GREEN | `cd build/tests && ./RetroAudioCaptureTest > 20-test-4b-green.log 2>&1` | **0** | `Totals: 16 passed, 0 failed, 0 skipped` |
| family | `cd build/tests && ctest -R '^RetroAudioCaptureTest$' --output-on-failure > 21-ctest-green.log 2>&1` | 0 | `100% tests passed out of 1` |

The RED failure is on the assertion that opens the destination: under the historical order the file
is gone (`after.open` false). Under the current order the same seam leaves it intact and the test
reaches the byte comparison and the one-shot check.

## Test matrix

| Path | Case | Result | Evidence |
|---|---|---|---|
| Normal | full `RetroAudioCaptureTest` family (16 cases), including the 700-frame writer and concurrent takes | PASS | `07-test-seam-green.log`, `20-test-4b-green.log`, `21-ctest-green.log` |
| Failure — refused move | existing regular take, complete second take staged, move refused | PASS: `false`, non-empty error, `written == 0`, old bytes unchanged, no staging sibling, next write succeeds (test `:435`) | `20-test-4b-green.log`; historical writer RED at `17-test-4b-red.log` |
| Failure — flush/close after all frames accepted | existing `aFailedFlushOrCloseIsReportedAndKeepsTheOldTake` (`:393`), one-shot `failNextTakeFinish()` | PASS | `20-test-4b-green.log` |
| Failure — empty window | existing `anEmptyWindowIsRefusedRatherThanWritten` | PASS | `20-test-4b-green.log` |
| Exhaustion | full disk / failing device is the same flush/close path (`:393`); no separate allocation or buffer limit in the writer | PASS (seam-injected) | `20-test-4b-green.log` |
| Reconfiguration | n/a: the writer takes path, frames and rate per call; no configuration state | — | — |
| Audio thread | `push()`/`transitionRateIfNeeded()`/`takeWindow()` untouched; the seam and the writer are command-handler code, no allocation, lock, wait or log added to the audio path | — | `git diff` of the seam commit |
| Platforms / root | no POSIX permission, no `QSKIP`, no directory stand-in: an atomic flag is the fault injector, so the test is identical on Windows, macOS and Linux and as root | PASS (built and run here as uid 1000) | `:435-479`, `grep -c QSKIP` = 0 |

## Commands and EXIT codes

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/5276.r2/logs`.

| # | Command | EXIT |
|---|---|---|
| 01 | `cmake --build build --target zene RetroAudioCaptureTest -j4 > 01-build-seam.log 2>&1` (first round-2 build) | 0 |
| 02 | `cd build/tests && ./RetroAudioCaptureTest > 02-test-green.log 2>&1` | 0 (`aFailedTakeMoveKeepsTheExistingTake` PASS, 16 passed) |
| 06 | `cmake --build build --target zene RetroAudioCaptureTest -j4 > 06-build-seam-green.log 2>&1` (seam restored after patch bookkeeping) | 0 |
| 07 | `cd build/tests && ./RetroAudioCaptureTest > 07-test-seam-green.log 2>&1` | 0 (16 passed) |
| 08 | `git add include/RetroAudioCapture.h src/core/RetroAudioCapture.cpp tests/src/core/RetroAudioCaptureTest.cpp && git commit -m "test(5276): deterministic move-failure seam and regression test for a refused take move"` | 0 (`448218a5ae`) |
| 09 | historical writer staged in the product file; `git diff -- src/core/RetroAudioCapture.cpp > historical-writer.patch` and `git diff -R -- src/core/RetroAudioCapture.cpp > fix.patch` | 0 | `fix.patch` = historical→current; `historical-writer.patch` = current→historical |
| 10 | `cd build/tests && ./RetroAudioCaptureTest aFailedTakeMoveKeepsTheExistingTake > 10-test-red.log 2>&1` (first historical run, same result as 17) | **1** |
| 14 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > 14-feature-check.log 2>&1` | 0 (every row PASS, `RESULT: PASS`) |
| 15 | `git apply -R fix.patch` (fix.patch is the historical→current diff) | 0 |
| 16 | `cmake --build build --target RetroAudioCaptureTest -j4 > 16-build-4b-red.log 2>&1` | 0 |
| 17 | `cd build/tests && ./RetroAudioCaptureTest aFailedTakeMoveKeepsTheExistingTake > 17-test-4b-red.log 2>&1` | **1** (RED) |
| 18 | `git apply fix.patch` | 0 |
| 19 | `cmake --build build --target zene RetroAudioCaptureTest -j4 > 19-build-4b-green.log 2>&1` | 0 |
| 20 | `cd build/tests && ./RetroAudioCaptureTest > 20-test-4b-green.log 2>&1` | 0 (16 passed) |
| 21 | `cd build/tests && ctest -R '^RetroAudioCaptureTest$' --output-on-failure > 21-ctest-green.log 2>&1` | 0 (100% passed) |
| 22 | `git add docs/qwen-fixes-2026-10/cluster-5276.md && git commit -m "docs(5276): report ALREADY-FIXED with the deterministic refused-move regression test"` | 0 (`d8b260e425`) |
| 23 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> 5276 > 23-submit.log 2>&1` | 0, every row PASS, `RESULT: PASS - submit` (log `23-submit.log`) |
| 24 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > 24-feature-check-final.log 2>&1` | 0 (every row PASS, `RESULT: PASS`) |

## Self-check

- **"The regression test FAILS with your product change reverted and PASSES with it (exit codes)."**
  `aFailedTakeMoveKeepsTheExistingTake` fails against the historical writer (EXIT=1,
  `17-test-4b-red.log`: `after.open` false — the old take was removed) and passes against the current
  writer (EXIT=0, `20-test-4b-green.log`: 16 passed). The product change of this round is the
  `LMMS_TESTING` seam; the behavioural difference the test measures is the writer's order
  (remove-then-rename versus staged `QSaveFile::commit()`), which is what `fix.patch` swaps.
- **"The test is deterministic: no wall-clock timing, sleeps or races."**
  One thread, no clock, no sleep. The failure is a one-shot `std::atomic<bool>` consumed with
  `exchange` at the move site (`src/core/RetroAudioCapture.cpp:356`); the destination is a regular
  take produced by the real writer, not a hand-made file or a directory.
- **"The fix is at the cause and complete: every caller and sibling of what you changed was
  checked."** I changed no writer logic; the seam is `LMMS_TESTING`-only and no-ops in release.
  All three callers of `writeRetroAudioTake` (`ControlCommandsRecordingRetro.cpp:182`,
  `ControlCommandsSamplerTake.cpp:331`, `ControlCommandsSessionAudioRecord.cpp:293`) propagate
  `false` plus the error text and none removes the destination. Sibling writers
  (`TrackRecorder::disarm()`, `TakeRecorder::stop()`) do not use remove-then-rename; their ignored
  close results are the separate cluster 5275-port item.
- **"No collateral damage: old projects and saved data still load as before, and any limit or
  check you added does not hit other callers that legitimately exceed it."**
  Release behaviour is unchanged: the seam compiles only under `LMMS_TESTING` and, when not armed,
  is one relaxed atomic exchange before `commit()`. No format, project-loading or command path
  changed; the full 16-case family and the take writer's normal path pass.
- **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
  contract that shows it."** `src/core/RetroAudioCapture.cpp:305-307` (QSaveFile staging, fallback
  disabled), `:309-360` (every earlier failure returns before the destination is touched), `:361-366`
  (the only replace is the checked `commit()`); `git show c2f2b48c2c` removed the
  `QFile::remove`/`QFile::rename` pair and is an ancestor of HEAD.
- **Sibling commands in the command files the diff touched:** the `submit_check` list (`app.*`,
  `arrangement.*`) comes from the branch-wide diff against `050/m0-train-35`, not from this lane.
  My commits touch only `include/RetroAudioCapture.h`, `src/core/RetroAudioCapture.cpp`,
  `tests/src/core/RetroAudioCaptureTest.cpp` and the report; no command file is changed, so no
  command behaviour or undo step changes.

## Other findings

- `QFile::rename()` in Qt is not one syscall: on Unix it falls back to
  `linkat(/proc/self/fd/N, dest)` + `unlink(source)` and then to a copy when `rename()` fails. That
  fallback is why a syscall-level probe can report success for the historical writer; it is not a
  defect in this cluster, and the seam-based test does not depend on it.
- `TrackRecorder::disarm()` (`src/core/audio/TrackRecorder.cpp`) and `TakeRecorder::stop()`
  (`src/core/TakeRecorder.cpp`) still ignore `sf_write_sync()`/`sf_close()` results (cluster
  5275-port "Other findings", outside this cluster).
- The proof manifest for this round is
  `/home/kruzzzzy/.config/kads/zene/state/runs/5276.r2/logs/proof.json`; the report is committed
  with the seam and the test.
