FIXED

Reason: 9e3fa27359 (`fix(take): refuse an existing file in TakeRecorder::start`) is only on `bugs/qwen-5402`. `git merge-base --is-ancestor 9e3fa27359 master` exited 1. That commit added no test. The regression test in this commit is the test for it.

## Open gap: no RED run for the current test

- The test file was last written at 06:17:38. The p3 GREEN and family runs ended at 06:05–06:10, so they ran an earlier version of the test. That version had slots `startingATakeNeverTruncatesAnExistingFile` and `startingATakeOverADirectoryIsRefused`, 9 passed. No log in the run folder mentions `takeRefusesExistingFile`.
- The current test, `takeRefusesExistingFile` (ReceiverEffectTest.cpp:308), has no RED run. The packet expected the RED failure at ReceiverEffectTest.cpp(325). Line 325 is `QVERIFY(!take->start(sentinel, 44100, &why));` in the current file, but it has not been observed failing.
- The RED logs in the run folder (`test-1-red.log`, `test-1b-red.log`) are for the earlier version. They fail at `'!started' returned FALSE` and record no exit code.
- Reproducing RED needs the reversed fix built, which means editing `src/core/TakeRecorder.cpp`. This run did not do that, because the packet forbids it.
- GREEN was rerun on the current file in this session (see Commands). Only the RED half is open.

## Finding

Before the fix, `TakeRecorder::start()` accepted a path that already held a file, and the take replaced it. The RED message in `test-1-red.log` says: "start() accepted a path that already holds a file: a take would replace it."

After the fix, `src/core/TakeRecorder.cpp:73-99`:
- lines 76-88: the file is created with `QIODevice::NewOnly`. If the open fails, `start()` returns false with `why` set (line 82 uses `QFileInfo::exists`, so an existing path gets "already exists: choose a new file; nothing was overwritten").
- line 93: `sf_open(..., SFM_WRITE, ...)`.
- line 96: if `sf_open` fails, `QFile::remove(path)` removes the empty file that the code itself created just above.

## Audit of callers

- Production callers of `start()` (checked with rg over `src/*.cpp`): only `src/core/ControlCommandsRecorder.cpp:88`. Its path is either `TakeRecorder::defaultTakePath()` or the user's file. `defaultTakePath()` (TakeRecorder.cpp:202-212) adds ` (n)` suffixes while the path exists.
- The packet cites `ReceiverEffectTest.cpp:226` as a caller. That line is stale. The current file calls `start()` at 130 (the `recordTake` helper, used at 333 and 347), 276, 325, 338 and 355.

## Test matrix (`takeRefusesExistingFile`, lines 308-358)

| Part | Lines | What it asserts |
|---|---|---|
| (a) | 319-330 | A file at the path: start refused with "already exists", not recording, bytes unchanged, `lastTake` unchanged |
| (b) | 331-343 | A recorded take: a second start on its path is refused; frames and bytes unchanged |
| (c) | 345-349 | A fresh path under a folder that does not exist yet still records |
| (d) | 351-358 | A directory at the path: refused, still a directory, still empty |
| (e) | — | `plugin.record` not covered. Per the packet, no fixture builds a Recorder target (not re-checked here) |

The packet says the reversed-fix RED aborts at the first assertion (a), so (b), (c) and (d) were exercised only under GREEN. (d) before the fix is unverified. The only pre-fix RED in the logs is for the earlier version (see Open gap).

## Commands and exit codes (from this session's logs)

- `cmake --build <wt>/build --parallel 8 --target zene ReceiverEffectTest` → EXIT 0 (`duet/08-worker/tmp/007-p4-build-green.log`). The log has no "Building CXX" line. The object `ReceiverEffectTest.cpp.o` (06:19:10) is newer than the source, and the GREEN output lists `takeRefusesExistingFile`, so the binary includes the current test.
- GREEN: `ctest -R ReceiverEffectTest -V` in `build/tests` → EXIT 0. Totals: 8 passed, 0 failed. `takeRefusesExistingFile()` PASS (`008-p4-test-green.log`).
- Family: `ctest -R "Recorder|Take|ControlCommands" --output-on-failure` → EXIT 0. 100% passed out of 19. `ControlMidiTakeRecord` is **Skipped** (`009-p4-test-family.log`). The packet's "6 tests; none skipped" is wrong.
- Gates: `python3 feature_check.py <wt> > logs/gates-2.log 2>&1` → EXIT=0, `RESULT: PASS`. The complexity and file-length gates report "no new item in your files". `gates-1.log` was run before the registration line was added and is also EXIT=0 PASS.
- Baseline: not run in this session.
- RED: not run in this session (see Open gap). The packet's "RED=8" is not supported by a log for the current test. The only `exit 8` in the run folder is the p3 first family run (16 Not Run, targets not built). That run is in `p3-test-family-first.log`.

## Registration and diff checks

- `tests/fork-sources.txt:2933` lists `ReceiverEffectTest.cpp` as inherited. `tests/upstream-modifications.txt` had no entry for it, so one was added, tab-separated, with a reason. The file still ends with a newline.
- `git diff --numstat -- tests/src/core/ReceiverEffectTest.cpp` → `104 0`.
- `git diff --stat -- src/core/TakeRecorder.cpp` → empty. The product change is already in `9e3fa27359`.
- `LOGS/fix.patch` (1427 bytes) is what the packet says was built from `git diff 9e3fa27359~1 9e3fa27359 -- src/core/TakeRecorder.cpp`. It was not regenerated here.

## Race reasoning (from the packet; threading not re-checked here)

- `start()` runs on the control path only.
- `NewOnly` makes create-or-fail atomic, so there is no check-then-open window.
- `push()` and `copyIn()` are untouched by the fix.
- Nothing new allocates, locks or logs on the audio thread.
- No control command was added.

## Directory path

A directory at the path is refused with "already exists" (`QFileInfo::exists`, TakeRecorder.cpp:82). Nothing is overwritten. The wording is slightly off for a directory, which is acceptable.

## Gaps

- Part (e), `plugin.record`, is not covered.
- TSan was not run.
- No RED run for the current test (see Open gap).

## Other findings

Truncating `SFM_WRITE` sites not examined for safety. They may write temp files or caller-checked paths:
- `ClipConsolidate.cpp:112`
- `BounceInPlace.cpp:581`
- `ControlCommandsRenderRealtime.cpp:152`
- `RenderQueue.cpp:135`
- `TrackRecorder.cpp:171`

## Self-check

