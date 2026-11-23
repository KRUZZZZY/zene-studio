ALREADY-FIXED

# Cluster 5276 — A failed rename can destroy an existing retrospective take

5276: ALREADY-FIXED

## Verdict

The finding described real code when it was written: `writeWindow()` removed the destination
(`QFile::remove(filePath)`) and only then moved the staged `.partial` over it
(`QFile::rename(partial, filePath)`), so a failed move left the old take already deleted.
That code is `src/core/RetroAudioCapture.cpp:184-189` at `c2f2b48c2c^` (log `04-prefix-code.log`).

On this branch the destructive sequence is gone. Commit `c2f2b48c2c` ("fix: concurrent
retrospective takes (cluster new-p9-400)") replaced it with `QSaveFile` staging, disabled
QSaveFile's direct-write fallback, and checks `commit()`. That commit is an ancestor of HEAD
(`git merge-base --is-ancestor c2f2b48c2c HEAD` → EXIT=0, log `01-history.log`). The current
file contains no `QFile::remove`, no `QFile::rename`, and no `.partial` at all (same log).

No product change is made by this lane. The commit for this verdict adds the report and two
contract tests to the existing test file; behaviour outside the tests is unchanged.

## Evidence (file:line)

| # | Claim | Source | What it shows |
|---|---|---|---|
| E1 | The destructive sequence is gone | `src/core/RetroAudioCapture.cpp:32`, `:303-306` | `#include <QSaveFile>` (no `<QFile>`); `QSaveFile output(filePath); output.setDirectWriteFallback(false);` — a staging temporary is created NEXT TO the destination and the destination is only replaced by `commit()` |
| E2 | A write that cannot be staged returns before touching the destination | `src/core/RetroAudioCapture.cpp:307-312` | `if (!output.open(...)) { *error = ...; return false; }`; the QSaveFile destructor discards the temporary and the destination keeps its bytes |
| E3 | Short writes and flush/close failures are refused BEFORE the commit | `src/core/RetroAudioCapture.cpp:336-350` | both checks `return false` while the destination is still untouched (`QSaveFile` discards the temporary; comment at `:342-344`) |
| E4 | The move itself is checked, and its failure is reported after leaving the destination alone | `src/core/RetroAudioCapture.cpp:351-356` | `if (!output.commit()) { *error = "could not commit the written take to %1: %2"; return false; }` |
| E5 | The direct-write fallback is the one way "save" could destroy the old take in place | `src/core/RetroAudioCapture.cpp:306` | Qt's fallback opens the DESTINATION directly when the staging file cannot be created, truncating it; `setDirectWriteFallback(false)` refuses that path (measured by the RED run in log `10-test-red.log`) |
| E6 | The fix that removed the sequence is in this branch | `git show c2f2b48c2c -- src/core/RetroAudioCapture.cpp` (log `01-history.log`, `04-prefix-code.log`) | the `-` lines are `QFile::remove(filePath)` and `QFile::rename(partial, filePath)`; the `+` lines are the `QSaveFile` block; `git log -S'QFile::remove(filePath)'` names only that commit and the earlier one that added it |
| E7 | Every caller treats a failed write as "nothing landed" and none deletes the destination | `src/core/ControlCommandsRecordingRetro.cpp:182`, `src/core/ControlCommandsSamplerTake.cpp:331`, `src/core/ControlCommandsSessionAudioRecord.cpp:293` | each returns `ControlResult::failure(... error)`; none removes the path. The retro command's default path (`zene-retro-take.wav`, `ControlCommandsRecordingRetro.cpp:171-173`) is reused by every take, which is exactly the case this finding is about |
| E8 | The guarantee is under test in the repository | `tests/src/core/RetroAudioCaptureTest.cpp:397` | the existing `aFailedFlushOrCloseIsReportedAndKeepsTheOldTake` already asserts that a failed take write leaves the previous bytes in place |

## Reproduction, measured (not only read)

The earlier lane called this `REAL-UNTESTED` because `QFile::rename()` cannot be made to fail
from the test process. It can be made to fail from outside the process. I built two copies of the
writer in scratch, both with the same driver and the same input (64 frames `(0.25, -0.25)`, a
13-byte destination holding `previous take`):

- HEAD: `src/core/RetroAudioCapture.cpp` from this worktree;
- pre-fix: `git show c2f2b48c2c^:src/core/RetroAudioCapture.cpp`.

An `LD_PRELOAD` shim makes `rename()`/`renameat()`/`renameat2()` to the destination fail with
`EACCES`, makes `linkat()` to the destination fail with `EACCES` (Qt's `QFile::rename()` falls
back to `linkat(/proc/self/fd/N, dest)` + `unlink(source)` when `rename()` fails — the first
probe attempt showed the fallback masking the loss), and refuses to open the destination for
writing (so the fallback's copy path fails too). `unlink()` is logged so the destructive step is
visible. Everything is deterministic: no clock, no sleep, no thread. Log `05-probe.log`:

```
== current writer (HEAD), staged move forced to fail ==
[shim] rename('.../take.wav.HirEEU', '.../take.wav') -> EACCES
[shim] unlink('.../take.wav.HirEEU')
ok=0 written=0 error=could not commit the written take to .../take.wav: Permission denied
dest_exists=1 dest_hex=70726576696f75732074616b65      ← "previous take", intact

== pre-fix writer (c2f2b48c2c^), staged move forced to fail ==
[shim] unlink('.../take.wav')                          ← the old take is removed FIRST
[shim] rename('.../take.wav.partial', '.../take.wav') -> EACCES
[shim] linkat('/proc/self/fd/4', '.../take.wav') -> EACCES
[shim] unlink('.../take.wav.partial')
ok=0 written=0 error=could not move the written take to ...
dest_exists=0                                           ← the old take is gone
```

This reproduces the finding against the historical code and shows the current code reports the
same failure with the destination intact. The scratch probe is not committed (it cannot be a
portable repository test); the log is kept at
`/home/kruzzzzy/.config/kads/zene/state/runs/5276.r1/logs/05-probe.log`.

## Test matrix

| Path | Case | Result | Evidence |
|---|---|---|---|
| Normal | 17 `RetroAudioCaptureTest` cases, including writing a 700-frame take and the concurrent-take test | PASS (17 passed, 0 failed, 0 skipped) | `08-test-new.log`, `12-test-green.log` |
| Failure — write cannot be staged | destination exists, parent directory read-only, so the staging file cannot be created | PASS: refused with an error, `written == 0`, old bytes unchanged, no staging sibling (test `RetroAudioCaptureTest::aTakeThatCannotBeStagedKeepsTheExistingTake`, `tests/src/core/RetroAudioCaptureTest.cpp:437`) | `08-test-new.log`; RED with the direct-write fallback enabled in `10-test-red.log` |
| Failure — move/commit fails after a complete stage | destination is a non-empty directory, so the move fails on every platform | PASS: refused with an error, `written == 0`, directory and its marker bytes intact, no staging file left (test `RetroAudioCaptureTest::aFailedCommitKeepsWhatTheDestinationHeld`, `:486`) | `08-test-new.log` |
| Exhaustion (full disk / failing device) | flush/close failure injected after every frame was accepted | PASS (existing `aFailedFlushOrCloseIsReportedAndKeepsTheOldTake`, `:397`): refused before the commit, old bytes kept | `08-test-new.log` |
| Reconfiguration | n/a | The writer has no configuration; it takes a path, frames and a rate per call | — |
| Audio thread | n/a | `push()`/`transitionRateIfNeeded()`/`takeWindow()` are untouched; the writer is a command-handler path (`include/RetroAudioCapture.h:200-207`) and adds no allocation, lock or I/O on the audio thread | `git diff --stat` (test file only) |
| Supported platforms | `aFailedCommitKeepsWhatTheDestinationHeld` uses only `QDir`/`QFileInfo`, so it builds and runs on Windows, macOS and Linux. `aTakeThatCannotBeStagedKeepsTheExistingTake` relies on POSIX directory permissions; it is compiled `#if defined(Q_OS_UNIX)` and `QSKIP`ped under `#else`, and it also `QSKIP`s when `geteuid() == 0`, because root ignores the permission that injects the failure | test source lines `437-484` |

## Commands and EXIT codes

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/5276.r1/logs`.

| # | Command | EXIT | Result |
|---|---|---|---|
| 01 | `git merge-base --is-ancestor c2f2b48c2c HEAD; git log -S'QFile::remove(filePath)' -- src/core/RetroAudioCapture.cpp; git log -S'QSaveFile output(filePath)' -- src/core/RetroAudioCapture.cpp; grep -n 'QFile::remove\|QFile::rename\|\.partial\|QSaveFile\|directWriteFallback\|commit()' src/core/RetroAudioCapture.cpp > 01-history.log` | 0 | ancestor EXIT=0; the remove+rename was removed by `c2f2b48c2c`; no destructive call remains (`01-history.log`) |
| 02 | `cmake --build build --target zene RetroAudioCaptureTest -j4 > 02-build.log 2>&1` | 0 | `Built target RetroAudioCaptureTest`, `Built target zene` |
| 03 | `cd build/tests && ctest -R '^RetroAudioCaptureTest$' --output-on-failure > 03-test.log 2>&1` | 0 | `100% tests passed out of 1` |
| 04 | `git show c2f2b48c2c^:src/core/RetroAudioCapture.cpp \| sed -n '175,200p' > 04-prefix-code.log` | 0 | pre-fix `QFile::remove(filePath)` / `QFile::rename(partial, filePath)` at lines 184-189 |
| 05 | build the HEAD and pre-fix writers with `g++ -std=c++20 -fPIC -Iinclude -Ibuild $(pkg-config --cflags Qt6Core sndfile) driver.cpp <writer>.cpp` and the `LD_PRELOAD` shim, then run both over an existing take (`05-probe.log`) | 0 | HEAD keeps `previous take`; pre-fix loses it (`dest_exists=0`) |
| 06 | `python3 .../feature_check.py <worktree> > 06-feature-check.log 2>&1` (before the test edit) | 0 | every row PASS, `RESULT: PASS` |
| 07 | `cmake --build build --target RetroAudioCaptureTest -j4 > 07-build-tests.log 2>&1` (after adding the two tests) | 0 | linked |
| 08 | `cd build/tests && ./RetroAudioCaptureTest > 08-test-new.log 2>&1` | 0 | `Totals: 17 passed, 0 failed, 0 skipped` |
| 09 | `sed -i 's/output.setDirectWriteFallback(false);/output.setDirectWriteFallback(true);/' src/core/RetroAudioCapture.cpp` then `git diff -- src/core/RetroAudioCapture.cpp > guard-revert.patch` then `cmake --build build --target RetroAudioCaptureTest -j4 > 09-build-red.log 2>&1` | 0 | the temporary regression builds |
| 10 | `cd build/tests && ./RetroAudioCaptureTest aTakeThatCannotBeStagedKeepsTheExistingTake aFailedCommitKeepsWhatTheDestinationHeld > 10-test-red.log 2>&1` | 1 | `FAIL! ... aTakeThatCannotBeStagedKeepsTheExistingTake() '!ok' returned FALSE` — the guard test is not vacuous |
| — | `git apply -R guard-revert.patch` | 0 | `setDirectWriteFallback(false)` restored; `git diff --stat` shows only the test file |
| 11 | `cmake --build build --target zene RetroAudioCaptureTest -j4 > 11-build-green.log 2>&1` | 0 | rebuilt the product target and the test |
| 12 | `cd build/tests && ctest -R '^RetroAudioCaptureTest$' --output-on-failure > 12-test-green.log 2>&1` | 0 | `100% tests passed out of 1` |
| 13 | `python3 .../feature_check.py <worktree> > 13-feature-check.log 2>&1` | 0 | every row PASS, `RESULT: PASS` (350 changed files on the branch; no new complexity or file-length item in my files) |

Step 4b of the procedure (reverse my product patch) does not apply: this lane changes no product
file, so `git diff -- src/ include/ plugins/` is empty. The guard test was instead proven
non-vacuous by 09-12 above (product protection temporarily regressed, test fails; protection
restored, test passes), and the defect itself was proven by the measured pre-fix/HEAD comparison
in 05.

## Self-check

The completion check asks these questions for every non-feature unit:

- **"The regression test FAILS with your product change reverted and PASSES with it (exit codes)."**
  There is no product change to revert: the verdict is ALREADY-FIXED and
  `git diff -- src/ include/ plugins/` is empty. The two tests I added are contract guards for the
  guarantee the existing fix provides. `aTakeThatCannotBeStagedKeepsTheExistingTake` *does* fail
  if the product's protection is removed: with `output.setDirectWriteFallback(true)` the same
  test gave EXIT=1 (`10-test-red.log`: `'!ok' returned FALSE`), and with the protection restored
  `RetroAudioCaptureTest` gave EXIT=0, 17 passed (`08-test-new.log`, `12-test-green.log`). The
  actual defect was measured against the historical writer in `05-probe.log`: pre-fix
  `dest_exists=0` after a forced move failure, HEAD `dest_exists=1` with the old bytes intact.
- **"The test is deterministic: no wall-clock timing, sleeps or races."**
  Both new tests are single-threaded and use no clock or sleep. `aTakeThatCannotBeStaged…` makes
  the staging open fail with a directory permission, restored immediately after the call;
  `aFailedCommitKeepsWhatTheDestinationHeld` makes the move fail by putting a non-empty directory
  at the destination. The out-of-tree probe uses `LD_PRELOAD` fault injection, not timing.
- **"The fix is at the cause and complete: every caller and sibling of what you changed was
  checked."** I changed no product code. For the verdict I checked all three callers of
  `writeRetroAudioTake` (E7): each propagates the failure and none removes the destination, so
  the non-destructive guarantee reaches them unchanged. The sibling take writers
  (`TrackRecorder::disarm()`, `TakeRecorder::stop()`) do not use a remove-then-rename sequence;
  they are listed under Other findings for a different, already-known issue (ignored close
  results, cluster 5275-port).
- **"No collateral damage: old projects and saved data still load as before, and any limit or
  check you added does not hit other callers that legitimately exceed it."**
  The product code is byte-identical to HEAD (`git diff --stat` shows only
  `tests/src/core/RetroAudioCaptureTest.cpp`). No format, limit or project-loading path changed;
  the existing 17 test cases pass, including the take writer's normal path, the empty-window
  refusal and the concurrent-take case.
- **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
  contract that shows it."** E1-E5 above cite `src/core/RetroAudioCapture.cpp:32`, `:303-306`,
  `:307-312`, `:336-350` and `:351-356`, and E6 cites the commit that removed the sequence,
  verified to be an ancestor of HEAD.
- **Sibling commands in the command files the diff touched:** no command file changed by this
  lane. `submit_check.py` lists `app.*` and `arrangement.*` because it diffs the whole branch
  against `050/m0-train-35`, not my commit: those files arrived with the train merges, and
  `git diff --name-only <my commit>^..<my commit>` contains only
  `docs/qwen-fixes-2026-10/cluster-5276.md` and `tests/src/core/RetroAudioCaptureTest.cpp`. The
  finding's callers in `record.*`/`session.*` are unaffected in behaviour: they never removed the
  destination themselves and still receive `false` plus the writer's error text.

## Other findings

- `QFile::rename()` on Unix is not a single syscall in Qt: when `rename()` fails it falls back to
  `linkat(/proc/self/fd/N, dest)` + `unlink(source)`, and, when that is unavailable, to a copy.
  My first probe (not the one logged as final) reported `ok=1` for the pre-fix writer because
  that fallback rescued the move. This is why the earlier lane's `QFile::rename()` fault
  injection could not show the defect: the final probe fails the fallback as well, which is what
  a real "the move cannot happen" failure (EACCES/EISDIR/ENOSPC) does. Not a defect in this
  cluster; recorded so the next lane does not re-derive it.
- `TrackRecorder::disarm()` (`src/core/audio/TrackRecorder.cpp`) and `TakeRecorder::stop()`
  (`src/core/TakeRecorder.cpp`) still ignore `sf_write_sync()`/`sf_close()` results. That is the
  cluster 5275-port "Other findings" item, outside this cluster, and not changed here.
- `record.retro_capture_to_take` writes to the same default path (`zene-retro-take.wav`) on every
  take by design; with the current writer an existing take is only replaced by a complete,
  committed write. No change needed.
