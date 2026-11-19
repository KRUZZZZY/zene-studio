FIXED

## Verdict and reason

`9e3fa27359` (`fix(take): refuse an existing file in TakeRecorder::start`) is only on `bugs/qwen-5402`: `git merge-base --is-ancestor 9e3fa27359 master` exited 1, so the fix is not yet on `master`. That commit changed only `src/core/TakeRecorder.cpp` (16 insertions) and added no test.

The regression test `takeRefusesExistingFile` is in `db57d9e28a`. This commit adds only this report. The test fails with the fix reverted (RED, exit 8) and passes with the fix applied (GREEN, exit 0). See the logs below.

## Evidence for the fix: `src/core/TakeRecorder.cpp:73-99`

- 74-75 (comment): `NewOnly` makes the open fail on an existing file, so a take never truncates one and there is no check-then-open race.
- 76-88: the file is created with `QIODevice::WriteOnly | QIODevice::NewOnly`. If the open fails, `why` is set (82-84): "`<path>` already exists: choose a new file; nothing was overwritten" when `QFileInfo::exists(path)`, otherwise "cannot write ...". Returns false.
- 93: `sf_open(..., SFM_WRITE, ...)` opens the file just created.
- 96: if `sf_open` fails, `QFile::remove(path)` removes the empty file the code created at 77.

## Caller audit

- The only production caller of `TakeRecorder::start` is `src/core/ControlCommandsRecorder.cpp:88`. Its path comes from line 86: `TakeRecorder::defaultTakePath()` or the requested file's absolute path. I searched `src` for `.start(` and `->start(`; the other matches are timers and threads.
- `src/core/ControlCommandsSamplerTake.cpp:258` and `src/core/ControlCommandsSessionAudioRecord.cpp:250` also call `defaultTakePath()`. Neither file has a `.start(` or `->start(` call. I did not trace where their paths go.
- `TakeRecorder::defaultTakePath()` is at `src/core/TakeRecorder.cpp:202-212`. Its uniqueness loop is at line 209: `for (int n = 2; QFileInfo::exists(path); ++n)` appends ` (n)` while the path exists.
- Test call sites in `tests/src/core/ReceiverEffectTest.cpp`: 130 (`take.start` inside the `recordTake` helper defined at 127; used at 333 and 347), 325, 338, 355. Line 276 is in another slot.

## Test matrix: `takeRefusesExistingFile`, `tests/src/core/ReceiverEffectTest.cpp:308-358`

| Part | Lines | What it asserts |
|---|---|---|
| (a) | 319-329 | A file at the path: start refused, message says "already exists", not recording, bytes unchanged, `lastTake` unchanged |
| (b) | 331-343 | A recorded take: a second start on its path is refused; frames and bytes unchanged |
| (c) | 345-349 | A fresh path under a folder that does not exist yet still records |
| (d) | 351-358 | A directory at the path: start refused, not recording, still a directory, still empty |
| (e) | — | `plugin.record` is not covered |

## Commands and exit codes

`LOGS` = `/home/kruzzzzy/.config/kads/zene/state/runs/5402.r1/logs`.

Preflight:
- `git status --short` → empty. `git rev-parse HEAD` → `db57d9e28aa18a5867d19094e3190a862c51890b`.
- `grep -c '^+[^+]' LOGS/fix.patch` → 16. `grep '^+++' LOGS/fix.patch` → `+++ b/src/core/TakeRecorder.cpp`.
- `git diff 9e3fa27359~1 9e3fa27359 -- src/core/TakeRecorder.cpp | cmp - LOGS/fix.patch` → EXIT 0 (identical).

Baseline (fix applied):
- `cmake --build build --target zene ReceiverEffectTest > LOGS/p4b-base-build.log 2>&1` → EXIT 0.
- `(cd build/tests && ctest -R '^ReceiverEffectTest$' --output-on-failure > LOGS/p4b-base-test.log 2>&1)` → EXIT 0; "100% tests passed out of 1". Plain ctest does not name passing slots.
- `(cd build/tests && ctest -V -R '^ReceiverEffectTest$' > LOGS/p4b-base-test-verbose.log 2>&1)` → EXIT 0; line 344: `PASS : ReceiverEffectTest::takeRefusesExistingFile()`; `Totals: 8 passed, 0 failed, 0 skipped`.

RED (fix reverted):
- `git apply -R LOGS/fix.patch` → EXIT 0. `git diff --stat -- src/core/TakeRecorder.cpp` → `1 file changed, 16 deletions(-)`.
- `cmake --build build --target zene ReceiverEffectTest > LOGS/p4b-red-build.log 2>&1` → EXIT 0. The log shows `Building CXX object src/CMakeFiles/lmmsobjs.dir/core/TakeRecorder.cpp.o` (line 14), `Linking CXX executable ../zene` (18) and `Linking CXX executable ReceiverEffectTest` (40). There is no compile line for `ReceiverEffectTest.cpp.o`: its source did not change, so the object was not rebuilt. The test binary was relinked against the rebuilt library object.
- `(cd build/tests && ctest -R '^ReceiverEffectTest$' --output-on-failure > LOGS/p4b-red-test.log 2>&1)` → EXIT 8. Line 16: `FAIL!  : ReceiverEffectTest::takeRefusesExistingFile() '!take->start(sentinel, 44100, &why)' returned FALSE`. Line 17: `Loc: [../../tests/src/core/ReceiverEffectTest.cpp(325)]`. `Totals: 7 passed, 1 failed, 0 skipped`.

GREEN (fix re-applied):
- `git apply LOGS/fix.patch` → EXIT 0. `git diff --stat -- src/core/TakeRecorder.cpp` → empty. `git status --short` → empty.
- `cmake --build build --target zene ReceiverEffectTest > LOGS/p4b-green-build.log 2>&1` → EXIT 0.
- `(cd build/tests && ctest -R '^ReceiverEffectTest$' --output-on-failure > LOGS/p4b-green-test.log 2>&1)` → EXIT 0; "100% tests passed out of 1".

Family (fix applied):
- `(cd build/tests && ctest -R 'ReceiverEffect|EffectDeviceMenu|Recorder|Take|ControlCommands' --output-on-failure > LOGS/p4b-family-test.log 2>&1)` → EXIT 0; 21 tests matched; "100% tests passed out of 21". `ControlMidiTakeRecord` is **Skipped** (log line 39), so 20 ran and passed and 1 was skipped.

Gates:
- `LOGS/gates-2.log` (an earlier run) ends with `RESULT: PASS`. Its EXIT code is not recorded in the file. The gates rerun for this commit is logged separately in `LOGS/p4b-gates.log`.

## Which assertion fails under RED

Part (a), line 325: `QVERIFY(!take->start(sentinel, 44100, &why));`. The message shows the expression `!take->start(...)` evaluated to false, so `start()` returned true: with the fix reverted, a take started on a path that already held a file.

`QVERIFY` aborts the slot, so (b), (c) and (d) are not reached under RED. (d) before the fix is therefore unverified. Under GREEN, all of (a) to (d) run and pass (verbose baseline line 344).

## Provenance of `fix.patch`

`LOGS/fix.patch` was already present and passed its checks. The working-tree diff of `src/core/TakeRecorder.cpp` was empty, so the patch is the fix commit's own diff. The `cmp` above confirms it.

## Registration

`tests/upstream-modifications.txt` gained one tab-separated line in `db57d9e28a`: `tests/src/core/ReceiverEffectTest.cpp` with the reason "cluster 5402-take-existing-file: adds the takeRefusesExistingFile regression slot for TakeRecorder::start refusing an existing file (product fix 9e3fa27359); test code only".

## Race and audio-thread reasoning (read from `src/core/TakeRecorder.cpp`, not measured)

- `start()` is reached from a control command (`ControlCommandsRecorder.cpp:88`). I did not trace the thread model further.
- `NewOnly` makes create-or-fail atomic, so there is no window between the existence check and the open.
- The fix adds 16 lines inside `start()` and nothing else. `push()` and `copyIn()` are unchanged.
- No control command was added.

## Directory-path wording

A directory at the path is refused with the "already exists" message (`QFileInfo::exists`, `TakeRecorder.cpp:82`). Nothing is overwritten. The wording says "choose a new file", which is slightly off for a directory, but no data is lost. Part (d) checks that the directory is untouched (lines 355-358), not the wording.

## Gaps

- Part (e), `plugin.record`, is not covered. It would need a Recorder fixture, which this test does not build.
- TSan was not run.
- Part (d) before the fix is unverified (see RED).

## Other findings

Five truncating `SFM_WRITE` sites, not examined for whether they are safe. They may write temp files or caller-checked paths:
- `src/core/ClipConsolidate.cpp:112`
- `src/core/BounceInPlace.cpp:581`
- `src/core/ControlCommandsRenderRealtime.cpp:152`
- `src/core/RenderQueue.cpp:135`
- `src/core/audio/TrackRecorder.cpp:171`

`grep -rl --include='*.cpp' SFM_WRITE src` lists 13 files, so these five are not all of them. The other files are not examined either.

## Self-check

