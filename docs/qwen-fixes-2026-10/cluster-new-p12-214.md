# Cluster new-p12-214: Tail trim can extend the timeline past the source

**Verdict: FIXED**

## The bug

`trimTailToFrame` in [src/core/SampleFrameEdits.cpp](../../src/core/SampleFrameEdits.cpp#L230)
computed `tailTick(*clip, frame)` from the caller's raw frame and only bounded the frame
to `sampleSize()` afterwards, when writing the window (`applyTail(..., std::min(frame, sampleSize), tail)`).
A frame past the source therefore ended the window at EOF while the clip's timeline length
was `tailTick(raw frame) - start` - the clip ran past its own audio. Both the check and the
applied length now use one frame bounded to `clip->sample().sampleSize()`
([SampleFrameEdits.cpp:233](../../src/core/SampleFrameEdits.cpp#L233) `at`), so the window and
the timeline stop together. For every frame inside the source the bound is the identity, so
in-range behaviour (including `checkOnly`) is unchanged.

Reachability: the in-tree callers (`control clip.trim` via `frameAtTimeline`, which clamps into
the clip's source, and `snapEdgeToZeroCrossing`) never pass a frame past `sampleSize()`.
The exported `arrangement::trimTailToFrame` API itself accepted such frames - it clamped the
window but not the length - which is the inconsistency the finding names and the test proves.

## Regression test

[tests/src/core/ArrangingCatchUpTest.cpp:225](../../tests/src/core/ArrangingCatchUpTest.cpp#L225)
`aTailTrimPastTheSourceEndsWhereTheSourceEnds` trims one fresh clip to its last frame as the
reference, then trims a second fresh clip with a frame 100 ticks past the source and requires
the same end tick. RED assertion (original code): `Actual (beyond->endPosition().getTicks()): 324,
Expected (endAtSource): 224` at `ArrangingCatchUpTest.cpp(238)` - the timeline ran 100 ticks past
the source. The test file is fork-NEW and already registered in `tests/fork-sources.txt`;
`src/core/SampleFrameEdits.cpp` is fork-NEW too (same manifest), so no manifest entry changed.

## Environment note (pre-existing, not part of this fix)

`build/plugins` contained no built plugin `.so`, so `ArrangingCatchUpTest::
queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie` failed at pristine HEAD
(`logs/test-3.log`, exit 8: plugin "tripleoscillator" not found). Building the existing
`tripleoscillator` target (build artefact only, no configure) made it pass; no repository
file changed for this.

## Verification evidence

The affected CTest family (`ctest -R 'Arranging|ArrangementModes|ControlVerbInverse|
ControlChordCommands|LinkedClip|ControlVcaLocked'`, run from `build/tests`) contains **8 tests**.
`ArrangingCatchUpTest` itself executes 13 slots (initTestCase, 11 test slots, cleanupTestCase).
All paths below are prefixed `LOGS=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-214.r1/logs`.

| Phase | Exact command | Exit |
|---|---|---:|
| Build with regression test, original product code | `cmake --build build --target zene ArrangingCatchUpTest > $LOGS/build-1.log 2>&1; echo EXIT=$?` | 0 |
| RED, first run (test + missing plugin artefact) | `ctest -R '^ArrangingCatchUpTest$' --output-on-failure > $LOGS/test-1.log 2>&1; echo EXIT=$?` (from `build/tests`) | 8 |
| Build missing plugin artefact | `cmake --build build --target tripleoscillator > $LOGS/build-3.log 2>&1; echo EXIT=$?` | 0 |
| Rebuild test after test-only revert/re-apply cycle | `cmake --build build --target ArrangingCatchUpTest > $LOGS/build-4.log 2>&1; echo EXIT=$?` | 0 |
| RED on original product code (only this test fails) | `ctest -R '^ArrangingCatchUpTest$' --output-on-failure > $LOGS/test-4.log 2>&1; echo EXIT=$?` | 8 |
| Build with fix | `cmake --build build --target zene ArrangingCatchUpTest > $LOGS/build-5.log 2>&1; echo EXIT=$?` | 0 |
| GREEN with fix | `ctest -R '^ArrangingCatchUpTest$' --output-on-failure > $LOGS/test-5.log 2>&1; echo EXIT=$?` | 0 |
| Build the other family test binaries | `cmake --build build --target LinkedClipEditsTest LinkedClipGroupEditsTest ControlChordCommandsTest ControlVerbInverseTest ControlVcaLockedEditsTest ArrangementModesTest ArrangingSpacerReferenceTest > $LOGS/build-6.log 2>&1; echo EXIT=$?` | 0 |
| GREEN, whole affected family (8/8) | `ctest -R 'Arranging\|ArrangementModes\|ControlVerbInverse\|ControlChordCommands\|LinkedClip\|ControlVcaLocked' --output-on-failure > $LOGS/test-7.log 2>&1; echo EXIT=$?` | 0 |
| Save fix and reverse it | `git diff -- src/core/SampleFrameEdits.cpp > $LOGS/fix.patch && git apply -R $LOGS/fix.patch; echo EXIT=$?` | 0 |
| Rebuild without fix | `cmake --build build --target zene ArrangingCatchUpTest > $LOGS/build-7.log 2>&1; echo EXIT=$?` | 0 |
| RED after reversing only the product patch | `ctest -R '^ArrangingCatchUpTest$' --output-on-failure > $LOGS/test-8.log 2>&1; echo EXIT=$?` | 8 |
| Reapply saved fix | `git apply $LOGS/fix.patch; echo EXIT=$?` | 0 |
| Rebuild with restored fix | `cmake --build build --target zene ArrangingCatchUpTest > $LOGS/build-8.log 2>&1; echo EXIT=$?` | 0 |
| GREEN after restoring fix | `ctest -R '^ArrangingCatchUpTest$' --output-on-failure > $LOGS/test-9.log 2>&1; echo EXIT=$?` | 0 |
| Final rebuild of current tree | `cmake --build build --target zene ArrangingCatchUpTest > $LOGS/build-9.log 2>&1; echo EXIT=$?` | 0 |
| Final family run (8/8) | `ctest -R 'Arranging\|ArrangementModes\|ControlVerbInverse\|ControlChordCommands\|LinkedClip\|ControlVcaLocked' --output-on-failure > $LOGS/test-10.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate, pristine HEAD (all lane changes reverted) | `bash tests/complexity-gate.sh --check > $LOGS/gate-baseline-1.log 2>&1; echo EXIT=$?` | 1 |
| File-length gate, pristine HEAD | `bash tests/file-length-gate.sh --check > $LOGS/gate-baseline-2.log 2>&1; echo EXIT=$?` | 1 |
| Complexity gate after final edit | `bash tests/complexity-gate.sh --check > $LOGS/gate-1.log 2>&1; echo EXIT=$?` | 1 |
| File-length gate after final edit | `bash tests/file-length-gate.sh --check > $LOGS/gate-2.log 2>&1; echo EXIT=$?` | 1 |

The RED assertion was identical in `test-1.log`, `test-4.log` and `test-8.log`
(`Compared values are not the same ... 324 vs 224, Loc: tests/src/core/ArrangingCatchUpTest.cpp(238)`),
and the test passed only when the fix was in the tree (`test-5.log`, `test-9.log`, `test-10.log`).

**Gate status: both ratchets fail at pristine HEAD on this machine, before and after the fix, with
byte-identical regression sets** (`diff` of the `REGRESSION` lines of `gate-baseline-1.log` vs
`gate-1.log` and of `gate-baseline-2.log` vs `gate-2.log` is empty: 6 complexity entries and 14
file-length entries). None of those entries name `src/core/SampleFrameEdits.cpp` or
`tests/src/core/ArrangingCatchUpTest.cpp`, and neither file is over either limit
(264 and 353 lines; both new functions well under CCN 10). The failures are pre-existing lane
debt in unrelated files (`SmfInterchangeReader.cpp`, `TakeLane.cpp`, `DeviceDspTest.cpp`, ...);
no baseline was edited and no gate was re-anchored, per the rules. Fixing 20 unrelated files
is out of scope for this single-bug cluster and would violate "change only the buggy behaviour".

## Files committed

- `src/core/SampleFrameEdits.cpp`
- `tests/src/core/ArrangingCatchUpTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-214.md`
