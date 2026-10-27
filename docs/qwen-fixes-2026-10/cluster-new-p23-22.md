# Cluster new-p23-22 — landRecoveredTake ignores clip and sample-file failures

Verdict: **FIXED** on this branch (`bugs/qwen-new-p23-22`).

## Finding

Luna BUG 12: `landRecoveredTake` (`src/core/ControlCommandsRecordingPlace.cpp`, finding line 62) does not check the
clip creation or the sample-file assignment. A track is reported as placed (`record.recovery_place` returns
`placed_track`) without the recovered audio in a clip.

## Evidence (current tree)

Reproduced, not only read. The finding is real, and a second defect sits in the same failure path.

1. **Sample file never checked.** `SampleClip::setSampleFile` returns `void` (`include/SampleClip.h:265`). For a
   file no decoder reads, `SampleBuffer::fromFile` returns the shared empty buffer (`src/core/SampleBuffer.cpp:105`),
   so the clip keeps no file name (`src/core/SampleClip.cpp:468-496`, `sampleFile()` at `:165-168`). The old
   `landRecoveredTake` returned the track id anyway. The take is found by the header
   (`src/core/RecordingJournal.cpp:322-340`, `frames_recoverable` > 0), so the placement is reached, and every decoder
   refuses the file: libsndfile (`src/core/SampleDecoder.cpp:66-70`), libvorbisfile, DS.
2. **Phantom undo step on a failed first placement.** `addUndoStep` pushes the step at once
   (`src/core/ControlReversibility.cpp:147-151` → `ProjectJournal::addJournalAction`, `src/core/ProjectJournal.cpp:277-290`).
   The old `recoveryPlace` recorded the step before the first landing (old lines 106-127), so a failed first placement
   left an undo step that removes nothing. Seen in the test as undo depth 3 where 2 is expected.
3. **Clip creation not checked.** `Track::createClip` (`src/core/ControlCommandsRecordingPlace.cpp`, old line 67) was
   cast and used without a null check. For a sample track `SampleTrack::createClip` always returns a new clip
   (`src/tracks/SampleTrack.cpp:324-328`), so this branch is not reachable through production code. It shares the
   cleanup path below and is covered by inspection, not by a test (see Self-check).

### The change

- `src/core/ControlCommandsRecordingPlace.cpp:62-77` (`landRecoveredTake`): the clip is created and the file set; if the
  clip is missing, or `clip->sampleFile()` is empty after the load, the track is removed with `removeTrack` and the
  function returns an empty id. The track id is returned only when the clip holds the file.
- `src/core/ControlCommandsRecordingPlace.cpp:114-119`: the first placement runs before the undo step is recorded. Its
  failure returns the same refusal as before (`could not create a sample track`) and rewrites the journal to offered.
- `src/core/ControlCommandsRecordingPlace.cpp:120-142`: `addUndoStep` moved after that check, unchanged otherwise. The
  redo lambda (`:130`) keeps its own failure path (decline the redo).

Callers of `landRecoveredTake` (grep): `:114` (first placement) and `:130` (redo). Both get the cleanup.

### Test

`tests/src/gui/RecordingRecoveryMenuTest.cpp` (the closest existing test of `record.recovery_place`; it already held the
seam test for the redo):

- `aTakeThatDoesNotDecodeIsNotPlaced` (new): writes a take whose fmt chunk declares 0 bits per sample. The header still
  measures `kFrames` frames, and the test first asserts `SampleDecoder::decode(take)` returns nothing, so the premise is
  checked. The placement must be refused, typed `Refused`, with no track added, no undo step, and the journal in
  `in_progress`.
- `aPlaceThatCannotLandRecordsNoUndoStep` (new): uses the existing seam `control::setRecoveredTakeFailsForTest` around
  the first placement only, and resets it before any assertion. The placement must be refused, with no track and no
  undo step added.
- `writeWav` gained a `bitsPerSample` parameter with default `16`; existing callers are unchanged.

## Commands and exit codes

Build cache: ccache's configured `temporary_dir` is not writable from this sandbox, so every build below sets
`CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-p23-22`. The first attempt without it failed (EXIT=2, `build-1-red.log`,
ccache "Permission denied"), and nothing was compiled.

| Step | Command | EXIT | Log (LOG folder) |
| --- | --- | --- | --- |
| RED build | `cmake --build build --target RecordingRecoveryMenuTest -j4` | 0 | `build-2-red.log` |
| **RED test** | `cd build/tests && ctest -R RecordingRecoveryMenuTest --output-on-failure` | **8** | `test-1-red.log` |
| GREEN build | `cmake --build build --target RecordingRecoveryMenuTest -j4` | 0 | `build-3-green.log` |
| **GREEN test** | `cd build/tests && ctest -R RecordingRecoveryMenuTest` | **0** | `test-2-green.log` |
| GREEN verbose | `cd build/tests && ctest -R RecordingRecoveryMenuTest -V` | 0 (13 passed) | `test-3-green-verbose.log` |
| zene build | `cmake --build build --target zene -j4` | 0 | `build-4-zene.log` |
| Build of 8 unbuilt family targets | `cmake --build build --target ControlArmReversibilityTest ControlSessionRecordLandTest ProjectRecoveryTest RecordingCrashLossTest ReversibilityContractTest ReversibilityUndoTest ReversibilityUndoDeviceTest ControlSessionRecordLandUndoTest -j4` | 0 | `build-5-family.log` |

**Reverse proof (step 4b)**, product file only:
`git diff -- src/core/ControlCommandsRecordingPlace.cpp > logs/fix.patch` (EXIT 0), then `git apply -R logs/fix.patch`
(EXIT 0), then `cmake --build build --target zene RecordingRecoveryMenuTest ReversibilityUndoDeviceTest ControlReversibilityTranscript -j4`
(EXIT 2: `ControlReversibilityTranscript` is a Python test, not a build target, so make stopped at that name; the log
still shows `ControlCommandsRecordingPlace.cpp.o` compiled and `RecordingRecoveryMenuTest` relinked, `build-6-reverted.log`
lines 13 and 33-34), then `cd build/tests && ctest -R RecordingRecoveryMenuTest -V` (**EXIT 8**, `test-8-RED-reverted.log`):
`aTakeThatDoesNotDecodeIsNotPlaced` fails at `tests/src/gui/RecordingRecoveryMenuTest.cpp:484` ('!placed.ok' returned
FALSE) and `aPlaceThatCannotLandRecordsNoUndoStep` fails at `:517` (undo depth 3, expected 2). The other 11 slots pass.
Then `cmake --build build --target zene -j4` (EXIT 0, `build-7-reverted-zene.log`) and the LADSPA-dependent tests on the
original code: `ctest -R "ReversibilityUndoDeviceTest|ControlReversibilityTranscript"` (**EXIT 8**, `test-9-reverted-ladspa.log`).
Then `git apply logs/fix.patch` (EXIT 0), `cmake --build build --target zene RecordingRecoveryMenuTest ReversibilityUndoDeviceTest -j4`
(EXIT 0, `build-8-fixed.log`, `ControlCommandsRecordingPlace.cpp.o` recompiled), and
`cd build/tests && ctest -R RecordingRecoveryMenuTest -V` (**EXIT 0**, `test-10-GREEN-reapplied.log`, 13 passed).

**Affected family** (`cd build/tests && ctest -R "RecordingRecoveryMenuTest|ControlRecordingRecovery|ProjectRecoveryTest|Reversib|ControlSessionRecordLand|ControlRecordInputs|ControlDryRunJournal|RecordingCrashLossTest"`,
EXIT 8, `test-11-family-final.log`): 13 tests, 11 passed, 2 failed. The two failures are
`ReversibilityUndoDeviceTest` and `ControlReversibilityTranscript`, both with
`the LADSPA host plugin 'ladspaeffect' is not in this build`. They fail identically on the original code
(`test-9-reverted-ladspa.log`), so they are pre-existing and environmental. They do not touch recording placement.
Passing: `RecordingRecoveryMenuTest`, `ControlRecordingRecovery` (live-instance Python test of `record.recovery_place`),
`ControlRecordInputs`, `ControlDryRunJournal`, `ProjectRecoveryTest`, `RecordingCrashLossTest`, `ControlArmReversibilityTest`,
`ControlSessionRecordLandTest`, `ControlSessionRecordLandUndoTest`, `ReversibilityContractTest`, `ReversibilityUndoTest`.

**Gates** (see Self-check for why `feature_check.py` could not run as written):

| Gate | Result | Evidence |
| --- | --- | --- |
| fork-sources | PASS (EXIT 0) | `gate-logs/fork-sources.log` |
| all-sources-reproduce | PASS (EXIT 0) | `gate-logs/all-sources-reproduce.log` |
| no-upstream-regression | PASS (EXIT 0) | `gate-logs/no-upstream-regression.log` |
| check-namespace | PASS (EXIT 0) | `gate-logs/check-namespace.log` |
| no-tautology | PASS (EXIT 0) | `gate-logs/no-tautology.log` |
| doc-figures | PASS (EXIT 0) | `gate-logs/doc-figures.log` |
| complexity (gate 4) | NOT COMPARED to baseline. Raw run EXIT 1: 6 REGRESSION lines, none in a changed file | `gate-logs/complexity-raw.log` |
| file-length (gate 7) | NOT COMPARED to baseline. Raw run EXIT 1: 22 REGRESSION lines, none in a changed file | `gate-logs/file-length-raw.log` |

## Files committed

- `src/core/ControlCommandsRecordingPlace.cpp`
- `tests/src/gui/RecordingRecoveryMenuTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p23-22.md`

No new source files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` need no change.

## Not changed (siblings with the same pattern)

Found while checking siblings. Each is a different function and would need its own finding and test:

- `src/core/ControlCommandsBounceLand.cpp:49-56` (`landTrack`): `setSampleFile` unchecked. Its `addUndoStep` (`:85`) also
  comes before the first landing (`:97`), so it has the same phantom-undo shape.
- `src/core/ControlCommandsBrowserMedia.cpp:284` (`loadAsSampleTrack`): unchecked `setSampleFile`.
- `src/core/ControlCommandsConsolidate.cpp:177-178`: `createClip` result dereferenced without a check.
- `src/core/AafImportBuild.cpp:120-122`: `made` dereferenced without a null check.

## Self-check

Items from the completion check (`submit_check.py`), each answered:

1. **The regression fails with the product change reverted and passes with it.** RED: `test-1-red.log` (EXIT 8, 2 FAIL)
   and `test-8-RED-reverted.log` (EXIT 8, same 2 FAIL). GREEN: `test-2-green.log` and `test-10-GREEN-reapplied.log`
   (EXIT 0, 13 passed).
2. **Deterministic.** No clocks, sleeps or threads. The take bytes are fixed (`writeWav` with 0 bits per sample for the
   undecodable case). The first-placement failure uses the existing synchronous seam `control::setRecoveredTakeFailsForTest`,
   and the test resets it before asserting. The decode precondition is checked, not assumed.
3. **Fix at the cause and complete.** The cause is the unchecked clip and file load in `landRecoveredTake`, with the
   cleanup and the undo ordering in the same failure path. Both callers (`:114`, `:130`) were checked. Siblings are listed
   above and not changed. The clip-null branch (`:71`) is not reachable through `SampleTrack::createClip`
   (`src/tracks/SampleTrack.cpp:324-328`), so it is covered by code reading, not by a test. A test would need a new seam
   on `SampleTrack`, which I did not add.
4. **No collateral.** The change is inside `record.recovery_place` only. Project load, `SampleClip` load and the
   journal file format are untouched. Behaviour changes in one case: a take that no decoder reads was previously placed as
   an empty track and is now refused, staying offered in the journal (the same outcome as the existing first-placement
   failure). No limit or check was added. Other tests in the family pass (see the family table).
5. **Verdict is FIXED,** so the ALREADY-FIXED / FALSE-POSITIVE item does not apply.

**Gate runs.** `feature_check.py` as written exits 1 before running any gate: it creates
`<worktree>-feature-check/` as a sibling of the worktree (`PermissionError`; the log folder was not created, see
`gate-1-feature-check.log`). A copy with only the log folder changed, kept under the LOG folder, exits 1 because
`bunny-guard` blocks the `git checkout` that builds the base baseline in a Bunny lane ("not allowed in a Bunny lane
(AGENTS.md rules 1, 2, 11, 12)", `gate-2-feature-check.log`). I did not work around the guard. I ran the six
non-ratchet gate commands and the two ratchet commands directly in the worktree (table above). The ratchets' baseline
comparison is not done. The raw REGRESSION lines name no file this change touches, and `feature_check.py` only fails
items in changed files. This gate still needs a run outside this lane.
