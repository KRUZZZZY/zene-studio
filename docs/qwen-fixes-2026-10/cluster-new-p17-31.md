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

**Run in the worktree itself, `feature_check.py` could not run:** it writes logs to `<worktree>/../<name>-feature-check/`, and the process failed with `PermissionError` creating `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-31-feature-check` (`logs/gate-1-feature-check.log`, script `EXIT=1`). The parent directory is not writable from this sandbox. I did not change the script.

**Run unmodified on a copy in `/tmp`, it passes:** I copied the worktree without `build/` to `/tmp/p17-gate/zene-qwen-new-p17-31` (same HEAD `ce6f2c61e`, clean) and ran `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /tmp/p17-gate/zene-qwen-new-p17-31`. Log: `logs/gate-2-feature-check-tmpcopy.log`, script **`EXIT=0`**, `RESULT: PASS`:

| Gate | Result |
| --- | --- |
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity (ratchet) | PASS, "no new item in your files" |
| file-length (ratchet) | PASS, "no new item in your files" |

The script's own per-gate logs are in `logs/feature-check-tmpcopy/`. Base `b61e5f965`, 3 changed files. A `fatal: gitfile ... doc/wiki/.git` line appears in the output; it comes from git reading a nested gitfile and does not affect any gate result.

I also ran each listed gate command directly in the worktree, with the same results: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology and doc-figures all exit 0. Complexity and file-length exit 1 with 5 and 15 `REGRESSION` lines (`gate-complexity.log`, `gate-file-length.log`). Those lines name `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ControlCommandsShortcuts.cpp`, `TakeLane.cpp` and other files, but not `ControlCommandsSessionAudioRecord.cpp`, `SessionAudioRecordTest.cpp` or `landSlot`. The ratchet rule (fail only on new items in changed files) therefore passes.

## Files committed

- `src/core/ControlCommandsSessionAudioRecord.cpp`
- `tests/src/core/SessionAudioRecordTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p17-31.md`

## Follow-ups (not in this change)

- `session.capture_to_slot` (`src/core/ControlCommandsSessionCapture.cpp:97`) can overwrite a non-empty cell, the same class of defect. It is a MIDI path, so it needs its own finding.
- The `session.record_slot_land` description does not mention the new refusal. Updating it would move `ControlCommandsSnapshot`, so it should go in a change that regenerates the snapshot.

## Self-check

- **Regression fails with the change reverted and passes with it.** Reverted (`git apply -R logs/fix.patch`, rebuild **EXIT=0**): `./SessionAudioRecordTest` **EXIT=1**, `FAIL!` at the new slot, actual `errorKind` 0, expected 5 (`logs/test-3-revert-full.log`). Reapplied (`git apply logs/fix.patch`, rebuild **EXIT=0**): `./SessionAudioRecordTest` **EXIT=0**, 6 passed (`logs/test-4-reapply-full.log`); `ctest -R SessionAudioRecordTest` **EXIT=0** (`logs/test-5-ctest-green.log`). The RED run on the original code before any edit gave the same failure (`logs/test-1-red.log`, `logs/test-1-red-direct.log`).
- **Deterministic.** There are no sleeps, no wall-clock reads and no races. The test stops the device (`initTestCase`, `SessionAudioRecordTest.cpp:104`) and drives every period itself through `period()` (`:209-229`). The input is a pure function of the frame index, `inputFrame(n)` (`:70-74`). The loops are bounded by period counts (`:186` for the new test, `:238` in `recordUntilDone`), not by time.
- **Fix at the cause, callers and siblings checked.** The landing path has one caller, `landSlot` → `writeAndLand` (`ControlCommandsSessionAudioRecord.cpp:276`), and `takeFinished()` has one caller, `writeAndLand` (`:225`), so the check before `writeAndLand` covers every landing. The record-start guard `recordableProblem` (`:115-118`) already refuses a non-empty cell, so landing now matches recording. The sibling `capture_to_slot` (`ControlCommandsSessionCapture.cpp:97`) has the same missing check, but it is a MIDI path under a different command; it is not changed here and is listed under Follow-ups.
- **No collateral.** The new refusal fires only when the target cell is non-empty at landing. Before this change that case silently overwrote the cell, so no legitimate caller relied on it. Landing into an empty cell is unchanged: the two existing landing tests in the same file (`aCellRecordsOneBar…`, `aQuantisedStopEnds…`) and the neighbour tests in the family table still pass. No save format or project field changes, so old projects and saved data load as before. The record-start check is unchanged.
- **Verdict is FIXED,** not ALREADY-FIXED or FALSE-POSITIVE, so the code-citation clause does not apply.
- **Submit check.** `submit_check.py` reports the gates row FAIL on the worktree itself, for the sandbox reason above (`PermissionError` on the parent directory). The same unmodified `feature_check.py` passes on the `/tmp` copy (`logs/gate-2-feature-check-tmpcopy.log`, `RESULT: PASS`). Only that row is affected; the other submit-check items pass (`logs/submit-check-1.log`).
