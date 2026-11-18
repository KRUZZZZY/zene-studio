# Cluster new-p10-429-port

Verdict: **FIXED**

`TimePos::fromFrames()` divided the frame count by an unvalidated frame rate and
converted the quotient to an `int` without a range check. A rate of zero (the
division yields NaN or infinity) or a quotient that no `int` can hold made the
float-to-int conversion undefined behaviour. The function now rejects a rate that
is zero, negative or not a number, and a quotient outside the range of an `int`,
and reports "no position" (0 ticks) - the same answer the inverse direction
`TimePos::frames()` already gives for an invalid rate.

Evidence:

- [include/TimePos.h:135](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-429-port/include/TimePos.h:135) -
  the fixed `fromFrames()`; the guard for the rate is at
  [include/TimePos.h:137](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-429-port/include/TimePos.h:137)
  and the range check for the quotient at
  [include/TimePos.h:140](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-429-port/include/TimePos.h:140).
  Before the fix the whole body was one line,
  `return TimePos(static_cast<int>(frames / framesPerTick));` (the finding's
  `include/TimePos.h:124` in the historical tree; the same statement stood at
  `include/TimePos.h:132` in this tree).
- The inverse direction, [include/TimePos.h:112](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-429-port/include/TimePos.h:112),
  already validates the rate (`!std::isfinite(framesPerTick) || framesPerTick < 0.0f`)
  and bounds the conversion - cluster new-p10-426. `fromFrames()` now mirrors it.
- The rate producers are not guaranteed to be positive:
  [include/Engine.h:98](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-429-port/include/Engine.h:98)
  returns the global atomic that
  [src/core/Engine.cpp:144](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-429-port/src/core/Engine.cpp:144)
  fills - it is 0 until the first tempo or sample-rate update - and
  [src/core/SampleClip.cpp:528](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-429-port/src/core/SampleClip.cpp:528)
  (`clipFramesPerTick()`) is 0 for a clip whose sample rate is 0. The sibling
  paths of that clip already guard exactly that case before the tick-to-frame
  call ([src/core/SampleClip.cpp:497](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-429-port/src/core/SampleClip.cpp:497),
  `if (framesPerTick <= 0.0) { return 0; }`), while the `fromFrames()` path at
  [src/core/SampleClip.cpp:745](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-429-port/src/core/SampleClip.cpp:745)
  had no guard.
- The regression lives in the file the sibling fix of cluster new-p10-426 uses:
  [tests/src/core/MidiLearnTest.cpp:104](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-429-port/tests/src/core/MidiLearnTest.cpp:104)
  (invalid rate), line 114 (a frame count no tick count can hold) and line 122
  (the valid rate keeps its tick position).
- [tests/upstream-modifications.txt:2409](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-429-port/tests/upstream-modifications.txt:2409)
  registers the inherited header change.

## Verification evidence

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-429-port.r2/logs`.

| Phase | Command (working directory) | Exit | Result / log |
|---|---|---:|---|
| Baseline build | `cmake --build build --target MidiLearnTest > .../logs/build-1.log 2>&1; status=$?; echo EXIT=$status` (worktree root) | 0 | `build-1.log` |
| RED before fix | `ctest -R '^MidiLearnTest$' --output-on-failure > .../logs/test-1.log 2>&1; status=$?; echo EXIT=$status` (`build/tests`) | 8 | `InvalidFramesPerTickReturnsNoPosition` and `UnrepresentableFrameCountReturnsNoPosition` failed: actual `-2147483648`, expected `0`; `test-1.log` |
| Build after the fix | `cmake --build build --target zene MidiLearnTest MidiLearnThreadTest ClipEditsMenuTest > .../logs/build-2.log 2>&1; status=$?; echo EXIT=$status` (worktree root) | 0 | `build-2.log` |
| GREEN after fix | `ctest -R '^MidiLearnTest$' --output-on-failure > .../logs/test-2.log 2>&1; status=$?; echo EXIT=$status` (`build/tests`) | 0 | 1/1 CTest passed; `test-2.log` |
| Rebuild of every target that includes the changed header | `cmake --build build --target AutomationThinTest ClipEditsMenuTest MidiLearnTest MidiLearnThreadTest Vst3InstrumentIntegrationTest MpePlaybackTest ControlDeviceCatalogueTest > .../logs/build-4.log 2>&1; status=$?; echo EXIT=$status` (worktree root) | 0 | 150 objects rebuilt; `build-4.log` |
| Affected family | `ctest -R '^(AutomationThinTest\|ClipEditsMenuTest\|MidiLearnTest\|MidiLearnThreadTest\|Vst3InstrumentIntegrationTest\|MpePlaybackTest\|ControlDeviceCatalogueTest)$' --output-on-failure > .../logs/test-3.log 2>&1; status=$?; echo EXIT=$status` (`build/tests`) | 8 | 6/7 passed; only `ControlDeviceCatalogueTest` failed, with the two assertions it also fails with the fix reverted (pre-existing, see below); `test-3.log` |
| Reverse product patch | `git diff -- include/TimePos.h > .../logs/fix.patch && git apply -R .../logs/fix.patch; status=$?; echo EXIT=$status` (worktree root) | 0 | `fix.patch` holds only the product header diff (28 lines) |
| Reversed build | `cmake --build build --target MidiLearnTest ControlDeviceCatalogueTest > .../logs/build-5.log 2>&1; status=$?; echo EXIT=$status` (worktree root) | 0 | `build-5.log` |
| RED with the fix removed | `ctest -R '^(MidiLearnTest\|ControlDeviceCatalogueTest)$' --output-on-failure > .../logs/test-4.log 2>&1; status=$?; echo EXIT=$status` (`build/tests`) | 8 | `MidiLearnTest` failed with the same two assertions (actual `-2147483648`); `ControlDeviceCatalogueTest` failed with the same two assertions as with the fix, so they are pre-existing; `test-4.log` |
| Reapply product patch | `git apply .../logs/fix.patch; status=$?; echo EXIT=$status` (worktree root) | 0 | restored the saved header diff |
| Final rebuild | `cmake --build build --target zene MidiLearnTest AutomationThinTest ClipEditsMenuTest MidiLearnThreadTest Vst3InstrumentIntegrationTest MpePlaybackTest ControlDeviceCatalogueTest > .../logs/build-6.log 2>&1; status=$?; echo EXIT=$status` (worktree root) | 0 | `build-6.log` |
| GREEN after fix | `ctest -R '^MidiLearnTest$' --output-on-failure > .../logs/test-5.log 2>&1; status=$?; echo EXIT=$status` (`build/tests`) | 0 | 1/1 CTest passed; `test-5.log` |
| Affected family (after the fix) | `ctest -R '^(AutomationThinTest\|ClipEditsMenuTest\|MidiLearnTest\|MidiLearnThreadTest\|Vst3InstrumentIntegrationTest\|MpePlaybackTest\|ControlDeviceCatalogueTest)$' --output-on-failure > .../logs/test-6.log 2>&1; status=$?; echo EXIT=$status` (`build/tests`) | 8 | 6/7 passed, only the pre-existing `ControlDeviceCatalogueTest`; `test-6.log` |
| Proof round on the final patch - reverse | `git diff -- include/TimePos.h > .../logs/fix.patch && git apply -R .../logs/fix.patch; status=$?; echo EXIT=$status` (worktree root) | 0 | `fix.patch` regenerated from the final diff |
| Proof round - reversed build | `cmake --build build --target MidiLearnTest > .../logs/build-8.log 2>&1; status=$?; echo EXIT=$status` (worktree root) | 0 | `build-8.log` |
| Proof round - RED with the fix removed | `ctest -R '^MidiLearnTest$' --output-on-failure > .../logs/test-9.log 2>&1; status=$?; echo EXIT=$status` (`build/tests`) | 8 | 11 passed, 2 failed - the two new assertions, actual `-2147483648`; `test-9.log` |
| Proof round - reapply | `git apply .../logs/fix.patch; status=$?; echo EXIT=$status` (worktree root) | 0 | restored the saved header diff |
| Proof round - final rebuild | `cmake --build build --target zene MidiLearnTest AutomationThinTest ClipEditsMenuTest MidiLearnThreadTest Vst3InstrumentIntegrationTest MpePlaybackTest ControlDeviceCatalogueTest > .../logs/build-9.log 2>&1; status=$?; echo EXIT=$status` (worktree root) | 0 | `build-9.log` |
| Proof round - GREEN | `ctest -R '^MidiLearnTest$' --output-on-failure > .../logs/test-10.log 2>&1; status=$?; echo EXIT=$status` (`build/tests`) | 0 | 1/1 CTest passed; `test-10.log` |
| Proof round - affected family | `ctest -R '^(AutomationThinTest\|ClipEditsMenuTest\|MidiLearnTest\|MidiLearnThreadTest\|Vst3InstrumentIntegrationTest\|MpePlaybackTest\|ControlDeviceCatalogueTest)$' --output-on-failure > .../logs/test-11.log 2>&1; status=$?; echo EXIT=$status` (`build/tests`) | 8 | 6/7 passed, only the pre-existing `ControlDeviceCatalogueTest`; `test-11.log` |
| Registration gate | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gate-2.log 2>&1; status=$?; echo EXIT=$status` (worktree root) | 0 | 8/8 gates PASS; `gate-2.log` |
| Complexity ratchet | `bash tests/complexity-gate.sh --check > .../logs/gate-3.log 2>&1; status=$?; echo EXIT=$status` (worktree root) | 1 | Pre-existing red of the train: the log names six new functions over the target (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp`) and none of them is in `include/TimePos.h` or `tests/src/core/MidiLearnTest.cpp` (`feature_check.py` reports "complexity: no new item in your files"); `gate-3.log` |
| File-length ratchet | `bash tests/file-length-gate.sh --check > .../logs/gate-4.log 2>&1; status=$?; echo EXIT=$status` (worktree root) | 1 | Pre-existing red of the train: the log names four new test files over 500 lines (`RackMacrosTest.cpp`, `RevisionTimelineTest.cpp`, `VideoTrackTest.cpp`, `ArrangerTimelineTest.cpp`) and none of them is mine (`feature_check.py` reports "file-length: no new item in your files"); `gate-4.log` |

A full `cmake --build build` is refused by the tree's own guard
(`bunny-guard: build a named target ...`, `build-3.log`, exit 97), so every
target that includes the changed header was built by name: `zene` plus the seven
test targets of the affected family.

## Self-check

**The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
With `git apply -R fix.patch` (the header back to `return TimePos(static_cast<int>(frames / framesPerTick));`)
`ctest -R '^MidiLearnTest$'` exits 8 with `11 passed, 2 failed`:
`InvalidFramesPerTickReturnsNoPosition` (actual `-2147483648`, expected `0`) and
`UnrepresentableFrameCountReturnsNoPosition` (actual `-2147483648`, expected `0`)
- `test-9.log`. With the patch applied the same command exits 0, 1/1 CTest passed
- `test-10.log`. Both runs used the same binary path and the same test file; only
the product header differed.

**The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).**
The new test slots in `tests/src/core/MidiLearnTest.cpp` call
`TimePos::fromFrames()` with literal arguments only - the seam is the public
static `TimePos::fromFrames(f_cnt_t, float)` itself, and the injected values are
the rate (`0.0f`, `-1.0f`, `quiet_NaN()`, `infinity()`, `1.0f`, `4.0f`) and the
frame count (`1024`, `std::numeric_limits<f_cnt_t>::max()`, `192`, `0`). No clock,
no timer, no sleep, no thread and no shared state is involved; the two failing
assertions in the RED run are pure function results.

**The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.**
- Sibling: the inverse `TimePos::frames()` (`include/TimePos.h:112`) already
  rejects a negative or non-finite rate and an out-of-range product; the fixed
  `fromFrames()` now answers "no position" in the same way, so both directions
  agree on what an invalid rate means.
- Callers of `fromFrames()` - all of them checked, none passes a rate that the
  new check would reject in normal operation:
  [src/tracks/InstrumentTrackNotes.cpp:167](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-429-port/src/tracks/InstrumentTrackNotes.cpp:167)
  (`Engine::framesPerTick()`),
  [src/core/SampleClip.cpp:745](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-429-port/src/core/SampleClip.cpp:745)
  (`clipFramesPerTick()`),
  [src/core/NotePlayHandle.cpp:272](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-429-port/src/core/NotePlayHandle.cpp:272),
  [src/core/NotePlayHandle.cpp:278](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-429-port/src/core/NotePlayHandle.cpp:278)
  and
  [src/core/NotePlayHandle.cpp:441](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-429-port/src/core/NotePlayHandle.cpp:441)
  (`Engine::framesPerTick()`), and
  [plugins/Vst3Instrument/Vst3Instrument.cpp:447](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-429-port/plugins/Vst3Instrument/Vst3Instrument.cpp:447)
  (`Engine::framesPerTick()`). For these callers a rate of 0 was undefined
  behaviour before, so the new "no position" answer can only replace a garbage
  value.
- The other path into the same conversion, the tick-to-frame one, is the sibling
  `frames()` and was already guarded by cluster new-p10-426; the tests that use
  it (`NegativeTickLengthReturnsZeroFrames`,
  `NegativeFramesPerTickReturnsZeroFrames`) still pass.
- The "wider type the value flows into" is the `int` of the tick count: the
  quotient is now bounded by `std::numeric_limits<int>::max()` before the
  conversion, so the float-to-int conversion can no longer be undefined.

**No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.**
The change touches no file format, no setting and no saved data - it only
converts a frame count to a tick position at runtime. For a valid rate the
arithmetic is bit-identical to before: the quotient is still computed in `float`
(`frames / framesPerTick`, the `f_cnt_t` converted to `float` exactly as before)
and still truncated towards zero, so every legitimate caller lands on the same
tick position as before - the positive case
`ValidFramesPerTickKeepsTheTickPosition` (192 frames at 4 frames per tick is
48 ticks) pins that. The new checks only reject inputs that produced undefined
behaviour before: a rate that is zero, negative or not a number, and a quotient
outside the range of an `int` (which needs more than 2^31 frames at one frame
per tick - about 12 hours of audio at 48 kHz - so no real caller reaches it).
The affected family (`AutomationThinTest`, `ClipEditsMenuTest`, `MidiLearnTest`,
`MidiLearnThreadTest`, `Vst3InstrumentIntegrationTest`, `MpePlaybackTest`) is
unchanged by this; `ControlDeviceCatalogueTest` fails with the two assertions
`aTruncatedPatchKeepsThePreviouslyLoadedSound` (the `patman` plugin is
unavailable in this build) and `builtinDevicesAreListedWithTheirCatalogueFields`
(the build lists no built-in device) both with and without the fix - the same
two failures appear in `test-3.log` (fix applied) and `test-4.log` (fix
reverted), so they are pre-existing and unrelated.

**If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.**
Not applicable - the verdict is FIXED. The defect was in the tree before this
commit: the whole body of `TimePos::fromFrames()` was
`return TimePos(static_cast<int>(frames / framesPerTick));` (the finding's
`include/TimePos.h:124`, the same statement at `include/TimePos.h:132` in this
tree), with no check on the rate and no bound on the quotient. The fix is at
`include/TimePos.h:135-146`.

## Notes

- No new control command was added, so the `ControlCommandsSnapshot` canary did
  not move.
- `fromFrames()` is reached from the audio thread
  (`NotePlayHandle::playNote`/`noteOff`); the fix adds two float comparisons and
  an early return - no allocation, no free, no lock, no wait and no logging on
  the audio thread.
- No undo step is involved: the change is a pure computation, not a command.

## Files committed

- `include/TimePos.h`
- `tests/src/core/MidiLearnTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-429-port.md`
