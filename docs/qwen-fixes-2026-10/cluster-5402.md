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

- The only production caller of `TakeRecorder::start` is `src/core/ControlCommandsRecorder.cpp:88`. Its path comes from line 86: `TakeRecorder::defaultTakePath()` or the requested file's absolute path. The 11 `.cpp` files under `src` that mention `TakeRecorder` (`rg -l TakeRecorder src --glob '*.cpp'`) were checked with `rg -n 'TakeRecorder::of|->start\(|\.start\('`. No other call to `TakeRecorder::start` exists in them. The other `.start(` matches are timers and threads (`MidiTakeRecorder.cpp:153`, `AudioEngine.cpp:557` and `570`, `Song.cpp:157`). The other `TakeRecorder::of` matches are the definition (`TakeRecorder.cpp:196`) and lookups (`ControlCommandsRecorder.cpp:61`, `EffectDeviceMenu.cpp:149`). `rg -n 'start\(' src/core/TakeRecorder.cpp` finds only the definition at line 66.
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
- `(cd build/tests && ctest -R '^ReceiverEffectTest$' --output-on-failure > LOGS/p6-red-test.log 2>&1; echo EXIT=$?)` → EXIT 8. In `LOGS/p6-red-test.log`: line 16 `FAIL!  : ReceiverEffectTest::takeRefusesExistingFile()`; line 19 `Totals: 7 passed, 1 failed, 0 skipped, 0 blacklisted, 545ms`; line 27 `The following tests FAILED:`; line 30 `EXIT=8`. `LOGS/p4b-red-test.log` has no EXIT line; it is supporting evidence only.

GREEN (fix re-applied):
- `git apply LOGS/fix.patch` → EXIT 0. `git diff --stat -- src/core/TakeRecorder.cpp` → empty. `git status --short` → empty.
- `cmake --build build --target zene ReceiverEffectTest > LOGS/p4b-green-build.log 2>&1` → EXIT 0.
- `(cd build/tests && ctest -R '^ReceiverEffectTest$' --output-on-failure > LOGS/p4b-green-test.log 2>&1)` → EXIT 0; "100% tests passed out of 1".

Family (fix applied):
- `(cd build/tests && ctest -R 'ReceiverEffect|EffectDeviceMenu|Recorder|Take|ControlCommands' --output-on-failure > LOGS/p4b-family-test.log 2>&1)` → EXIT 0; 21 tests matched; "100% tests passed out of 21". `ControlMidiTakeRecord` is **Skipped** (log line 39), so 20 ran and passed and 1 was skipped.

Gates:
- `LOGS/p5-gates.log` is the gates run for this report: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/p5-gates.log 2>&1; echo EXIT=$?` → EXIT 0. The log ends with `RESULT: PASS`. (`LOGS/gates-2.log` is no longer cited: its exit code is not recorded.)

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

1. **RED and GREEN exit codes.** Yes. RED (fix reverted with `git apply -R LOGS/fix.patch`): exit 8 is in `LOGS/p6-red-test.log` line 30 (`EXIT=8`), from the ctest run in the RED section above; its line 16 is `FAIL!  : ReceiverEffectTest::takeRefusesExistingFile()` and its line 19 is `Totals: 7 passed, 1 failed, 0 skipped, 0 blacklisted, 545ms`. `LOGS/p4b-red-test.log` has no exit-code line, but it shows `FAIL!  : ReceiverEffectTest::takeRefusesExistingFile() '!take->start(sentinel, 44100, &why)' returned FALSE` at `tests/src/core/ReceiverEffectTest.cpp(325)` and `Totals: 7 passed, 1 failed`. GREEN (fix applied): `LOGS/p6-green-test.log` has `EXIT=0` at line 8 and `100% tests passed out of 1` at line 5; `LOGS/p4b-green-test.log` shows `100% tests passed out of 1`. A fresh run of the same ctest command in this session, through the quiet runner, printed `exit 0` (log `duet/12-worker/tmp/001-p5-green-now.log`).
2. **Deterministic.** Yes, with one gap. A search of `tests/src/core/ReceiverEffectTest.cpp` for `sleep|qWait|QTimer|msleep|usleep|QThread|wait|QElapsedTimer|currentTime|QDateTime` found no matches. The test uses fixed values: sample rate 44100 (lines 325, 338, 355; helper line 130), a `QTemporaryDir` (line 316), 10 blocks of `stereoBlock(0.25f, 0.5f)` (helper lines 133-135), and exact frame counts `10 * Frames` (lines 336, 342, 349). Not traced: whether `TakeRecorder::stop` returns only after the writer thread has finished. So the test alone does not show the absence of races. It passed in `LOGS/p4b-green-test.log` and in the fresh run above.
3. **Fix at the cause and complete.** The cause is the open with `QIODevice::NewOnly` at `src/core/TakeRecorder.cpp:76-88`, which makes create-or-fail atomic. The only caller of `TakeRecorder::start` is `src/core/ControlCommandsRecorder.cpp:88` (see the corrected caller audit). The fix changes only `start()`: `git show --stat 9e3fa27359` shows 16 insertions in `src/core/TakeRecorder.cpp`. The family run in `LOGS/p4b-family-test.log` shows `100% tests passed out of 21` (line 45) and one skipped test, `ControlMidiTakeRecord` (line 49). This is complete for `TakeRecorder::start` only; the five other truncating `SFM_WRITE` sites under Other findings were not examined.
4. **No collateral damage.** The check refuses only a path that already exists. `git show --stat 9e3fa27359` shows no file outside `src/core/TakeRecorder.cpp`, so project-loading and saved-data code is not touched. Generated paths never hit the refusal: `defaultTakePath()` (`src/core/TakeRecorder.cpp:202-211`) loops at line 209 until the name is unused. The one caller with a user-given path, `src/core/ControlCommandsRecorder.cpp:88`, now gets a refusal where it used to truncate an existing file; that is the intended change. Loading old projects and saved data was not run, and no test here loads a project.
5. **ALREADY-FIXED or FALSE-POSITIVE evidence.** Not applicable: the verdict on line 1 is FIXED.

