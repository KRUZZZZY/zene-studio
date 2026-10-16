# Cluster new-p12-99

Verdict: **BLOCKED**

The audio-path Qt emissions are replaced by lock-free note activity counters. `InstrumentTrackView` polls the snapshot on the existing GUI periodic update and synchronizes the activity button. The regression is red with the original product patch removed and green with the fix applied.

The mandatory ratchets still fail on unrelated repository entries. The complexity gate reports six over-target functions in other files; the file-length gate reports 19 files over 500 code lines, none among the changed files. Their baselines were left unchanged. The fix and its focused test are ready, but the gates prevent an unqualified FIXED verdict.

## File and line evidence

- `include/InstrumentTrack.h:376-385,425-426`: declares the GUI-polled snapshot and its lock-free count/generation fields.
- `src/tracks/InstrumentTrackNotes.cpp:53-68,279,296`: updates the counters from note-on/off and no longer emits `newNote()` or `endNote()` on the event path.
- `include/FadeButton.h:84` and `src/gui/tracks/FadeButton.cpp:87-101`: adds snapshot synchronization to the button.
- `src/gui/tracks/InstrumentTrackView.cpp:142-143,438-439`: initializes the snapshot and polls it from the existing periodic update.
- `tests/src/core/InstrumentRoutingTest.cpp:141-169`: drives a restored track’s note-on/off path on a worker thread and verifies no queued activity signal reaches the GUI receiver.

## Test and build evidence

| Evidence | Exact command | Exit | Result |
|---|---|---:|---|
| RED, product patch reversed | `cd build/tests && ctest -R InstrumentRoutingTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r1/logs/test-7.log 2>&1; result=$?; echo EXIT=$result` | 8 | The regression failed: `Actual (noteOns): 1`, `Expected (0)`. |
| GREEN, fixed patch restored | `cd build/tests && ctest -R InstrumentRoutingTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r1/logs/test-10.log 2>&1; result=$?; echo EXIT=$result` | 0 | CTest passed 1/1 registered family test. |
| Affected-family case count | `cd build/tests && ./InstrumentRoutingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r1/logs/test-11.log 2>&1; result=$?; echo EXIT=$result` | 0 | QtTest: 11 passed, 0 failed, 0 skipped. |
| Focused rebuild | `cmake --build build --target zene InstrumentRoutingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r1/logs/build-11.log 2>&1; result=$?; echo EXIT=$result` | 0 | Rebuilt `zene` and the affected test target. |
| Header-dependent targets | `cmake --build build --target zene lmmsobjs $target_list > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r1/logs/build-12.log 2>&1; result=$?; echo EXIT=$result` | 0 | `$target_list` was generated from sources including `InstrumentTrack.h` or `FadeButton.h`: 87 test targets and 30 plug-in targets. |

The RED control was repeated after reversing the final product patch: `build-8.log` exit 0, then `test-7.log` exit 8. The patch was reapplied with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r1/logs/fix.patch` (exit 0), followed by the focused rebuild and GREEN run above.

## Required gates

| Gate | Exact command | Exit | Evidence |
|---|---|---:|---|
| Complexity | `bash tests/complexity-gate.sh --check` | 1 | Six regressions are reported in `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, and `ControlCommandsShortcuts.cpp`. |
| File length | `bash tests/file-length-gate.sh --check` | 1 | 19 over-500-line files are reported; no changed source or test file is listed. |

Gate output is in `gate-1.log` and `gate-2.log`. Neither gate baseline or limit was changed.

## Files in this commit

- `include/FadeButton.h`
- `include/InstrumentTrack.h`
- `src/gui/tracks/FadeButton.cpp`
- `src/gui/tracks/InstrumentTrackView.cpp`
- `src/tracks/InstrumentTrackNotes.cpp`
- `tests/src/core/InstrumentRoutingTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-99.md`
