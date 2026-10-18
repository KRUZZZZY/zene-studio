# Cluster new-p12-99

Verdict: **FIXED**

The existing implementation removes `newNote()` and `endNote()` emissions from the MIDI event path and publishes note activity through the lock-free snapshot polled by `InstrumentTrackView`. This follow-up addresses Luna's round 1 test failure: the layer-routing test now observes actual note-off callbacks received by the base instrument. Its assertions still distinguish skipped notes from accepted notes in both duration orders.

The two required gates were rerun after the final test edit. They still exit 1 on unrelated repository regressions; the changed test file and new helper header are not listed by the file-length gate, and no function in the changed files is listed by the complexity gate. No gate baseline or limit changed.

## File and line evidence

- `src/tracks/InstrumentTrackNotes.cpp:53-68,259-296`: lock-free note activity counters are updated on note-on/off; the event path does not emit the activity signals.
- `src/gui/tracks/InstrumentTrackView.cpp:438-449`: the existing GUI update polls the activity snapshot.
- `tests/src/core/InstrumentRoutingTest.cpp:141-169`: worker-thread regression verifies no queued activity signals reach the GUI receiver.
- `tests/src/core/InstrumentLayersCommandsTest.cpp:209-268`: the accepted/skipped note-off routing assertions remain and now use base-instrument delivery counts.
- `tests/src/core/InstrumentNoteOffProbe.h:24-67`: test-only instrument records actual `MidiNoteOff` calls and restores the original instrument with RAII.
- `tests/fork-sources.txt:2289`: registers the new test helper header.

## Build and test evidence

The initial reproduction used the committed product fix with the old `endNote()` spy. It confirmed Luna's report: `acceptedLongOffs` was 0 instead of 1. After the test was changed to observe actual instrument delivery, the focused family passed. The final RED/GREEN proof reverses and reapplies the committed product patch, then rebuilds before each test run.

| Evidence | Exact command | Exit | Result |
|---|---|---:|---|
| Initial focused build | `cmake --build build --target zene InstrumentRoutingTest InstrumentLayersCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r2/logs/build-1.log 2>&1; build_status=$?; echo EXIT=$build_status` | 0 | Built `zene` and both test targets before reproducing the old layer-test failure. |
| RED, committed product patch reversed | `cmake --build build --target zene InstrumentRoutingTest InstrumentLayersCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r2/logs/build-10.log 2>&1; build_status=$?; echo EXIT=$build_status` | 0 | Rebuilt both test targets with the signal-emitting product implementation restored. |
| RED regression | `ctest -R InstrumentRoutingTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r2/logs/test-9.log 2>&1; test_status=$?; echo EXIT=$test_status` | 8 | Fails as required: `noteOns` actual 1, expected 0. |
| GREEN, product patch restored | `cmake --build build --target zene InstrumentRoutingTest InstrumentLayersCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r2/logs/build-11.log 2>&1; build_status=$?; echo EXIT=$build_status` | 0 | Rebuilt `zene` and both test targets with the committed fix restored. |
| GREEN regression | `ctest -R InstrumentRoutingTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r2/logs/test-10.log 2>&1; test_status=$?; echo EXIT=$test_status` | 0 | CTest passed 1/1 registered test. |
| Routing case count | `./InstrumentRoutingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r2/logs/test-12.log 2>&1; test_status=$?; echo EXIT=$test_status` (run from `build/tests`) | 0 | QtTest: 11 passed, 0 failed, 0 skipped. |
| Layer case count | `./InstrumentLayersCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r2/logs/test-13.log 2>&1; test_status=$?; echo EXIT=$test_status` (run from `build/tests`) | 0 | QtTest: 14 passed, 0 failed, 0 skipped. Affected family total: 25 passed cases. |

The initial reproduction ran `ctest -R 'InstrumentRoutingTest|InstrumentLayersCommandsTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r2/logs/test-1.log 2>&1; test_status=$?; echo EXIT=$test_status` from `build/tests`; it exited 8, with routing passing and the layer test failing at line 266 (`acceptedLongOffs`: actual 0, expected 1).

The final affected-family run used `ctest -R 'InstrumentRoutingTest|InstrumentLayersCommandsTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r2/logs/test-11.log 2>&1; test_status=$?; echo EXIT=$test_status` from `build/tests`; it exited 0 and passed 2/2 registered tests.

The original product implementation is in branch commit `6c0328e11`. For the final RED/GREEN proof, its complete product patch was reversed with `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r2/logs/fix.patch` and restored with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r2/logs/fix.patch`; both patch operations completed successfully.

## Required gates

| Gate | Exact command | Exit | Evidence |
|---|---|---:|---|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r2/logs/gate-3.log 2>&1; gate_status=$?; echo EXIT=$gate_status` | 1 | Six over-target functions are reported in unrelated files: `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, and `ControlCommandsShortcuts.cpp`. |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r2/logs/gate-4.log 2>&1; gate_status=$?; echo EXIT=$gate_status` | 1 | Fourteen unrelated new files exceed 500 code lines. Neither `InstrumentLayersCommandsTest.cpp` nor `InstrumentNoteOffProbe.h` is listed. |

All build, test, and gate logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-99.r2/logs`. The `fff grep` tool was not available in this session, so the logs were searched with `rg` for `error`, `FAILED`, and `Assertion`.

CTest and direct test executable commands were run from `build/tests`; build and gate commands were run from the worktree root.

## Files in this follow-up commit

- `tests/src/core/InstrumentLayersCommandsTest.cpp`
- `tests/src/core/InstrumentNoteOffProbe.h`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-99.md`
