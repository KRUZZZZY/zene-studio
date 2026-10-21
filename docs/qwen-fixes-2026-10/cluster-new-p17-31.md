# Cluster new-p17-31: landing a take can overwrite a cell changed during recording

**Verdict: FIXED**

`session.record_slot_land` checked that the recorder's cell was inside the grid, but not that it was still empty. `landSlot` now refuses the landing when the cell holds a clip, before `writeAndLand` takes the recorded buffer. The take stays in hand, so the user can clear the cell and land the same take.

## Evidence

- Bug, before the fix: `src/core/ControlCommandsSessionAudioRecord.cpp:264` checked only `gridContains`, and `applyLanded` (`:213-218`) then replaced the cell's audio reference and name without checking.
- Fix: `src/core/ControlCommandsSessionAudioRecord.cpp:265-271` refuses a non-empty cell with `ControlErrorKind::Refused`. The check sits before `takePathOf` (`:273`) and `writeAndLand` (`:276`). `takeFinished()` (`src/core/SessionAudioRecorder.cpp:77-82`) resets the accumulator, so a refusal after it would discard the take.
- The check mirrors the record-start guard `recordableProblem` (`src/core/ControlCommandsSessionAudioRecord.cpp:115-118`), which refuses a non-empty cell with the same reasoning.
- Only one landing path exists: `writeAndLand` is called only from `landSlot` (`:276`), and `takeFinished` is called only from `writeAndLand` (`:225`). `src/core/Song.cpp:402` (`audioRecorder().push`) and `Song.cpp:1596` (`cancel`) are unaffected.
- Sibling `session.capture_to_slot` (`src/core/ControlCommandsSessionCapture.cpp:97`) also writes into a cell without an emptiness check. It is a different command and a MIDI path, so it is out of scope here and is flagged as a follow-up.
- Regression test: `tests/src/core/SessionAudioRecordTest.cpp:182-205`, new slot `aLandingNeverOverwritesACellFilledWhileItRecorded`. It starts a recording into cell 0, fills cell 0 with `session.set_slot` (`type:'audio'`) mid-take, and lands. It asserts `Refused` (`:195`), that the WAV was not written (`:196`), that the cell keeps its filled source (`:197`), that the recorder is still `done` (`:198`), and that after `session.clear_slot` the same take lands with the standard PCM and undo checks (`:200-204`).
- The test is deterministic: it drives periods itself on the Dummy device, with no sleeps, timing or races.
- Control surface: no command added, no command description changed, so `ControlCommandsSnapshot` is untouched. `session.record_slot_land`'s description (`:323-329`) does not promise overwrite behaviour, so it stays accurate. Its new refusal is not listed there; adding it would move the snapshot, so it is left for a separate change.
- Realtime: the change runs on the control thread only, with no allocation, locking or logging added on the audio path.
- Undo: unchanged. One undo step still restores the `<session>` block captured at landing.
- Test registration: the test is a new slot in an existing test already listed in `tests/CMakeLists.txt:1369`, so no `fork-sources` or `upstream-modifications` entry is needed.

## Commands and exit codes

Build environment note: the first build failed with `EXIT=2` on a ccache temp-file permission error under `/run/user/1000/ccache-tmp` (`logs/build-1-red.log:34`). That is not a code failure. All later builds set `CCACHE_TEMPDIR=/tmp/p17-ccache-tmp`.

| Step | Command (run from the worktree unless noted) | Log | EXIT |
| --- | --- | --- | --- |
| Build, first try | `cmake --build build --target zene SessionAudioRecordTest -j4` | `build-1-red.log` | 2 (ccache permission, not code) |
| Build, RED | `CCACHE_TEMPDIR=/tmp/p17-ccache-tmp cmake --build build --target zene SessionAudioRecordTest -j4` | `build-2-red.log` | 0 |
| **RED** (test before the fix) | `cd build/tests && ctest -R SessionAudioRecordTest --output-on-failure` | `test-1-red.log` | **8** (`1/1 Failed`, Totals 5 passed 1 failed) |
| RED detail | `cd build/tests && ./SessionAudioRecordTest aLandingNeverOverwritesACellFilledWhileItRecorded` | `test-1-red-direct.log` | 1; `FAIL!` at `SessionAudioRecordTest.cpp:195`, actual `errorKind` 0, expected 5 (Refused) |
| Fix applied, build | `CCACHE_TEMPDIR=/tmp/p17-ccache-tmp cmake --build build --target zene SessionAudioRecordTest -j4` | `build-3-green.log` | 0 |
| **GREEN** | `cd build/tests && ctest -R SessionAudioRecordTest --output-on-failure` | `test-2-green.log` | **0** (`1/1 Passed`) |
| Proof: save product diff | `git diff -- src/core/ControlCommandsSessionAudioRecord.cpp > logs/fix.patch` | - | 0 |
| Proof: revert fix | `git apply -R logs/fix.patch` | `apply-revert.log` | 0 |
| Proof: rebuild reverted | same build command | `build-4-revert.log` | 0 |
| **Proof: reverted must FAIL** | `cd build/tests && ./SessionAudioRecordTest` | `test-3-revert-full.log` | **1** (`FAIL!` at `SessionAudioRecordTest::aLandingNeverOverwritesACellFilledWhileItRecorded()`, actual 0, expected 5; Totals 5 passed 1 failed) |
| Proof: reapply fix | `git apply logs/fix.patch` | `apply-reapply.log` | 0 |
| Proof: rebuild reapplied | same build command | `build-5-reapply.log` | 0 |
| **Proof: reapplied must PASS** | `cd build/tests && ./SessionAudioRecordTest` | `test-4-reapply-full.log` | **0** (Totals 6 passed 0 failed) |
| Reapplied ctest | `cd build/tests && ctest -R SessionAudioRecordTest --output-on-failure` | `test-5-ctest-green.log` | 0 (1/1) |
| Affected family build | `CCACHE_TEMPDIR=/tmp/p17-ccache-tmp cmake --build build --target SessionAudioRecorderTest SessionAudioCellPlaybackTest SessionGridRecordTest -j4` | `build-6-family.log` | 0 |
| Affected family | `cd build/tests && ctest -R "SessionAudio\|SessionGridRecord" --output-on-failure` | `test-6-family.log` | 0 (4/4 passed: SessionGridRecordTest, SessionAudioRecorderTest, SessionAudioRecordTest, SessionAudioCellPlaybackTest) |

Affected-family test count: 4 ctest targets executed, 4 passed. The new test is one of the 6 functions in `SessionAudioRecordTest`, and all 6 pass.

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` **could not run in this sandbox**: it writes logs to `<worktree>/../<name>-feature-check/`, and the process failed with `PermissionError` creating `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-31-feature-check` (`logs/gate-1-feature-check.log`, `EXIT=1`). I did not change the script. Instead I ran each command the script lists, from the worktree, with logs in the run folder:

| Gate | Command | Log | EXIT |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | `gate-fork-sources.log` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | `gate-all-sources-reproduce.log` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream-regression.log` | 0 |
| check-namespace | `tests/scripted/check-namespace` | `gate-check-namespace.log` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | `gate-no-tautology.log` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | `gate-doc-figures.log` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | `gate-complexity.log` | 1 (5 `REGRESSION` lines, none in the two files I changed) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | `gate-file-length.log` | 1 (15 `REGRESSION` lines, none in the two files I changed) |

The two ratchet gates are red on the train on purpose. Their `REGRESSION` lines name `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ControlCommandsShortcuts.cpp`, `TakeLane.cpp` and other files, but not `ControlCommandsSessionAudioRecord.cpp` or `SessionAudioRecordTest.cpp`. `landSlot` is not named either. The `feature_check.py` rule (fail only on new items in changed files) therefore passes for this change.

## Files committed

- `src/core/ControlCommandsSessionAudioRecord.cpp`
- `tests/src/core/SessionAudioRecordTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p17-31.md`

## Follow-ups (not in this change)

- `session.capture_to_slot` (`src/core/ControlCommandsSessionCapture.cpp:97`) can overwrite a non-empty cell, the same class of defect. It is a MIDI path, so it needs its own finding.
- The `session.record_slot_land` description does not mention the new refusal. Updating it would move `ControlCommandsSnapshot`, so it should go in a change that regenerates the snapshot.

## Self-check

(filled in below after running `submit_check.py`)
